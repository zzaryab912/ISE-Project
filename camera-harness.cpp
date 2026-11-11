#include "camera-harness.h"

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstddef>
#include <exception>
#include <limits>
#include <utility>
#include <opencv2/imgproc.hpp>

namespace {

float maxInsideShift(const cv::Point2f& pt,
                     const cv::Point2f& dir,
                     float maxShift,
                     int cols,
                     int rows) {

    if (maxShift <= 0.0f || cols <= 0 || rows <= 0) {

        return 0.0f;
    }

    const float maxX = static_cast<float>(cols - 1);
    const float maxY = static_cast<float>(rows - 1);

    if (pt.x < 0.0f || pt.x > maxX || pt.y < 0.0f || pt.y > maxY) {

        return 0.0f;
    }

    float limit = maxShift;
    constexpr float kEps = 1e-6f;

    if (dir.x > kEps) {
        limit = std::min(limit, (maxX - pt.x) / dir.x);
    } else if (dir.x < -kEps) {
        limit = std::min(limit, (0.0f - pt.x) / dir.x);
    }

    if (dir.y > kEps) {
        limit = std::min(limit, (maxY - pt.y) / dir.y);
    } else if (dir.y < -kEps) {
        limit = std::min(limit, (0.0f - pt.y) / dir.y);
    }

    return std::max(0.0f, limit);
}

bool hasBarcodeTransitions(const cv::Mat& fullGray,
                           const std::vector<cv::Point2f>& fullBox) {

    if (fullGray.empty() || fullBox.size() != 4) {
        return false;
    }

    const float width = static_cast<float>(cv::norm(fullBox[1] - fullBox[0]));
    const float height = static_cast<float>(cv::norm(fullBox[3] - fullBox[0]));

    if (width < 1.0f || height < 1.0f) {

        return false;
    }

    const int patchW = std::clamp(static_cast<int>(std::lround(width)), 120, 600);
    const int patchH = std::clamp(static_cast<int>(std::lround(height)), 15, 120);

    const std::vector<cv::Point2f> dstPts = {
        cv::Point2f(0.0f, 0.0f),
        cv::Point2f(static_cast<float>(patchW - 1), 0.0f),
        cv::Point2f(static_cast<float>(patchW - 1), static_cast<float>(patchH - 1)),
        cv::Point2f(0.0f, static_cast<float>(patchH - 1))
    };

    const cv::Mat transform = cv::getPerspectiveTransform(fullBox, dstPts);
    cv::Mat patch;
    cv::warpPerspective(fullGray, patch, transform, cv::Size(patchW, patchH),
                        cv::INTER_LINEAR, cv::BORDER_REPLICATE);

    const int yStart = patch.rows / 4;
    const int yEnd = std::max(yStart + 1, (patch.rows * 3) / 4);
    const cv::Mat midBand = patch.rowRange(yStart, yEnd);

    double minVal = 0.0;
    double maxVal = 0.0;
    cv::minMaxLoc(midBand, &minVal, &maxVal);
    if (maxVal - minVal < 20.0) {
        return false;
    }

    cv::Mat vertSmooth;
    cv::boxFilter(midBand, vertSmooth, CV_8U, cv::Size(1, 3),
                  cv::Point(-1, -1), true, cv::BORDER_REPLICATE);

    int blockSize = std::max(21, patchW / 5);
    if (blockSize % 2 == 0) {
        ++blockSize;
    }

    cv::Mat binary;
    cv::adaptiveThreshold(vertSmooth, binary, 255, cv::ADAPTIVE_THRESH_MEAN_C,
                          cv::THRESH_BINARY, blockSize, 8.0);

    int passingRows = 0;
    constexpr int kSampleRows = 5;
    for (int s = 0; s < kSampleRows; ++s) {
        const int rowIdx = (binary.rows == 1)
            ? 0
            : (s * (binary.rows - 1)) / (kSampleRows - 1);
        const unsigned char* rowPtr = binary.ptr<unsigned char>(rowIdx);
        int transitions = 0;
        for (int c = 1; c < binary.cols; ++c) {
            if (rowPtr[c] != rowPtr[c - 1]) {
                ++transitions;
            }
        }
        if (transitions >= 22 && transitions <= 110) {
            ++passingRows;
        }
    }

    return passingRows >= 3;
}

} // namespace

