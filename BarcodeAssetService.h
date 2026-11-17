#pragma once

#include "PosTypes.h"
#include <QImage>
#include <filesystem>

class BarcodeAssetService {
private:
    std::filesystem::path dataRoot_;

public:
    BarcodeAssetService(std::filesystem::path dataRoot);

    QImage preview(const std::string& barcode) const;
    Result<std::string> save(const std::string& barcode) const;
};