#include "ProductService.h"
#include "BarcodeAssetService.h"
#include "IdGenerator.h" // Khurram (P3) isay apni branch mein banayega, isliye is par red line aaye to ignore karein
#include <algorithm>

using namespace std;

/**
 * @brief Constructs the ProductService with references to shared storage, asset manager, and ID generator.
 */
ProductService::ProductService(IPosStore& store, BarcodeAssetService& images, const IdGenerator& ids)
    : store_(store), images_(images), ids_(ids) {
}

/**
 * @brief Retrieves the current catalogue of products from the storage.
 *
 * @return Result containing a vector of all products on success.
 */
Result<vector<Product>> ProductService::list() const {
    Result<vector<Product>> result;
    Result<StoreData> storeResult = store_.read();

    if (storeResult.success) {
        result.success = true;
        result.value = storeResult.value.products;
    }
    else {
        result.success = false;
        result.message = "Failed to read data from storage.";
    }
    return result;
}

/**
 * @brief Finds a specific product using its unique 6-digit barcode.
 */
Result<Product> ProductService::findByBarcode(const string& barcode) const {
    Result<Product> result;
    Result<StoreData> storeResult = store_.read();

    if (!storeResult.success) {
        result.message = "Storage error.";
        return result;
    }

    for (const auto& prod : storeResult.value.products) {
        if (prod.barcode == barcode) {
            result.success = true;
            result.value = prod;
            return result;
        }
    }

    result.message = "Product not found with the given barcode.";
    return result;
}

/**
 * @brief Finds a specific product using its internal system ID.
 */
Result<Product> ProductService::findById(const string& productId) const {
    Result<Product> result;
    Result<StoreData> storeResult = store_.read();

    if (!storeResult.success) {
        result.message = "Storage error.";
        return result;
    }

    for (const auto& prod : storeResult.value.products) {
        if (prod.id == productId) {
            result.success = true;
            result.value = prod;
            return result;
        }
    }

    result.message = "Product not found with the given ID.";
    return result;
}

/**
 * @brief Validates input, assigns a new ID, generates a barcode image, and saves the new product.
 */
Result<Product> ProductService::create(const Product& input) {
    Result<Product> result;
    Result<StoreData> storeResult = store_.read();

    if (!storeResult.success) {
        result.message = "Failed to access storage.";
        return result;
    }
    StoreData data = storeResult.value;

    // Generate internal product ID using Khurram's IdGenerator
    Result<string> idResult = ids_.productId(data);
    if (!idResult.success) {
        result.message = "Failed to generate product ID: " + idResult.message;
        return result;
    }

    // Generate and save the barcode PNG image
    Result<string> imageResult = images_.save(input.barcode);
    if (!imageResult.success) {
        result.message = "Failed to generate barcode image: " + imageResult.message;
        return result;
    }

    // Construct the final product
    Product newProduct = input;
    newProduct.id = idResult.value;                   // Assign new ID (ignores user input)
    newProduct.barcodeImagePath = imageResult.value;  // Assign newly generated image path

    // Add to storage collection and persist to disk
    data.products.push_back(newProduct);
    Status saveStatus = store_.save(data);

    if (saveStatus.success) {
        result.success = true;
        result.message = "Product created successfully.";
        result.value = newProduct;
    }
    else {
        result.message = "Failed to save product to storage: " + saveStatus.message;
    }

    return result;
}

/**
 * @brief Updates an existing product's details. Prevents changing ID, Barcode, and Image Path.
 */
Result<Product> ProductService::update(const Product& original, const Product& replacement) {
    Result<Product> result;
    Result<StoreData> storeResult = store_.read();

    if (!storeResult.success) {
        result.message = "Storage error.";
        return result;
    }
    StoreData data = storeResult.value;

    bool found = false;
    for (auto& p : data.products) {
        if (p.id == original.id) {
            // Apply updates but keep ID, barcode, and barcodeImagePath unchanged
            p.acronym = replacement.acronym;
            p.name = replacement.name;
            p.unit = replacement.unit;
            p.unitPrice = replacement.unitPrice;
            p.taxPerUnit = replacement.taxPerUnit;
            p.discountPerUnit = replacement.discountPerUnit;
            p.available = replacement.available;

            result.value = p;
            found = true;
            break;
        }
    }

    if (!found) {
        result.message = "Original product not found in database for update.";
        return result;
    }

    Status saveStatus = store_.save(data);
    if (saveStatus.success) {
        result.success = true;
        result.message = "Product updated successfully.";
    }
    else {
        result.message = "Failed to save updated product to storage.";
    }

    return result;
}

/**
 * @brief Deletes a product, but strictly blocks deletion if an 'OnHold' sale references it.
 */
Status ProductService::remove(const string& productId) {
    Status result;
    Result<StoreData> storeResult = store_.read();

    if (!storeResult.success) {
        result.message = "Storage error.";
        return result;
    }
    StoreData data = storeResult.value;

    // Rule: Reject deletion if an OnHold sale references the product
    for (const auto& sale : data.sales) {
        if (sale.status == OrderStatus::OnHold) {
            for (const auto& line : sale.lines) {
                if (line.productId == productId) {
                    result.message = "Cannot delete product: It is currently referenced by an OnHold sale.";
                    return result;
                }
            }
        }
    }

    // Safely remove the product if no restrictions exist
    auto it = std::remove_if(data.products.begin(), data.products.end(),
        [&](const Product& p) { return p.id == productId; });

    if (it != data.products.end()) {
        data.products.erase(it, data.products.end());
        result = store_.save(data);
    }
    else {
        result.message = "Product not found.";
    }

    return result;
}

/**
 * @brief Checks if the requested quantity is positive and within available stock limits.
 */
bool ProductService::hasStock(const Product& product, int64_t requested) const {
    return (requested > 0 && requested <= product.available);
}

/**
 * @brief Deducts stock quantities based on purchase lines. Updates the provided memory copy without writing to disk.
 */
Status ProductService::deductStock(vector<Product>& products, const vector<SaleLine>& lines) const {
    Status status;

    for (const auto& line : lines) {
        bool found = false;
        for (auto& prod : products) {
            if (prod.id == line.productId) {
                if (prod.available >= line.quantity) {
                    prod.available -= line.quantity;
                    found = true;
                    break;
                }
                else {
                    status.message = "Insufficient stock for product ID: " + line.productId;
                    return status;
                }
            }
        }
        if (!found) {
            status.message = "Product not found during stock deduction: " + line.productId;
            return status;
        }
    }

    status.success = true;
    status.message = "Stock deducted successfully.";
    return status;
}