InputResult ImageSource::loadFile(const cv::Mat& image) const {

    if (image.empty() || image.data == nullptr || image.dims != 2) {

        return {ImageStatus::InvalidImage, "Input image is empty or invalid.", cv::Mat(), cv::Mat()};
    }

    // Ensure compatibility with CV_8UC3
    cv::Mat decoded;
    try {
        if (image.type() == CV_8UC3) {
            decoded = image;
        } else if (image.type() == CV_8UC1) {
            cv::cvtColor(image, decoded, cv::COLOR_GRAY2BGR);
        } else if (image.type() == CV_8UC4) {
            cv::cvtColor(image, decoded, cv::COLOR_BGRA2BGR);
        } else {
            return {ImageStatus::InvalidImage, "Unsupported image format. Expected 8-bit 1, 3, or 4 channel image.", cv::Mat(), cv::Mat()};
        }
    } catch (const cv::Exception&) {
        return {ImageStatus::InvalidImage, "Failed to process input image.", cv::Mat(), cv::Mat()};
    }

    if (decoded.empty() || decoded.type() != CV_8UC3) {
        return {ImageStatus::InvalidImage, "Input image is empty or invalid.", cv::Mat(), cv::Mat()};
    }

    InputResult result = prepare(decoded);
    result.preview = cv::Mat();
    return result;
}

bool ImageSource::startCamera(int index, std::string& message) {
    try {
        if (camera.isOpened()) {
            camera.release();
        }
    } catch (const cv::Exception&) {
    }

    if (index < 0) {
        message = "Invalid camera index.";
        return false;
    }

    try {
        if (!camera.open(index) || !camera.isOpened()) {
            if (camera.isOpened()) {
                camera.release();
            }
            message = "Could not open camera.";
            return false;
        }
    } catch (const cv::Exception&) {
        try {
            if (camera.isOpened()) {
                camera.release();
            }
        } catch (const cv::Exception&) {
        }
        message = "Could not open camera.";
        return false;
    }

    message = "Camera started.";
    return true;
}

InputResult ImageSource::nextFrame() {

    if (!camera.isOpened()) {        
        return {ImageStatus::CameraError, "Camera is not open.", cv::Mat(), cv::Mat()};
    }

    cv::Mat rawFrame;
    try {
        if (!camera.read(rawFrame) || rawFrame.empty()) {
            return {ImageStatus::CameraError, "Could not read frame from camera.", cv::Mat(), cv::Mat()};
        }
    } catch (const cv::Exception&) {
        return {ImageStatus::CameraError, "Camera frame capture failed.", cv::Mat(), cv::Mat()};
    }

    cv::Mat bgrFrame;
    
    try {
        if (rawFrame.dims != 2 || rawFrame.depth() != CV_8U) {
            return {ImageStatus::CameraError, "Unsupported camera frame format.", cv::Mat(), cv::Mat()};
        }

        if (rawFrame.type() == CV_8UC3) {
            bgrFrame = rawFrame.clone();
        } else if (rawFrame.type() == CV_8UC4) {
            cv::cvtColor(rawFrame, bgrFrame, cv::COLOR_BGRA2BGR);
        } else if (rawFrame.type() == CV_8UC1) {
            cv::cvtColor(rawFrame, bgrFrame, cv::COLOR_GRAY2BGR);
        } else {
            return {ImageStatus::CameraError, "Unsupported camera frame format.", cv::Mat(), cv::Mat()};
        }
    } catch (const cv::Exception&) {
        return {ImageStatus::CameraError, "Failed to process camera frame.", cv::Mat(), cv::Mat()};
    }

    if (bgrFrame.empty() || bgrFrame.type() != CV_8UC3) {
        return {ImageStatus::CameraError, "Unsupported camera frame format.", cv::Mat(), cv::Mat()};
    }

    InputResult result = prepare(bgrFrame);
    result.preview = std::move(bgrFrame);
    return result;
}

