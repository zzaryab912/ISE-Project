#ifndef BARCODEIMAGEIO_H
#define BARCODEIMAGEIO_H

#include <QImage>
#include <QString>

// Function prototypes
bool saveBarcodeImage(const QImage& image, const QString& filename);

QImage loadBarcodeImage(const QString& filename);

#endif
