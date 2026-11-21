#pragma once
#include "PosTypes.h"
#include "ReaderContracts.h"
#include <string>
#include <chrono>

class ImageSource;
class BarcodeDecoder;


struct ScanResult{

  cv::Mat preview {};         //video feed preview
  DecodeResult decoded {};    //tells if barcode decoded
  bool codeChanged = false;   //true for an accepted barcode presentation

};


class PosScanner{


    ImageSource& source_;
    const BarcodeDecoder& decoder_;     
    std::string lastCode_;          //last barcode scanned
    std::chrono::steady_clock::time_point noDecodeSince_{};
    unsigned noDecodePolls_ = 0;
    bool rearmed_ = false;
    bool running_;                  //scanner running or not

public:

    PosScanner(ImageSource& source, const BarcodeDecoder& decoder);

    ~PosScanner();

    Status start(int cameraIndex);

    Result<ScanResult> poll();

    void stop();
};
