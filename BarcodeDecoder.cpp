#include "BarcodeDecoder.h"
#include "barcodeGenerator.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <cstddef>
#include <exception>
#include <opencv2/imgproc.hpp>

using namespace std;

DecodeResult BarcodeDecoder::decode(const cv::Mat& barcode) const
{
    // Accept only a non-empty, two-dimensional, 8-bit grayscale image.
    if (barcode.empty() || barcode.dims != 2 ||
        barcode.type() != CV_8UC1) {
        return {false, "Invalid barcode image.", ""};
    }

    // Practical processing limits, not Code 128 requirements.
    constexpr int MAX_SIDE = 8192;
    constexpr size_t MAX_PIXELS = 16'000'000;

    if (barcode.cols > MAX_SIDE || barcode.rows > MAX_SIDE ||
        barcode.total() > MAX_PIXELS) {
        return {
            false,
            "Barcode image is too large. Please provide a smaller crop.",
            ""
        };
    }

    // This strict reader requires both margins and at least one pixel/module.
    if (barcode.cols < 121) {
        return {
            false,
            "Barcode image is too narrow to read reliably.",
            ""
        };
    }

    try {
        cv::Mat binary;

        cv::threshold(
            barcode,
            binary,
            0,   // Otsu chooses the cutoff; this value is ignored.
            255, // White output pixels have value 255.
            cv::THRESH_BINARY | cv::THRESH_OTSU
        );

        string acceptedCode;
        const int rowCount = min(binary.rows, 31);

        // Scan rows spread from top to bottom, then repeat after flipping.
        for (int direction = 0; direction < 2; ++direction) {
            for (int index = 0; index < rowCount; ++index) {
                int row = 0;

                if (rowCount > 1) {
                    row = index * (binary.rows - 1) / (rowCount - 1);
                }

                string rowCode;
                bool ambiguous = false;

                const bool found = decodeLine(
                    binary, row, rowCode, ambiguous
                );

                if (ambiguous ||
                    (found &&
                     !acceptedCode.empty() &&
                     acceptedCode != rowCode)) {
                    return {
                        false,
                        "Conflicting barcode readings. Please try again.",
                        ""
                    };
                }

                if (found) {
                    acceptedCode = rowCode;
                }
            }

            if (direction == 0) {
                cv::flip(binary, binary, 1);
            }
        }

        if (!acceptedCode.empty()) {
            return {
                true,
                "Barcode read successfully.",
                acceptedCode
            };
        }

        return {
            false,
            "Could not read the barcode. Please try again.",
            ""
        };
    }
    catch (const exception&) {
        return {
            false,
            "Barcode processing failed. Please try again.",
            ""
        };
    }
}

bool BarcodeDecoder::decodeLine(const cv::Mat& binary,
                                int row,
                                string& code,
                                bool& ambiguous) const
{
    code.clear();
    ambiguous = false;

    if (binary.empty() ||
        binary.dims != 2 ||
        binary.type() != CV_8UC1 ||
        row < 0 ||
        row >= binary.rows) {
        return false;
    }

    const unsigned char* pixels = binary.ptr<unsigned char>(row);

    vector<int> runs;
    runs.reserve(binary.cols);

    int position = 0;
    unsigned char colour = 255;

    // Even run indices are white; odd indices are black.
    // A row starting black gets a zero-width initial white run.
    while (position < binary.cols) {
        if (pixels[position] != 0 && pixels[position] != 255) {
            return false;
        }

        const int start = position;

        while (position < binary.cols &&
               pixels[position] == colour) {
            ++position;
        }

        runs.push_back(position - start);

        if (colour == 255) {
            colour = 0;
        } else {
            colour = 255;
        }
    }

    // Margins + Start B + six digits + checksum + Stop:
    // 1 + 6 + 36 + 6 + 7 + 1 = 57 runs.
    const size_t candidateRuns = 57;

    if (runs.size() < candidateRuns) {
        return false;
    }

    vector<int> candidate(candidateRuns);

    // Advance by two to start each candidate on a white run.
    for (size_t begin = 0;
         begin <= runs.size() - candidateRuns;
         begin += 2) {

        copy_n(
            runs.begin() + begin,
            candidateRuns,
            candidate.begin()
        );

        string candidateCode;

        if (!readCandidate(candidate, candidateCode)) {
            continue;
        }

        // Reject two different valid codes within the same row.
        if (!code.empty() && code != candidateCode) {
            code.clear();
            ambiguous = true;
            return false;
        }

        code = candidateCode;
    }

    return !code.empty();
}

