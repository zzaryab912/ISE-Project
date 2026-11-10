#include "BarcodePlotter.h"

#include <QFont>
#include <QFontMetrics>
//-------------------------------------------------------------------


static QFont labelFont()
{
    QFont font;
    font.setPixelSize(FONT_SIZE);
    return font;
}

// ---------------------------------------------------------------------------
// Draw barcode modules
// ---------------------------------------------------------------------------
void drawBarcode(QPainter& painter, const string& codedStr, int x, int y)
{

    QPaintDevice* dev = painter.device();
    const int totalWidth = (int)(codedStr.size()) * MODULE_WIDTH;
    if (!dev || x < 0 || y < 0 ||
        x + totalWidth > dev->width() ||
        y + BARCODE_HEIGHT > dev->height())
        return;

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, false);
    painter.setPen(Qt::NoPen);
    painter.setBrush(Qt::black);

    const size_t n = codedStr.size();
    size_t i = 0;
    while (i < n)
    {
        if (codedStr[i] != '1')
        {
            i++;
            continue;
        }

        // Merge adjacent black modules into one big rectangle
        const size_t start = i;
        while (i < n && codedStr[i] == '1')
            i++;

        painter.fillRect(
            QRect(x + (int)(start) * MODULE_WIDTH,
                  y,
                (int)(i - start) * MODULE_WIDTH,
                  BARCODE_HEIGHT),
            Qt::black);
    }

    painter.restore();
}

// ---------------------------------------------------------------------------
//                  Draw human-readable text
// ---------------------------------------------------------------------------
void displayText(QPainter& painter, const QString& value, const QRect& area)
{
    if (!painter.isActive() || area.isEmpty())
        return;

    painter.save();
    painter.setFont(labelFont());
    painter.setPen(Qt::black);
    painter.drawText(area, Qt::AlignCenter, value);
    painter.restore();
}

// ---------------------------------------------------------------------------
//                  Create the complete image
// ---------------------------------------------------------------------------
QImage codedString2Image(const string& codedStr,
                         const QString& value,
                         bool showText)
{


    // 1. Dimensions. Quiet zones are already inside the pattern, so the
    //    image is exactly 121 * MODULE_WIDTH = 242 px wide (no extra margins).
    const int width = (int)(codedStr.size()) * MODULE_WIDTH;

    int textHeight = 0;
    if (showText)
        textHeight = QFontMetrics(labelFont()).height();

    int height = TOP_MARGIN + BARCODE_HEIGHT + BOTTOM_MARGIN;
    if (showText)
        height += TEXT_GAP + textHeight;

    // 2. Allocate + fill white
    QImage image(width, height, QImage::Format_RGB32);
    if (image.isNull())
        return QImage();
    image.fill(Qt::white);

    QPainter painter(&image);
    if (!painter.isActive())
        return QImage();

    drawBarcode(painter, codedStr, 0, TOP_MARGIN);

    // 3. Optional label centred under the bars
    if (showText)
    {
        QRect textArea(TEXT_PADDING,
                       TOP_MARGIN + BARCODE_HEIGHT + TEXT_GAP,
                       width - 2 * TEXT_PADDING,
                       textHeight);
        displayText(painter, value, textArea);
    }

    painter.end();
    return image;
}
