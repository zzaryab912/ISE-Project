#ifndef BARCODE_DECODER_H
#define BARCODE_DECODER_H

#include "ReaderContracts.h"
#include <opencv2/core/mat.hpp>
#include <string>
#include <vector>

using namespace std;

class BarcodeDecoder {
public:
    DecodeResult decode(const cv::Mat& barcode) const;

private:
    bool decodeLine(const cv::Mat& binary,
                    int row,
                    string& code,
                    bool& ambiguous) const;

    bool readCandidate(const vector<int>& widths,
                       string& code) const;
};

#endif
