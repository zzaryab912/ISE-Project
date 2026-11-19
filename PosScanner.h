#pragma once
#include "PosTypes.h"
#include "ReaderContracts.h"
#include <string>

class ImageSource;
class BarcodeDecoder;


struct ScanResult{

  cv::Mat preview {};         //video feed preview
  DecodeResult decoded {};    //tells if barcode decoded
  bool codeChanged = false;   //true only if barcode different than lastCode ( last barcode string)

};


class PosScanner{


    ImageSource& source_;
    const BarcodeDecoder& decoder_;     
    std::string lastCode_;          //last barcode scanned
    bool running_;                  //scanner running or not

public:

    PosScanner(ImageSource& source, const BarcodeDecoder& decoder);

    ~PosScanner();

    Status start(int cameraIndex);

    Result<ScanResult> poll();

    void stop();
};
