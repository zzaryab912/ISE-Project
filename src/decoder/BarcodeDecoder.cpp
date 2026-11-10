#include "BarcodeDecoder.h"
#include "../generator/barcodeGenerator.h"

#include <opencv2/imgproc.hpp>

using namespace std;
DecodeResult BarcodeDecoder::decode(const cv::Mat& barcode) const
{
    //checks if its empty 
    if (barcode.empty() ||
        barcode.dims != 2 || // checks it must be 2D
        barcode.type() != CV_8UC1) {
        return {false, "Invalid barcode image.", ""};
    }
    //so basically it rejects any other image type sirf 8 bit grayscale image



    //8192 is our maximum allowed width or height for processing. 
    //We picked it as a practical safety limit
    //it has no special meaning in the barcode format
 constexpr int MAX_SIDE = 8192;

constexpr size_t MAX_PIXELS = 16'000'000;
//pixel limit
if (barcode.cols > MAX_SIDE ||
    barcode.rows > MAX_SIDE ||
    barcode.total() > MAX_PIXELS) {
    return {
        false,
        "Barcode image is too large. Please provide a smaller crop.",
        ""
    };
}



    // This implementation requires at least one pixel per module.
    if (barcode.cols < 121) {
        return {
            false,
            "Barcode image is too narrow to read reliably.",
            ""
        };

        //too narrow image
    }

    try {
        cv::Mat binary;

        cv::threshold(
            barcode,
            binary,
            0, //0 being black 
            255, //255 being white
            cv::THRESH_BINARY | cv::THRESH_OTSU
        );
        //opencv chooses brightness cut off
        //thresh binary uses the cut off

        string acceptedCode;//place to hold the answer
        const int rowCount = min(binary.rows, 31); 
        //max it will examine rows 
 

    // Try horizontal lines spread from top to bottom then repeat

        for (int direction = 0; direction < 2; ++direction) {
            for (int index = 0; index < rowCount; ++index) {
                int row = 0;
                

                if (rowCount > 1) {
                    row = index * (binary.rows - 1) / (rowCount - 1);
                }
      


                string rowCode;
                bool ambiguous = false;

                const bool found = decodeLine(
                    binary, row, rowCode, ambiguous
                );
                //decode the row

                if (ambiguous ||
                    (found &&
                     !acceptedCode.empty() &&
                     acceptedCode != rowCode)) {
                    return {
                        false,
                        "Conflicting barcode readings. Please try again.",
                        ""
                    };
                }
                //same row producing valid codes or two diff rows two diff valid codes

                if (found) {
                    acceptedCode = rowCode;
                }
                //store and remember valid code
            }

            if (direction == 0) {
                cv::flip(binary, binary, 1);
            }
        }
        //flip before second pass

        if (!acceptedCode.empty()) {
            return {
                true,
                "Barcode read successfully.",
                acceptedCode
            };
            //return correct code
        }

        return {
            false,
            "Could not read the barcode. Please try again.",
            ""
        };
    }
    catch (const exception&) {
        return {
            false,
            "Barcode processing failed. Please try again.",
            ""
        };
        // catch the error it throws incase of opencv or c++ error
    }
}