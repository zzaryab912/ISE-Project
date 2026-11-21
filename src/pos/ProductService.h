#pragma once

#include "PosTypes.h"
#include "IPosStore.h"

// Forward declarations taake extra headers include na karne parein[cite: 22]
class BarcodeAssetService;
class IdGenerator;

class ProductService {
private:
    IPosStore& store_;
    BarcodeAssetService& images_;
    const IdGenerator& ids_;

public:
    ProductService(IPosStore& store, BarcodeAssetService& images, const IdGenerator& ids);

    // Const methods (sirf data read karte hain)[cite: 22]
    Result<std::vector<Product>> list() const;
    Result<Product> findByBarcode(const std::string& barcode) const;
    Result<Product> findById(const std::string& productId) const;
    bool hasStock(const Product& product, int64_t requested) const;
    Status deductStock(std::vector<Product>& products, const std::vector<SaleLine>& lines) const;

    // Non-const methods (data modify karte hain)[cite: 22]
    Result<Product> create(const Product& input);
    Result<Product> update(const Product& original, const Product& replacement);
    Status remove(const std::string& productId);
};