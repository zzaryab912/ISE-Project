#pragma once

//this file is shared between member 4 (rayyan) and member 5 (rayan dar). so ensure that only one 
//instance of this file remains at end. overlapping functions

#include <string>
#include <opencv2/core/mat.hpp>

enum class ImageStatus{

    Ready,
    NoCandidate,
    MultipleCandidates,
    InvalidImage,
    CameraError,
    ProcessingError
};

struct InputResult{

    ImageStatus status;
    std::string message;
    cv::Mat barcode;
    cv::Mat preview;
};

struct DecodeResult{

    bool found;
    std::string message;
    std::string code;
};