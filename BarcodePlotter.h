#ifndef BARCODEPLOTTER_H
#define BARCODEPLOTTER_H

#include <QImage>
#include <QPainter>
#include <QRect>
#include <QString>
#include <string>

using namespace std;

// ---------------------------------------------------------------------------
//                    Configuration constants
// ---------------------------------------------------------------------------
const int MODULE_WIDTH   = 2;    // px per module
const int BARCODE_HEIGHT = 100;  // height of the black bars
const int TOP_MARGIN     = 10;   
const int BOTTOM_MARGIN  = 10;   
const int TEXT_GAP       = 8;    // gap between bars and label
const int TEXT_PADDING   = 10;   // horizontal inset of the label rectangle
const int FONT_SIZE      = 14;   

// Pattern shape produced by P2: 10 + 11 + 6*11 + 11 + 13 + 10
const int CODED_STRING_LENGTH = 121;
const int QUIET_MODULES       = 10;

// ---------------------------------------------------------------------------
//                   Function prototypes
// ---------------------------------------------------------------------------
void drawBarcode(QPainter& painter, const string& codedStr, int x, int y);

void displayText(QPainter& painter, const QString& value, const QRect& area);

// Returns a null QImage on any invalid argument or rendering failure.
QImage codedString2Image(const string& codedStr,
                         const QString& value,
                         bool showText =true);

#endif