bool BarcodeDecoder::readCandidate(const vector<int>& widths,
                                  string& code) const
{
    code.clear();

    if (widths.size() != 57) {
        return false;
    }

    for (int width : widths) {
        if (width <= 0) {
            return false;
        }
    }

    // Start + six digits + checksum + stop:
    // 11 + 66 + 11 + 13 = 101 modules.
    // Exclude the two quiet zones.
    double barcodeWidth = 0.0;

    for (int i = 1; i <= 55; ++i) {
        barcodeWidth += widths[i];
    }

    const double moduleWidth = barcodeWidth / 101.0;

    if (!std::isfinite(moduleWidth) || moduleWidth < 1.0) {
        return false;
    }

    // Allow one pixel of boundary-rounding uncertainty.
    constexpr double quietAllowance = 1.0;
    const double requiredQuiet = 10.0 * moduleWidth;

    if (widths.front() + quietAllowance < requiredQuiet ||
        widths.back() + quietAllowance < requiredQuiet) {
        return false;
    }

    const double runTolerance =
        std::max(1.0, 0.45 * moduleWidth);

    // Return mean squared error in module units.
    // Return -1 when the symbol does not match.
    auto symbolError = [&](int firstRun, int value) -> double {
        const string pattern = getCode128Pattern(value);

        const bool stop = value == 106;
        const size_t expectedModules = stop ? 13 : 11;
        const int expectedRuns = stop ? 7 : 6;

        if (firstRun < 1 ||
            firstRun + expectedRuns > 56 ||
            pattern.size() != expectedModules ||
            pattern.front() != '1' ||
            pattern.back() != (stop ? '1' : '0')) {
            return -1.0;
        }

        size_t bit = 0;
        int run = 0;
        double error = 0.0;

        while (bit < pattern.size()) {
            const char colour = pattern[bit];

            if ((colour != '0' && colour != '1') ||
                run >= expectedRuns) {
                return -1.0;
            }

            int modules = 0;

            while (bit < pattern.size() &&
                   pattern[bit] == colour) {
                ++modules;
                ++bit;
            }

            const double expectedPixels =
                modules * moduleWidth;

            const double difference =
                std::abs(widths[firstRun + run] - expectedPixels);

            if (difference > runTolerance) {
                return -1.0;
            }

            const double normalized = difference / moduleWidth;
            error += normalized * normalized;

            ++run;
        }

        return run == expectedRuns
            ? error / expectedRuns
            : -1.0;
    };

    // Validate Start B and Stop.
    if (symbolError(1, 104) < 0.0 ||
        symbolError(49, 106) < 0.0) {
        return false;
    }

    string decoded;
    decoded.reserve(6);

    // Reject digit matches with nearly equal scores.
    // Validate this threshold against actual camera crops.
    constexpr double minimumScoreGap = 0.05;

    for (int digit = 0; digit < 6; ++digit) {
        int bestValue = -1;

        double bestError =
            std::numeric_limits<double>::infinity();

        double secondError =
            std::numeric_limits<double>::infinity();

        // Set B values 16–25 represent digits 0–9.
        for (int value = 16; value <= 25; ++value) {
            const double error =
                symbolError(7 + digit * 6, value);

            if (error < 0.0) {
                continue;
            }

            if (error < bestError) {
                secondError = bestError;
                bestError = error;
                bestValue = value;
            } else if (error < secondError) {
                secondError = error;
            }
        }

        if (bestValue == -1 ||
            secondError - bestError < minimumScoreGap) {
            return false;
        }

        decoded.push_back(
            static_cast<char>(bestValue + 32)
        );
    }

    // Enforce exactly six digits and no leading zero.
    if (!isValid6DigitInput(decoded)) {
        return false;
    }

   const int checksum = calculateChecksum(104, decoded);

if (checksum < 0 || checksum > 102) {
    return false;
}

// Decode the observed checksum independently.
int observedChecksum = -1;

double bestChecksumError =
    std::numeric_limits<double>::infinity();

double secondChecksumError =
    std::numeric_limits<double>::infinity();

for (int value = 0; value <= 102; ++value) {
    const double error = symbolError(43, value);

    if (error < 0.0) {
        continue;
    }

    if (error < bestChecksumError) {
        secondChecksumError = bestChecksumError;
        bestChecksumError = error;
        observedChecksum = value;
    } else if (error < secondChecksumError) {
        secondChecksumError = error;
    }
}

// Require the correct checksum and reject ambiguous readings.
if (observedChecksum != checksum ||
    secondChecksumError - bestChecksumError < minimumScoreGap) {
    return false;
}

    code = decoded;
    return true;
}