void ImageSource::stopCamera() {
    
    try {
        if (camera.isOpened()) {
            camera.release();
        }
    } catch (const cv::Exception&) {
    }
}

ImageSource::~ImageSource() {

    stopCamera();
}



   
InputResult ImageSource::prepare(const cv::Mat& input) const {
    if (input.empty() || input.dims != 2) {
        return {ImageStatus::InvalidImage, "Input image is empty or invalid.", cv::Mat(), cv::Mat()};
    }

    try {
        cv::Mat gray;
        if (input.type() == CV_8UC1) {
            gray = input;
        } else if (input.type() == CV_8UC3) {
            cv::cvtColor(input, gray, cv::COLOR_BGR2GRAY);
        } else if (input.type() == CV_8UC4) {
            cv::cvtColor(input, gray, cv::COLOR_BGRA2GRAY);
        } else {
            return {ImageStatus::InvalidImage, "Unsupported image pixel format.", cv::Mat(), cv::Mat()};
        }

        std::vector<std::vector<cv::Point2f>> boxes;
        const bool found = locate(gray, boxes);
        if (!found || boxes.empty()) {
            return {ImageStatus::NoCandidate, "No barcode found.", cv::Mat(), cv::Mat()};
        }
        if (boxes.size() > 1) {
            return {ImageStatus::MultipleCandidates, "Please scan only one barcode at a time.", cv::Mat(), cv::Mat()};
        }

        cv::Mat barcode = straighten(gray, boxes.front());
        if (barcode.empty() || barcode.dims != 2 || barcode.type() != CV_8UC1) {
            return {ImageStatus::ProcessingError, "Could not prepare barcode candidate.", cv::Mat(), cv::Mat()};
        }
        if (!barcode.isContinuous()) {
            barcode = barcode.clone();
        }

        return {ImageStatus::Ready, "Barcode candidate ready.", std::move(barcode), cv::Mat()};
    } catch (const cv::Exception&) {
        return {ImageStatus::ProcessingError, "Image processing failed.", cv::Mat(), cv::Mat()};
    } catch (const std::exception&) {
        return {ImageStatus::ProcessingError, "Image processing failed.", cv::Mat(), cv::Mat()};
    }
}

