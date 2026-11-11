#include "camera-harness.h"

#include <QByteArray>
#include <QFile>
#include <QFileInfo>
#include <QIODevice>
#include <limits>
#include <opencv2/imgcodecs.hpp>



bool isSupportedImageBytes(const QByteArray& bytes) {
    if (bytes.size() >= 8) {
        const auto* data = reinterpret_cast<const unsigned char*>(bytes.constData());
        const bool isPng =
            data[0] == 0x89 && data[1] == 0x50 && data[2] == 0x4E && data[3] == 0x47 &&
            data[4] == 0x0D && data[5] == 0x0A && data[6] == 0x1A && data[7] == 0x0A;
        if (isPng) {
            return true;
        }
    }

    if (bytes.size() >= 3) {
        const auto* data = reinterpret_cast<const unsigned char*>(bytes.constData());
        const bool isJpeg = data[0] == 0xFF && data[1] == 0xD8 && data[2] == 0xFF;
        if (isJpeg) {
            return true;
        }
    }

    return false;
}


InputResult ImageSource::loadFile(const QString& path) const {
    if (path.trimmed().isEmpty()) {
        return {ImageStatus::InvalidImage, "Image file path is empty.", cv::Mat(), cv::Mat()};
    }

    const QFileInfo fileInfo(path);
    if (!fileInfo.exists() || !fileInfo.isFile()) {
        return {ImageStatus::InvalidImage, "Image file does not exist.", cv::Mat(), cv::Mat()};
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {ImageStatus::InvalidImage, "Could not open image file.", cv::Mat(), cv::Mat()};
    }

    const QByteArray bytes = file.readAll();
    if (file.error() != QFile::NoError || bytes.isEmpty()) {
        return {ImageStatus::InvalidImage, "Could not read image file.", cv::Mat(), cv::Mat()};
    }

    if (bytes.size() > std::numeric_limits<int>::max()) {
        return {ImageStatus::InvalidImage, "Image file is too large to decode.", cv::Mat(), cv::Mat()};
    }

    if (!isSupportedImageBytes(bytes)) {
        return {ImageStatus::InvalidImage, "Unsupported or corrupt image file. Please select a PNG or JPEG image.", cv::Mat(), cv::Mat()};
    }

    cv::Mat decoded;
    try {
        const cv::Mat rawBuffer(1, static_cast<int>(bytes.size()), CV_8UC1, const_cast<char*>(bytes.constData()));
        decoded = cv::imdecode(rawBuffer, cv::IMREAD_COLOR);
    } catch (const cv::Exception&) {
        return {ImageStatus::InvalidImage, "Image file is corrupt or could not be decoded.", cv::Mat(), cv::Mat()};
    }

    if (decoded.empty() || decoded.type() != CV_8UC3) {
        return {ImageStatus::InvalidImage, "Image file is corrupt or could not be decoded.", cv::Mat(), cv::Mat()};
    }

    InputResult result = prepare(decoded);
    result.preview = cv::Mat();
    return result;
}

// Placeholders: replace with Person 4's completed implementation.
bool ImageSource::startCamera(int, std::string& message) {
    message = "Camera input is awaiting Person 4's implementation.";
    return false;
}

InputResult ImageSource::nextFrame() {
    return {ImageStatus::CameraError, "Camera input is not implemented yet.", cv::Mat(), cv::Mat()};
}

void ImageSource::stopCamera() {
    camera.release();
}

ImageSource::~ImageSource() {
    stopCamera();
}

InputResult ImageSource::prepare(const cv::Mat&) const {
    return {ImageStatus::ProcessingError, "Barcode preparation is awaiting Person 4's implementation.", cv::Mat(), cv::Mat()};
}

bool ImageSource::locate(const cv::Mat&, std::vector<std::vector<cv::Point2f>>& boxes) const {
    boxes.clear();
    return false; // Detection is not implemented yet.
}

cv::Mat ImageSource::straighten(const cv::Mat&, const std::vector<cv::Point2f>&) const {
    return cv::Mat(); // Straightening is not implemented yet.
}
