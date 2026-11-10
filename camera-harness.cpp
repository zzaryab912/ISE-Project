#include "camera-harness.h"


   InputResult ImageSource::loadFile(const QString& path) const{



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
