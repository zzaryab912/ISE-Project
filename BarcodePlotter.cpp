#include "BarcodePlotter.h"

#include <opencv2/imgproc.hpp>


// Draw barcode modules

void drawBarcode(cv::Mat& image, const string& codedStr, int x, int y)
{
    if (image.empty())
        return;

    const int totalWidth = (int)(codedStr.size()) * MODULE_WIDTH;

    if (x < 0 || y < 0 ||
        x + totalWidth > image.cols ||
        y + BARCODE_HEIGHT > image.rows)
        return;

    int i = 0;
    const int n = (int)(codedStr.size());

    while (i < n)
    {
        if (codedStr[i] != '1')
        {
            i++;
            continue;
        }

        // Merge adjacent black modules into one big rectangle
        const int start = i;

        while (i < n && codedStr[i] == '1')
            i++;

        cv::rectangle(
            image,
            cv::Rect(
                x + start * MODULE_WIDTH,
                y,
                (i - start) * MODULE_WIDTH,
                BARCODE_HEIGHT
            ),
            cv::Scalar(0, 0, 0),
            cv::FILLED
        );
    }
}

//    Draw human-readable text

void displayText(cv::Mat& image, const string& value, const cv::Rect& area)
{
    if (image.empty() || area.empty())
        return;

    int baseline = 0;

    cv::Size textSize = cv::getTextSize(
        value,
        cv::FONT_HERSHEY_SIMPLEX,
        FONT_SIZE,
        1,
        &baseline
    );

    const int textX = area.x + (area.width - textSize.width) / 2;
    const int textY = area.y + (area.height + textSize.height - baseline) / 2;

    cv::putText(
        image,
        value,
        cv::Point(textX, textY),
        cv::FONT_HERSHEY_SIMPLEX,
        FONT_SIZE,
        cv::Scalar(0, 0, 0),
        1,
        cv::LINE_AA
    );
}


//       Create the complete image
cv::Mat codedString2Image(const string& codedStr,const string& value,bool showText)
{
    // 1. Validate coded string length
    if ((int)(codedStr.size()) != CODED_STRING_LENGTH)
        return cv::Mat();

    // 2. Validate binary characters
    for (int i = 0; i < CODED_STRING_LENGTH; i++)
    {
        if (codedStr[i] != '0' && codedStr[i] != '1')
            return cv::Mat();
    }

    // 3. Validate leading and trailing quiet zones
    for (int i = 0; i < QUIET_MODULES; i++)
    {
        if (codedStr[i] != '0' ||
            codedStr[CODED_STRING_LENGTH - 1 - i] != '0')
            return cv::Mat();
    }

    // 4. Validate human-readable value if text is enabled
    if (showText)
    {
        if ((int)(value.size()) != 6)
            return cv::Mat();

        for (int i = 0; i < 6; i++)
        {
            if (value[i] < '0' || value[i] > '9')
                return cv::Mat();
        }
    }

    // 5. Calculate dimensions
    const int width = (int)(codedStr.size()) * MODULE_WIDTH;

    int textHeight = 0;

    if (showText)
    {
        int baseline = 0;

        textHeight = cv::getTextSize(
            value,
            cv::FONT_HERSHEY_SIMPLEX,
            FONT_SIZE,
            1,
            &baseline
        ).height + baseline;
    }

    int height = TOP_MARGIN + BARCODE_HEIGHT + BOTTOM_MARGIN;

    if (showText)
        height += TEXT_GAP + textHeight;

    // 6. Allocate and fill image with white
    cv::Mat image(
        height,
        width,
        CV_8UC3,
        cv::Scalar(255, 255, 255)
    );

    // 7. Draw barcode
    drawBarcode(image, codedStr, 0, TOP_MARGIN);

    // 8. Optional label centred under the bars
    if (showText)
    {
        cv::Rect textArea(
            TEXT_PADDING,
            TOP_MARGIN + BARCODE_HEIGHT + TEXT_GAP,
            width - 2 * TEXT_PADDING,
            textHeight
        );

        displayText(image, value, textArea);
    }

    return image;
}

//   Convert OpenCV Mat to Qt QImage

QImage mat2QImage(const cv::Mat& mat)
{
    if (mat.empty() || mat.type() != CV_8UC3)
        return QImage();

    QImage image(
        mat.data,
        mat.cols,
        mat.rows,
        (int)(mat.step),
        QImage::Format_BGR888
    );

    return image.copy();
}