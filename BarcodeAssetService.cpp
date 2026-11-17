#include "BarcodeAssetService.h"
#include "barcodeGenerator.h"    
#include "BarcodePlotter.h"      
#include "BarcodeImageIO.h"      
#include <QString>

using namespace std;

/**
 * @brief Initializes the BarcodeAssetService with the application's root data directory.
 *
 * @param dataRoot The filesystem path where generated barcode assets will be persisted.
 */
BarcodeAssetService::BarcodeAssetService(std::filesystem::path dataRoot)
    : dataRoot_(std::move(dataRoot)) {
}

/**
 * @brief Generates a QImage preview of the barcode for UI display.
 * Reuses the Increment 1 encoder and Qt drawing pipeline in memory.
 *
 * @param barcode The 6-digit barcode string to encode.
 * @return A valid QImage if generation succeeds, or a null QImage on failure.
 */
QImage BarcodeAssetService::preview(const string& barcode) const {
    // Generate the 121-bit binary pattern using the standard encoder
    string bits = generateBarcodeBits(barcode);

    // Return a null image immediately if input validation or encoding fails
    if (bits.empty()) {
        return QImage();
    }

    // Render the bitstream to a Qt image container
    return codedString2Image(bits, QString::fromStdString(barcode), true);
}

/**
 * @brief Generates and saves the barcode as a PNG file in the designated data directory.
 *
 * @param barcode The 6-digit barcode string.
 * @return A Result object containing the relative filename on success, or an error message on failure.
 */
Result<string> BarcodeAssetService::save(const string& barcode) const {
    Result<string> result;

    // Attempt to generate the in-memory image first
    QImage img = preview(barcode);
    if (img.isNull()) {
        result.success = false;
        result.message = "Failed to generate barcode image. Invalid input or encoding error.";
        return result;
    }

    // Construct the destination file path (e.g., "123456.png")
    string filename = barcode + ".png";
    std::filesystem::path fullPath = dataRoot_ / filename;

    // Persist the image to disk using the standard I/O module
    bool saved = saveBarcodeImage(img, QString::fromStdString(fullPath.string()));

    if (saved) {
        result.success = true;
        result.message = "Barcode saved successfully.";
        result.value = filename; // Return only the relative filename/path per specifications
    }
    else {
        result.success = false;
        result.message = "Failed to save barcode PNG file to disk.";
    }

    return result;
}