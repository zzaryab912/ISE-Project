#include "BarcodeImageIO.h"

// ---------------------------------------------------------------------------
// Save barcode image
// ---------------------------------------------------------------------------
bool saveBarcodeImage(const QImage& image, const QString& filename)
{
    if (image.isNull() || filename.isEmpty())
        return false;

    return image.save(filename, "PNG");
}

// ---------------------------------------------------------------------------
// Load barcode image
// ---------------------------------------------------------------------------
QImage loadBarcodeImage(const QString& filename)
{
    if (filename.isEmpty())
        return QImage();

    QImage image;

    if (!image.load(filename) || image.isNull())
        return QImage();

    return image;       // if image loaded successfully, then make it the image object and return 
}
