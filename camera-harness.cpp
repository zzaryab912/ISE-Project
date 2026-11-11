#include "camera-harness.h"

#include <utility>
#include <opencv2/imgproc.hpp>

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



   
   InputResult ImageSource::prepare(const cv::Mat& input) const{



   }

   bool ImageSource::locate(const cv::Mat& gray, std::vector<std::vector<cv::Point2f>>& boxes) const{


   }

   cv::Mat ImageSource::straighten(const cv::Mat& gray, const std::vector<cv::Point2f>& box) const{

    
   }
