#ifndef READER_CONTRACTS_H
#define READER_CONTRACTS_H

#include <string>
#include <opencv2/core/mat.hpp>         // makes mat for OpenCV available

using namespace std;

// structure
//      imageStatus Enum Class
//      InputResult struct 

enum class ImageStatus {

    // all different image statuses 
    // numbers assgined to each option in order, starting from 0
    
    Ready,
    NoCandidate,
    MultipleCandidates,
    InvalidImage,
    CameraError,
    ProcessingError
};

struct InputResult {                // this is what person 4 returns, person 1 receives this
    ImageStatus status;             // it contains the status, a message with it, and barcode and preview
    string message;                 // mat is an OpenCV object that can hold image pixels in memory 
    cv::Mat barcode;
    cv::Mat preview;
};

struct DecodeResult {               // this is what person 5 returns, it contains bool result, a message and the decoded code
    bool found;
    string message;
    string code;
};

#endif
