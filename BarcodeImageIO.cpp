#include "BarcodeImageIO.h"
#include <QSaveFile>

// ---------------------------------------------------------------------------
// Save barcode image
// ---------------------------------------------------------------------------
bool saveBarcodeImage(const QImage& image, const QString& filename)
{
    if (image.isNull() || filename.isEmpty())
        return false;

    QSaveFile file(filename);
    return file.open(QIODevice::WriteOnly) && image.save(&file, "PNG") && file.commit();
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

    return image;
}
