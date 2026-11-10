#include "BarcodeDecoder.h"
#include "../generator/barcodeGenerator.h"

#include <algorithm>
#include <cmath>
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

    // One full candidate has 57 alternating white/black runs.
    if (widths.size() != 57) {
        return false;
    }

    for (int width : widths) {
        if (width <= 0) {
            return false;
        }
    }

    // Start B occupies six runs containing eleven modules.
    double startWidth = 0.0;

    for (int index = 1; index <= 6; ++index) {
        startWidth += widths[index];
    }

    const double moduleWidth = startWidth / 11.0;

    if (!std::isfinite(moduleWidth) || moduleWidth < 1.0) {
        return false;
    }

    // Require real white margins on both sides of the barcode.
    if (widths.front() < 10.0 * moduleWidth ||
        widths.back() < 10.0 * moduleWidth) {
        return false;
    }

    // Chosen measurement allowance; verify with actual camera crops.
    const double tolerance = 0.45 * moduleWidth;

    // Compare measured runs with one pattern from Member 2's table.
    auto matchesSymbol = [&](int firstRun, int value) -> bool {
        const string pattern = getCode128Pattern(value);

        const bool stop = value == 106;
        const size_t expectedModules = stop ? 13 : 11;
        const int expectedRuns = stop ? 7 : 6;

        if (firstRun < 0 ||
            firstRun > static_cast<int>(widths.size()) - expectedRuns) {
            return false;
        }

        if (pattern.size() != expectedModules ||
            pattern.front() != '1' ||
            pattern.back() != (stop ? '1' : '0')) {
            return false;
        }

        size_t bit = 0;
        int run = 0;

        while (bit < pattern.size()) {
            const char colour = pattern[bit];

            if (colour != '0' && colour != '1') {
                return false;
            }

            int modules = 0;

            while (bit < pattern.size() &&
                   pattern[bit] == colour) {
                ++modules;
                ++bit;
            }

            if (run >= expectedRuns) {
                return false;
            }

            const double expectedPixels = modules * moduleWidth;

            const double difference =
                std::abs(widths[firstRun + run] - expectedPixels);

            if (difference > tolerance) {
                return false;
            }

            ++run;
        }

        return run == expectedRuns;
    };

    // Verify Start B.
    if (!matchesSymbol(1, 104)) {
        return false;
    }

    string decoded;
    decoded.reserve(6);

    // Read exactly six digit symbols.
    for (int digit = 0; digit < 6; ++digit) {
        int matchedValue = -1;

        // Set B values 16 through 25 represent '0' through '9'.
        for (int value = 16; value <= 25; ++value) {
            if (matchesSymbol(7 + digit * 6, value)) {
                // Reject a measurement matching more than one digit.
                if (matchedValue != -1) {
                    return false;
                }

                matchedValue = value;
            }
        }

        if (matchedValue == -1) {
            return false;
        }

        decoded.push_back(
            static_cast<char>(matchedValue + 32)
        );
    }

    // Apply Member 2's validation, including no leading zero.
    if (!isValid6DigitInput(decoded)) {
        return false;
    }

    // Compare the observed checksum bars with the expected pattern.
    const int checksum = calculateChecksum(104, decoded);

    if (checksum < 0 ||
        checksum > 102 ||
        !matchesSymbol(43, checksum) ||
        !matchesSymbol(49, 106)) {
        return false;
    }

    code = decoded;
    return true;
}