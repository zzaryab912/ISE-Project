#pragma once

#include "ReaderContracts.h"
#include <QString>
#include <string>
#include <vector>
#include <opencv2/core.hpp>
#include <opencv2/videoio.hpp>

class ImageSource{

public:
    InputResult loadFile(const QString& path) const;
    bool startCamera(int index, std::string& message);
    InputResult nextFrame();
    void stopCamera();
    ~ImageSource();

private:
    cv::VideoCapture camera;

    InputResult prepare(const cv::Mat& input) const;
    bool locate(const cv::Mat& gray, std::vector<std::vector<cv::Point2f>>& boxes) const;
    cv::Mat straighten(const cv::Mat& gray, const std::vector<cv::Point2f>& box) const;
};