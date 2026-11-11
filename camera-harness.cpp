#include "camera-harness.h"

#include <opencv2/imgproc.hpp>

InputResult ImageSource::loadFile(const cv::Mat& image) const {
    if (image.empty() || image.data == nullptr || image.dims != 2) {
        return {ImageStatus::InvalidImage, "Input image is empty or invalid.", cv::Mat(), cv::Mat()};
    }

    // Ensure compatibility with CV_8UC3 expected by downstream prepare()
    cv::Mat decoded;
    try {
        if (image.type() == CV_8UC3) {
            decoded = image; // Shallow reference copy (O(1))
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

   bool ImageSource::startCamera(int index, std::string& message){



   }

   InputResult ImageSource::nextFrame(){


   }

   void ImageSource::stopCamera(){


   }

   ImageSource::~ImageSource(){

   }



   
   InputResult ImageSource::prepare(const cv::Mat& input) const{



   }

   bool ImageSource::locate(const cv::Mat& gray, std::vector<std::vector<cv::Point2f>>& boxes) const{


   }

   cv::Mat ImageSource::straighten(const cv::Mat& gray, const std::vector<cv::Point2f>& box) const{

    
   }
