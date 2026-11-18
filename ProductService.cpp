#include "ProductService.h"
#include "BarcodeAssetService.h"
#include "IdGenerator.h"
#include "barcodeGenerator.h"
#include <algorithm>
#include <tuple>
#include <utility>

using namespace std;

namespace {

Status validateProduct(const Product& product) {
    if (!isValid6DigitInput(product.barcode)) {
        return {false, "Enter a six-digit barcode starting with a nonzero digit."};
    }
    if (product.acronym.size() > 8 ||
        product.acronym.find_first_not_of(" \t") == string::npos ||
        product.name.find_first_not_of(" \t") == string::npos ||
        product.acronym.find_first_of("\r\n") != string::npos ||
        product.name.find_first_of("\r\n") != string::npos) {
        return {false, "Enter a name and an acronym of up to eight characters on one line."};
    }
    if (product.unitPrice < 0 || product.taxPerUnit < 0 ||
        product.discountPerUnit < 0 || product.discountPerUnit > product.unitPrice) {
        return {false, "Enter nonnegative prices and tax, with discount no greater than price."};
    }
    if (product.available < 0 ||
        (product.unit != Unit::Count && product.unit != Unit::Weight) ||
        (product.unit == Unit::Count && product.available % 1000 != 0)) {
        return {false, "Enter valid stock for the selected unit. Count requires whole units."};
    }
    return {true, {}};
}

auto productFields(const Product& product) {
    return tie(product.id, product.barcode, product.acronym, product.name,
               product.unit, product.unitPrice, product.taxPerUnit,
               product.discountPerUnit, product.available, product.barcodeImagePath);
}

bool referencedByHeldSale(const StoreData& data, const string& productId) {
    for (const SaleRecord& sale : data.sales) {
        if (sale.status == OrderStatus::OnHold &&
            any_of(sale.lines.begin(), sale.lines.end(), [&](const SaleLine& line) {
                return line.productId == productId;
            })) {
            return true;
        }
    }
    return false;
}

} // namespace

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
    const Status valid = validateProduct(input);
    if (!valid.success) {
        return {false, valid.message, {}};
    }
    Result<Product> result;
    Result<StoreData> storeResult = store_.read();

    if (!storeResult.success) {
        result.message = "Failed to access storage.";
        return result;
    }
    StoreData& data = storeResult.value;
    if (any_of(data.products.begin(), data.products.end(), [&](const Product& product) {
        return product.barcode == input.barcode;
    })) {
        return {false, "A product already uses this barcode.", {}};
    }

    // Generate internal product ID using Khurram's IdGenerator
    Result<string> idResult = ids_.productId(data);
    if (!idResult.success) {
        result.message = "Failed to generate product ID: " + idResult.message;
        return result;
    }

    const auto imagePath = images_.imagePath(input.barcode);
    error_code imageError;
    const bool existingImage = filesystem::exists(imagePath, imageError);
    if (imageError) {
        return {false, "Could not check the barcode image destination.", {}};
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
        // Keep pre-existing assets and any image the store may still reference.
        const auto current = store_.read();
        if (!existingImage && current.success &&
            none_of(current.value.products.begin(), current.value.products.end(),
                [&](const Product& product) { return product.barcodeImagePath == imageResult.value; })) {
            filesystem::remove(imagePath, imageError);
            if (imageError) {
                result.message += " Could not remove the unused barcode image.";
            }
        }
    }

    return result;
}

/**
 * @brief Updates an existing product's details. Prevents changing ID, Barcode, and Image Path.
 */
Result<Product> ProductService::update(const Product& original, const Product& replacement) {
    auto storeResult = store_.read();
    if (!storeResult.success) {
        return {false, storeResult.message, {}};
    }
    StoreData& data = storeResult.value;
    const auto current = find_if(data.products.begin(), data.products.end(),
        [&](const Product& product) { return product.id == original.id; });
    if (current == data.products.end()) {
        return {false, "The product no longer exists.", {}};
    }
    if (productFields(*current) != productFields(original)) {
        return {false, "The product changed. Reload it before saving edits.", {}};
    }
    if (current->unit != replacement.unit && referencedByHeldSale(data, current->id)) {
        return {false, "The unit cannot change while a held bill uses this product.", {}};
    }
    Product updated = replacement;
    updated.id = current->id;
    updated.barcode = current->barcode;
    updated.barcodeImagePath = current->barcodeImagePath;
    const Status valid = validateProduct(updated);
    if (!valid.success) {
        return {false, valid.message, {}};
    }
    *current = updated;
    const Status saved = store_.save(data);
    return saved.success ? Result<Product>{true, {}, std::move(updated)}
                         : Result<Product>{false, saved.message, {}};
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
    if (referencedByHeldSale(data, productId)) {
        return {false, "The product cannot be deleted while a held bill uses it."};
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
    return requested > 0 && requested <= product.available &&
           (product.unit == Unit::Weight ||
            (product.unit == Unit::Count && requested % 1000 == 0));
}

/**
 * @brief Deducts stock quantities based on purchase lines. Updates the provided memory copy without writing to disk.
 */
Status ProductService::deductStock(vector<Product>& products, const vector<SaleLine>& lines) const {
    auto updated = products;
    for (const auto& line : lines) {
        const auto product = find_if(updated.begin(), updated.end(),
            [&](const Product& current) { return current.id == line.productId; });
        if (product == updated.end() || product->barcode != line.barcode) {
            return {false, "Product " + line.productId + " no longer matches the catalogue."};
        }
        if (!hasStock(*product, line.quantity)) {
            return {false, "Invalid quantity or insufficient stock for product " + line.productId + "."};
        }
        product->available -= line.quantity;
    }
    products.swap(updated);
    return {true, {}};
}