bool ImageSource::locate(const cv::Mat& gray, std::vector<std::vector<cv::Point2f>>& boxes) const {
    boxes.clear();

    if (gray.empty() || gray.dims != 2 || gray.type() != CV_8UC1 ||
        gray.cols < 40 || gray.rows < 20) {
        return false;
    }

    cv::Mat workGray;
    double scale = 1.0;
    constexpr int kMaxWorkDim = 640;
    const int maxDim = std::max(gray.cols, gray.rows);
    if (maxDim > kMaxWorkDim) {
        scale = static_cast<double>(kMaxWorkDim) / static_cast<double>(maxDim);
        cv::resize(gray, workGray, cv::Size(), scale, scale, cv::INTER_AREA);
    } else {
        workGray = gray;
    }

    if (workGray.cols < 20 || workGray.rows < 10) {
        return false;
    }

    cv::Mat gx, gy;
    cv::Sobel(workGray, gx, CV_32F, 1, 0, 3);
    cv::Sobel(workGray, gy, CV_32F, 0, 1, 3);

    cv::Mat gx2, gy2, gxy;
    cv::multiply(gx, gx, gx2);
    cv::multiply(gy, gy, gy2);
    cv::multiply(gx, gy, gxy);

    const int workMax = std::max(workGray.cols, workGray.rows);
    const int smoothSize = (workMax < 320) ? 7 : 13;
    const cv::Size smoothKernel(smoothSize, smoothSize);

    cv::Mat jxx, jyy, jxy;
    cv::boxFilter(gx2, jxx, CV_32F, smoothKernel, cv::Point(-1, -1), true, cv::BORDER_REPLICATE);
    cv::boxFilter(gy2, jyy, CV_32F, smoothKernel, cv::Point(-1, -1), true, cv::BORDER_REPLICATE);
    cv::boxFilter(gxy, jxy, CV_32F, smoothKernel, cv::Point(-1, -1), true, cv::BORDER_REPLICATE);

    constexpr float kMinEnergy = 1200.0f;
    constexpr float kMinCoherence = 0.55f;

    cv::Mat mask(workGray.size(), CV_8UC1, cv::Scalar(0));
    for (int r = 0; r < workGray.rows; ++r) {
        const float* px = jxx.ptr<float>(r);
        const float* py = jyy.ptr<float>(r);
        const float* pxy = jxy.ptr<float>(r);
        unsigned char* pm = mask.ptr<unsigned char>(r);

        for (int c = 0; c < workGray.cols; ++c) {
            const float sxx = px[c];
            const float syy = py[c];
            const float sxy = pxy[c];
            const float energy = sxx + syy;
            if (energy < kMinEnergy) {
                continue;
            }

            const float diff = sxx - syy;
            const float aniso = std::sqrt(diff * diff + 4.0f * sxy * sxy);
            const float coherence = aniso / (energy + 1e-6f);
            if (coherence >= kMinCoherence) {
                pm[c] = 255;
            }
        }
    }

    const int closeSize = (workMax < 320) ? 3 : 5;
    const cv::Mat closeKernel = cv::getStructuringElement(cv::MORPH_RECT, cv::Size(closeSize, closeSize));
    cv::morphologyEx(mask, mask, cv::MORPH_CLOSE, closeKernel);

    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(mask, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);

    const cv::Rect imageBounds(0, 0, workGray.cols, workGray.rows);
    for (const auto& contour : contours) {
        const double area = cv::contourArea(contour);
        if (area < 200.0) {
            continue;
        }

        const cv::Rect bbox = cv::boundingRect(contour) & imageBounds;
        if (bbox.width <= 0 || bbox.height <= 0) {
            continue;
        }

        cv::Mat localMask(bbox.size(), CV_8UC1, cv::Scalar(0));
        const std::vector<std::vector<cv::Point>> singleContour = {contour};
        cv::drawContours(localMask, singleContour, 0, cv::Scalar(255),
                         cv::FILLED, cv::LINE_8, cv::noArray(), INT_MAX, -bbox.tl());

        const cv::Mat subXX = jxx(bbox);
        const cv::Mat subYY = jyy(bbox);
        const cv::Mat subXY = jxy(bbox);

        double sumXX = 0.0;
        double sumYY = 0.0;
        double sumXY = 0.0;
        for (int r = 0; r < bbox.height; ++r) {
            const unsigned char* pm = localMask.ptr<unsigned char>(r);
            const float* pxx = subXX.ptr<float>(r);
            const float* pyy = subYY.ptr<float>(r);
            const float* pxy = subXY.ptr<float>(r);
            for (int c = 0; c < bbox.width; ++c) {
                if (pm[c] != 0) {
                    sumXX += static_cast<double>(pxx[c]);
                    sumYY += static_cast<double>(pyy[c]);
                    sumXY += static_cast<double>(pxy[c]);
                }
            }
        }

        const double denom = sumXX + sumYY;
        if (denom <= 1e-6) {
            continue;
        }

        const double diff = sumXX - sumYY;
        const double regionCoherence = std::sqrt(diff * diff + 4.0 * sumXY * sumXY) / denom;
        if (regionCoherence < 0.52) {
            continue;
        }

        const double phi = 0.5 * std::atan2(2.0 * sumXY, sumXX - sumYY);
        const cv::Point2f u(static_cast<float>(std::cos(phi)), static_cast<float>(std::sin(phi)));
        const cv::Point2f v(-u.y, u.x);

        float uMin = std::numeric_limits<float>::max();
        float uMax = -std::numeric_limits<float>::max();
        float vMin = std::numeric_limits<float>::max();
        float vMax = -std::numeric_limits<float>::max();

        for (const cv::Point& pt : contour) {
            const float px = static_cast<float>(pt.x);
            const float py = static_cast<float>(pt.y);
            const float projU = px * u.x + py * u.y;
            const float projV = px * v.x + py * v.y;
            uMin = std::min(uMin, projU);
            uMax = std::max(uMax, projU);
            vMin = std::min(vMin, projV);
            vMax = std::max(vMax, projV);
        }

        const float boxW = uMax - uMin;
        const float boxH = vMax - vMin;
        if (boxW <= 1.0f || boxH <= 1.0f) {
            continue;
        }

        const float fullW = boxW / static_cast<float>(scale);
        const float fullH = boxH / static_cast<float>(scale);
        if (fullW < 80.0f || fullH < 12.0f) {
            continue;
        }

        const float aspect = boxW / boxH;
        if (aspect < 0.4f || aspect > 15.0f) {
            continue;
        }

        const double fillRatio = area / static_cast<double>(boxW * boxH);
        if (fillRatio < 0.45) {
            continue;
        }

        const float invScale = static_cast<float>(1.0 / scale);
        std::vector<cv::Point2f> fullBox = {
            (u * uMin + v * vMin) * invScale,
            (u * uMax + v * vMin) * invScale,
            (u * uMax + v * vMax) * invScale,
            (u * uMin + v * vMax) * invScale
        };

        if (!hasBarcodeTransitions(gray, fullBox)) {
            continue;
        }

        boxes.push_back(std::move(fullBox));
    }

    return !boxes.empty();
}

