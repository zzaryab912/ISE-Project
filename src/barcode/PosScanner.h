#pragma once
#include "PosTypes.h"
#include "ReaderContracts.h"
#include <chrono>
#include <string>

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
    std::chrono::steady_clock::time_point absentSince_{};
    unsigned absentPolls_ = 0;
    bool rearmed_ = false;
    bool running_ = false;          //scanner running or not

    // Require uninterrupted absence across multiple polls and elapsed wall time before re-arming.
    static constexpr unsigned kRearmAbsentPolls = 4;
    static constexpr std::chrono::milliseconds kRearmAbsentDuration{800};

    void resetAbsence();
    void resetSession();
    void evaluatePresentation(ImageStatus status,
                              std::chrono::steady_clock::time_point now,
                              ScanResult& result);

public:

    PosScanner(ImageSource& source, const BarcodeDecoder& decoder);

    ~PosScanner();

    Status start(int cameraIndex);

    Result<ScanResult> poll();

    void stop();
};
