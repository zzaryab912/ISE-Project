#ifndef BARCODEPLOTTER_H
#define BARCODEPLOTTER_H

#include <opencv2/core.hpp>
#include <QImage>
#include <string>

using namespace std;


const int MODULE_WIDTH   = 2;
const int BARCODE_HEIGHT = 100;
const int TOP_MARGIN     = 10;
const int BOTTOM_MARGIN  = 10;
const int TEXT_GAP       = 8;
const int TEXT_PADDING   = 10;
const double FONT_SIZE   = 0.7;

// Pattern shape produced by P2
const int CODED_STRING_LENGTH = 121;
const int QUIET_MODULES       = 10;


// Function prototypes

void drawBarcode(cv::Mat& image, const string& codedStr, int x, int y);

void displayText(cv::Mat& image,const string& value,const cv::Rect& area);

cv::Mat codedString2Image(const string& codedStr,const string& value,bool showText=true);

// Convert OpenCV Mat to Qt QImage
QImage mat2QImage(const cv::Mat& mat);

#endif