cv::Mat ImageSource::straighten(const cv::Mat& gray, const std::vector<cv::Point2f>& box) const {

    if (gray.empty() || gray.dims != 2 || gray.type() != CV_8UC1 || box.size() != 4) {

        return cv::Mat();
    }

    const cv::Point2f& tl = box[0];
    const cv::Point2f& tr = box[1];
    const cv::Point2f& br = box[2];
    const cv::Point2f& bl = box[3];

    const cv::Point2f dirU = ((tr - tl) + (br - bl)) * 0.5f;
    const cv::Point2f dirV = ((bl - tl) + (br - tr)) * 0.5f;
    const float width = static_cast<float>(cv::norm(dirU));
    const float height = static_cast<float>(cv::norm(dirV));

    if (width < 1.0f || height < 1.0f) {
        return cv::Mat();
    }

    const cv::Point2f u = dirU / width;
    const float maxExpand = 0.12f * width;

    const float leftExpand = std::min(
        maxInsideShift(tl, -u, maxExpand, gray.cols, gray.rows),
        maxInsideShift(bl, -u, maxExpand, gray.cols, gray.rows)
    );

    const float rightExpand = std::min(
        maxInsideShift(tr, u, maxExpand, gray.cols, gray.rows),
        maxInsideShift(br, u, maxExpand, gray.cols, gray.rows)
    );

    const cv::Point2f expTL = tl - u * leftExpand;
    const cv::Point2f expBL = bl - u * leftExpand;
    const cv::Point2f expTR = tr + u * rightExpand;
    const cv::Point2f expBR = br + u * rightExpand;

    const float expandedWidth = width + leftExpand + rightExpand;
    const int dstW = std::max(1, static_cast<int>(std::lround(expandedWidth)) + 1);
    const int dstH = std::max(1, static_cast<int>(std::lround(height)) + 1);

    if (dstW > 8192 || dstH > 8192 ||
        static_cast<std::size_t>(dstW) * static_cast<std::size_t>(dstH) > 16'000'000) {

        return cv::Mat();
    }

    const std::vector<cv::Point2f> srcPts = {expTL, expTR, expBR, expBL};

    const std::vector<cv::Point2f> dstPts = {
        cv::Point2f(0.0f, 0.0f),
        cv::Point2f(static_cast<float>(dstW - 1), 0.0f),
        cv::Point2f(static_cast<float>(dstW - 1), static_cast<float>(dstH - 1)),
        cv::Point2f(0.0f, static_cast<float>(dstH - 1))
    };

    const cv::Mat transform = cv::getPerspectiveTransform(srcPts, dstPts);
    cv::Mat warped;

    cv::warpPerspective(gray, warped, transform, cv::Size(dstW, dstH),
                        cv::INTER_LINEAR, cv::BORDER_CONSTANT, cv::Scalar(0));
    return warped;
}
