#include "ProductService.h"
#include "IPosStore.h"
#include "BarcodeAssetService.h"
#include "IdGenerator.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <iomanip>
#include <limits>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

using namespace std;

namespace {

    string trim(const string& text) {
        size_t start = 0;
        while (start < text.size() && isspace(static_cast<unsigned char>(text[start]))) {
            ++start;
        }
        size_t end = text.size();
        while (end > start && isspace(static_cast<unsigned char>(text[end - 1]))) {
            --end;
        }
        return text.substr(start, end - start);
    }

    string toLower(const string& text) {
        string lowered = text;
        transform(lowered.begin(), lowered.end(), lowered.begin(),
            [](unsigned char ch) { return static_cast<char>(tolower(ch)); });
        return lowered;
    }

    bool isValidCategoryId(const string& id) {
        if (id.size() != 2 || !isdigit(static_cast<unsigned char>(id[0])) ||
            !isdigit(static_cast<unsigned char>(id[1]))) {
            return false;
        }
        int numeric = stoi(id);
        return numeric >= 10 && numeric <= 99;
    }

    Result<string> allocateCategoryId(const StoreData& data) {
        unordered_set<string> usedIds;
        for (const auto& cat : data.categories) {
            usedIds.insert(cat.id);
        }

        for (int id = 10; id <= 99; ++id) {
            string candidate = to_string(id);
            if (usedIds.find(candidate) == usedIds.end()) {
                return { true, {}, candidate };
            }
        }
        return { false, "No category IDs remaining (10-99 exhausted).", {} };
    }

    Result<string> allocateBarcode(const StoreData& data, const string& categoryId) {
        if (!isValidCategoryId(categoryId)) {
            return { false, "Invalid category ID for barcode allocation.", {} };
        }

        unordered_set<string> issued(data.issuedBarcodes.begin(), data.issuedBarcodes.end());
        for (const auto& prod : data.products) {
            if (!prod.barcode.empty()) {
                issued.insert(prod.barcode);
            }
        }

        for (int suffix = 1; suffix <= 9999; ++suffix) {
            ostringstream oss;
            oss << categoryId << setw(4) << setfill('0') << suffix;
            string candidate = oss.str();
            if (issued.find(candidate) == issued.end()) {
                return { true, {}, candidate };
            }
        }
        return { false, "Barcode suffix space exhausted for category " + categoryId + ".", {} };
    }

} // namespace

/**
 * @brief Constructs the ProductService with references to shared storage, asset manager, and ID generator.
 */
ProductService::ProductService(IPosStore& store, BarcodeAssetService& images, const IdGenerator& ids)
    : store_(store), images_(images), ids_(ids) {
}

/**
 * @brief Retrieves all products currently in the catalogue.
 */
Result<vector<Product>> ProductService::list() const {
    Result<StoreData> storeResult = store_.read();
    if (!storeResult.success) {
        return { false, storeResult.message.empty() ? "Failed to read storage." : storeResult.message, {} };
    }
    return { true, {}, storeResult.value.products };
}

/**
 * @brief Retrieves all product categories.
 */
Result<vector<Category>> ProductService::listCategories() const {
    Result<StoreData> storeResult = store_.read();
    if (!storeResult.success) {
        return { false, storeResult.message.empty() ? "Failed to read storage." : storeResult.message, {} };
    }
    return { true, {}, storeResult.value.categories };
}

/**
 * @brief Finds a live product by its internal system ID.
 */
Result<Product> ProductService::findById(const string& productId) const {
    Result<StoreData> storeResult = store_.read();
    if (!storeResult.success) {
        return { false, storeResult.message.empty() ? "Storage error." : storeResult.message, {} };
    }

    for (const auto& prod : storeResult.value.products) {
        if (prod.id == productId) {
            return { true, {}, prod };
        }
    }
    return { false, "Product not found with the given ID.", {} };
}

/**
 * @brief Finds a live product by its active 6-digit barcode.
 */
Result<Product> ProductService::findByBarcode(const string& barcode) const {
    Result<StoreData> storeResult = store_.read();
    if (!storeResult.success) {
        return { false, storeResult.message.empty() ? "Storage error." : storeResult.message, {} };
    }

    for (const auto& prod : storeResult.value.products) {
        if (prod.barcode == barcode) {
            return { true, {}, prod };
        }
    }
    return { false, "Product not found with the given barcode.", {} };
}

/**
 * @brief Creates a new category (when original is empty) or updates an existing one.
 * Enforces unique case-insensitive names and acronyms, non-empty values, and valid IDs (10-99).
 */
Result<Category> ProductService::saveCategory(const optional<Category>& original, const Category& input) {
    string cleanName = trim(input.name);
    string cleanAcronym = trim(input.acronym);

    if (cleanName.empty() || cleanAcronym.empty()) {
        return { false, "Category name and acronym must not be empty.", {} };
    }

    Result<StoreData> storeResult = store_.read();
    if (!storeResult.success) {
        return { false, storeResult.message.empty() ? "Failed to read storage." : storeResult.message, {} };
    }
    StoreData data = storeResult.value;

    string lowerName = toLower(cleanName);
    string lowerAcronym = toLower(cleanAcronym);

    if (!original.has_value()) {
        // Creating a new category
        for (const auto& existing : data.categories) {
            if (toLower(existing.name) == lowerName) {
                return { false, "A category with this name already exists.", {} };
            }
            if (toLower(existing.acronym) == lowerAcronym) {
                return { false, "A category with this acronym already exists.", {} };
            }
        }

        Result<string> idResult = allocateCategoryId(data);
        if (!idResult.success) {
            return { false, idResult.message, {} };
        }

        Category created{ idResult.value, cleanName, cleanAcronym };
        data.categories.push_back(created);

        Status saveStatus = store_.save(data);
        if (!saveStatus.success) {
            return { false, saveStatus.message.empty() ? "Failed to save category." : saveStatus.message, {} };
        }
        return { true, "Category created successfully.", created };
    }

    // Updating an existing category
    if (!isValidCategoryId(original->id)) {
        return { false, "Invalid original category ID.", {} };
    }

    auto it = find_if(data.categories.begin(), data.categories.end(),
        [&](const Category& c) { return c.id == original->id; });
    if (it == data.categories.end()) {
        return { false, "Original category not found.", {} };
    }
    if (it->name != original->name || it->acronym != original->acronym) {
        return { false, "Category was modified by another operation. Please refresh and try again.", {} };
    }

    for (const auto& existing : data.categories) {
        if (existing.id == original->id) {
            continue;
        }
        if (toLower(existing.name) == lowerName) {
            return { false, "Another category with this name already exists.", {} };
        }
        if (toLower(existing.acronym) == lowerAcronym) {
            return { false, "Another category with this acronym already exists.", {} };
        }
    }

    it->name = cleanName;
    it->acronym = cleanAcronym;
    Category updated = *it;

    Status saveStatus = store_.save(data);
    if (!saveStatus.success) {
        return { false, saveStatus.message.empty() ? "Failed to save updated category." : saveStatus.message, {} };
    }
    return { true, "Category updated successfully.", updated };
}

/**
 * @brief Removes a category if it exists and has no referring products or issued barcodes.
 */
Status ProductService::removeCategory(const string& categoryId) {
    if (!isValidCategoryId(categoryId)) {
        return { false, "Invalid category ID." };
    }

    Result<StoreData> storeResult = store_.read();
    if (!storeResult.success) {
        return { false, storeResult.message.empty() ? "Failed to read storage." : storeResult.message };
    }
    StoreData data = storeResult.value;

    auto it = find_if(data.categories.begin(), data.categories.end(),
        [&](const Category& c) { return c.id == categoryId; });
    if (it == data.categories.end()) {
        return { false, "Category not found." };
    }

    for (const auto& prod : data.products) {
        if (prod.categoryId == categoryId) {
            return { false, "Cannot delete category: one or more products belong to it." };
        }
    }

    for (const auto& code : data.issuedBarcodes) {
        if (code.size() >= 2 && code.substr(0, 2) == categoryId) {
            return { false, "Cannot delete category: barcodes have already been issued under this prefix." };
        }
    }

    data.categories.erase(it);
    Status saveStatus = store_.save(data);
    if (!saveStatus.success) {
        return { false, saveStatus.message.empty() ? "Failed to remove category." : saveStatus.message };
    }
    return { true, "Category removed successfully." };
}

/**
 * @brief Generates a preview barcode image for the given category without issuing the code.
 */
Result<QImage> ProductService::previewBarcode(const string& categoryId) const {
    if (!isValidCategoryId(categoryId)) {
        return { false, "Invalid category ID.", {} };
    }

    Result<StoreData> storeResult = store_.read();
    if (!storeResult.success) {
        return { false, storeResult.message.empty() ? "Failed to read storage." : storeResult.message, {} };
    }

    const auto& categories = storeResult.value.categories;
    bool categoryExists = any_of(categories.begin(), categories.end(),
        [&](const Category& c) { return c.id == categoryId; });
    if (!categoryExists) {
        return { false, "Category does not exist.", {} };
    }

    Result<string> codeResult = allocateBarcode(storeResult.value, categoryId);
    if (!codeResult.success) {
        return { false, codeResult.message, {} };
    }

    QImage img = images_.preview(codeResult.value);
    if (img.isNull()) {
        return { false, "Failed to render barcode preview image.", {} };
    }
    return { true, {}, img };
}

/**
 * @brief Computes physical (onHand), reserved, and available stock from one StoreData snapshot.
 * Active reservations come from unarchived OnHold orders. Excluding a held order ID allows
 * a resumed hold to spend its own reservation.
 */
Result<StockPosition> ProductService::stockPosition(
    const StoreData& data,
    const string& productId,
    const string& excludeHeldOrderId) const {

    auto prodIt = find_if(data.products.begin(), data.products.end(),
        [&](const Product& p) { return p.id == productId; });
    if (prodIt == data.products.end()) {
        return { false, "Product not found in catalogue.", {} };
    }

    int64_t reserved = 0;
    for (const auto& sale : data.sales) {
        if (sale.archived || sale.status != OrderStatus::OnHold) {
            continue;
        }
        if (!excludeHeldOrderId.empty() && sale.ids.orderId == excludeHeldOrderId) {
            continue;
        }
        for (const auto& line : sale.lines) {
            if (line.productId == productId && line.quantity > 0) {
                reserved += line.quantity;
            }
        }
    }

    StockPosition pos;
    pos.onHand = prodIt->onHand;
    pos.reserved = reserved;
    pos.available = max<int64_t>(0, prodIt->onHand - reserved);
    return { true, {}, pos };
}

/**
 * @brief Lists all products along with their computed stock positions from a single snapshot.
 */
Result<vector<ProductStock>> ProductService::listWithStock() const {
    Result<StoreData> storeResult = store_.read();
    if (!storeResult.success) {
        return { false, storeResult.message.empty() ? "Failed to read storage." : storeResult.message, {} };
    }

    vector<ProductStock> resultList;
    resultList.reserve(storeResult.value.products.size());

    for (const auto& prod : storeResult.value.products) {
        Result<StockPosition> posResult = stockPosition(storeResult.value, prod.id, "");
        if (!posResult.success) {
            return { false, posResult.message, {} };
        }
        resultList.push_back({ prod, posResult.value });
    }
    return { true, {}, resultList };
}

/**
 * @brief Resolves a product by ID with live stock, optionally excluding a held order's own reservation.
 */
Result<ProductStock> ProductService::findWithStock(
    const string& productId,
    const string& excludeHeldOrderId) const {

    Result<StoreData> storeResult = store_.read();
    if (!storeResult.success) {
        return { false, storeResult.message.empty() ? "Failed to read storage." : storeResult.message, {} };
    }

    auto prodIt = find_if(storeResult.value.products.begin(), storeResult.value.products.end(),
        [&](const Product& p) { return p.id == productId; });
    if (prodIt == storeResult.value.products.end()) {
        return { false, "Product not found with the given ID.", {} };
    }

    Result<StockPosition> posResult = stockPosition(storeResult.value, prodIt->id, excludeHeldOrderId);
    if (!posResult.success) {
        return { false, posResult.message, {} };
    }
    return { true, {}, {*prodIt, posResult.value} };
}

/**
 * @brief Resolves a product by its live barcode with stock, optionally excluding a held order's reservation.
 */
Result<ProductStock> ProductService::findWithStockByBarcode(
    const string& barcode,
    const string& excludeHeldOrderId) const {

    Result<StoreData> storeResult = store_.read();
    if (!storeResult.success) {
        return { false, storeResult.message.empty() ? "Failed to read storage." : storeResult.message, {} };
    }

    auto prodIt = find_if(storeResult.value.products.begin(), storeResult.value.products.end(),
        [&](const Product& p) { return p.barcode == barcode; });
    if (prodIt == storeResult.value.products.end()) {
        return { false, "Product not found with the given barcode.", {} };
    }

    Result<StockPosition> posResult = stockPosition(storeResult.value, prodIt->id, excludeHeldOrderId);
    if (!posResult.success) {
        return { false, posResult.message, {} };
    }
    return { true, {}, {*prodIt, posResult.value} };
}

/**
 * @brief Checks all requested sale lines against live stock and returns any shortages.
 * Aggregates duplicate product lines and produces a named shortage with zero available
 * for missing products or changed units.
 */
Result<vector<StockShortage>> ProductService::shortages(
    const StoreData& data,
    const vector<SaleLine>& requested,
    const string& excludeHeldOrderId) const {

    vector<StockShortage> shortageList;
    unordered_map<string, int64_t> aggregatedQty;
    unordered_map<string, const SaleLine*> firstLine;
    vector<string> order;

    for (const auto& line : requested) {
        if (line.quantity <= 0) {
            continue;
        }
        if (aggregatedQty.find(line.productId) == aggregatedQty.end()) {
            order.push_back(line.productId);
            firstLine[line.productId] = &line;
        }
        if (aggregatedQty[line.productId] > numeric_limits<int64_t>::max() - line.quantity) {
            return { false, "Requested quantity overflows supported limit for product ID: " + line.productId, {} };
        }
        aggregatedQty[line.productId] += line.quantity;
    }

    for (const auto& prodId : order) {
        const SaleLine& line = *firstLine[prodId];
        int64_t totalRequired = aggregatedQty[prodId];

        auto prodIt = find_if(data.products.begin(), data.products.end(),
            [&](const Product& p) { return p.id == prodId; });

        if (prodIt == data.products.end() || prodIt->unit != line.unit) {
            string displayName = !line.name.empty() ? line.name : line.acronym;
            shortageList.push_back({ prodId, displayName, line.unit, totalRequired, 0 });
            continue;
        }

        Result<StockPosition> posResult = stockPosition(data, prodIt->id, excludeHeldOrderId);
        int64_t available = posResult.success ? posResult.value.available : 0;

        if (available < totalRequired) {
            shortageList.push_back({ prodIt->id, prodIt->name, prodIt->unit, totalRequired, available });
        }
    }

    return { true, {}, shortageList };
}

/**
 * @brief Validates and deducts physical stock in the supplied snapshot without writing to disk.
 * Leaves the snapshot unchanged if any line fails validation or has a shortage.
 */
Status ProductService::deductStock(
    StoreData& data,
    const vector<SaleLine>& lines,
    const string& excludeHeldOrderId) const {

    for (const auto& line : lines) {
        if (line.quantity < 0) {
            return { false, "Quantity cannot be negative for product ID: " + line.productId };
        }
        if (line.unit == Unit::Count && (line.quantity % 1000 != 0)) {
            return { false, "Count product quantity must be a whole number for product ID: " + line.productId };
        }
    }

    Result<vector<StockShortage>> check = shortages(data, lines, excludeHeldOrderId);
    if (!check.success) {
        return { false, check.message };
    }
    if (!check.value.empty()) {
        return { false, "Insufficient stock for product: " + check.value.front().name };
    }

    // All checks passed; apply physical stock deductions
    for (const auto& line : lines) {
        if (line.quantity == 0) {
            continue;
        }
        auto prodIt = find_if(data.products.begin(), data.products.end(),
            [&](const Product& p) { return p.id == line.productId; });
        if (prodIt != data.products.end()) {
            prodIt->onHand -= line.quantity;
        }
    }

    return { true, "Stock deducted successfully." };
}

/**
 * @brief Applies net stock increases or restockable decreases for an approved paid bill edit.
 * Validates the entire batch before mutating any product in the snapshot.
 */
Status ProductService::applyPaidStockDifference(
    StoreData& data,
    const vector<SaleLine>& before,
    const vector<QuantityChange>& changes) const {

    struct PendingDelta {
        string productId;
        int64_t onHandDelta; // negative for additional deduction, positive for restock
    };

    vector<PendingDelta> pending;
    unordered_set<string> seenIds;

    for (const auto& change : changes) {
        if (!seenIds.insert(change.productId).second) {
            return { false, "Duplicate product ID in paid edit changes: " + change.productId };
        }
        if (change.quantity < 0 || change.restockQuantity < 0) {
            return { false, "Quantities and restock values must be non-negative." };
        }

        auto beforeIt = find_if(before.begin(), before.end(),
            [&](const SaleLine& l) { return l.productId == change.productId; });
        if (beforeIt == before.end()) {
            return { false, "Edited product does not exist on the original bill: " + change.productId };
        }

        if (beforeIt->unit == Unit::Count &&
            (change.quantity % 1000 != 0 || change.restockQuantity % 1000 != 0)) {
            return { false, "Count quantities must be whole numbers for product ID: " + change.productId };
        }

        int64_t delta = change.quantity - beforeIt->quantity;

        if (delta > 0) {
            if (change.restockQuantity != 0) {
                return { false, "Restock quantity must be zero when increasing a paid line." };
            }

            auto prodIt = find_if(data.products.begin(), data.products.end(),
                [&](const Product& p) { return p.id == change.productId; });
            if (prodIt == data.products.end() || prodIt->unit != beforeIt->unit) {
                return { false, "Cannot increase quantity: product is missing or its unit has changed." };
            }

            Result<StockPosition> posResult = stockPosition(data, prodIt->id, "");
            if (!posResult.success || posResult.value.available < delta) {
                return { false, "Insufficient sellable stock to increase quantity for: " + prodIt->name };
            }

            pending.push_back({ prodIt->id, -delta });
        }
        else if (delta < 0) {
            int64_t decrease = -delta;
            if (change.restockQuantity > decrease) {
                return { false, "Restock quantity cannot exceed the reduced quantity." };
            }

            if (change.restockQuantity > 0) {
                auto prodIt = find_if(data.products.begin(), data.products.end(),
                    [&](const Product& p) { return p.id == change.productId; });
                if (prodIt == data.products.end() || prodIt->unit != beforeIt->unit) {
                    return { false, "Cannot restock: product is missing or its unit has changed." };
                }
                if (prodIt->onHand > numeric_limits<int64_t>::max() - change.restockQuantity) {
                    return { false, "Cannot restock: physical stock would overflow for product: " + prodIt->name };
                }
                pending.push_back({ prodIt->id, change.restockQuantity });
            }
        }
        else {
            if (change.restockQuantity != 0) {
                return { false, "Restock quantity must be zero when line quantity is unchanged." };
            }
        }
    }

    // All validations succeeded; apply mutations to snapshot
    for (const auto& op : pending) {
        auto prodIt = find_if(data.products.begin(), data.products.end(),
            [&](const Product& p) { return p.id == op.productId; });
        if (prodIt != data.products.end()) {
            prodIt->onHand += op.onHandDelta;
        }
    }

    return { true, "Paid stock adjustments applied successfully." };
}

/**
 * @brief Validates product fields, assigns a new ID and category-prefixed barcode,
 *        generates the PNG asset, and persists the product and issued barcode together.
 *        Cleans up the newly created PNG if storage save fails.
 */
Result<Product> ProductService::create(const Product& input) {
    string cleanName = trim(input.name);
    string cleanAcronym = trim(input.acronym);

    if (cleanName.empty() || cleanAcronym.empty()) {
        return { false, "Product name and acronym must not be empty.", {} };
    }
    if (!isValidCategoryId(input.categoryId)) {
        return { false, "A valid category ID (10-99) is required.", {} };
    }
    if (input.unitPrice < 0 || input.discountPerUnit < 0 || input.discountPerUnit > input.unitPrice) {
        return { false, "Invalid unit price or discount per unit.", {} };
    }
    if (input.taxRate < 0 || input.taxRate > 10000) {
        return { false, "Tax rate must be between 0 and 10000 basis points (0.00% - 100.00%).", {} };
    }
    if (input.onHand < 0) {
        return { false, "On-hand stock cannot be negative.", {} };
    }
    if (input.unit == Unit::Count && (input.onHand % 1000 != 0)) {
        return { false, "Count product on-hand stock must be a whole number.", {} };
    }

    Result<StoreData> storeResult = store_.read();
    if (!storeResult.success) {
        return { false, storeResult.message.empty() ? "Failed to read storage." : storeResult.message, {} };
    }
    StoreData data = storeResult.value;

    bool categoryExists = any_of(data.categories.begin(), data.categories.end(),
        [&](const Category& c) { return c.id == input.categoryId; });
    if (!categoryExists) {
        return { false, "Selected category does not exist.", {} };
    }

    Result<string> idResult = ids_.productId(data);
    if (!idResult.success) {
        return { false, "Failed to generate product ID: " + idResult.message, {} };
    }

    Result<string> barcodeResult = allocateBarcode(data, input.categoryId);
    if (!barcodeResult.success) {
        return { false, barcodeResult.message, {} };
    }

    Result<string> imageResult = images_.save(barcodeResult.value);
    if (!imageResult.success) {
        return { false, "Failed to generate barcode PNG: " + imageResult.message, {} };
    }

    Product created = input;
    created.id = idResult.value;
    created.barcode = barcodeResult.value;
    created.name = cleanName;
    created.acronym = cleanAcronym;
    created.barcodeImagePath = imageResult.value;

    data.products.push_back(created);
    data.issuedBarcodes.push_back(created.barcode);

    Status saveStatus = store_.save(data);
    if (!saveStatus.success) {
        error_code ec;
        filesystem::remove(images_.imagePath(created.barcode), ec);
        return { false, saveStatus.message.empty() ? "Failed to save product to storage." : saveStatus.message, {} };
    }

    return { true, "Product created successfully.", created };
}

/**
 * @brief Updates an existing product after verifying the original snapshot.
 *        Preserves product ID, allocates a new barcode only on category reassignment,
 *        and blocks reducing on-hand stock below reservations or changing unit on active holds.
 */
Result<Product> ProductService::update(const Product& original, const Product& replacement) {
    string cleanName = trim(replacement.name);
    string cleanAcronym = trim(replacement.acronym);

    if (cleanName.empty() || cleanAcronym.empty()) {
        return { false, "Product name and acronym must not be empty.", {} };
    }
    if (!isValidCategoryId(replacement.categoryId)) {
        return { false, "A valid category ID (10-99) is required.", {} };
    }
    if (replacement.unitPrice < 0 || replacement.discountPerUnit < 0 ||
        replacement.discountPerUnit > replacement.unitPrice) {
        return { false, "Invalid unit price or discount per unit.", {} };
    }
    if (replacement.taxRate < 0 || replacement.taxRate > 10000) {
        return { false, "Tax rate must be between 0 and 10000 basis points (0.00% - 100.00%).", {} };
    }
    if (replacement.onHand < 0) {
        return { false, "On-hand stock cannot be negative.", {} };
    }
    if (replacement.unit == Unit::Count && (replacement.onHand % 1000 != 0)) {
        return { false, "Count product on-hand stock must be a whole number.", {} };
    }

    Result<StoreData> storeResult = store_.read();
    if (!storeResult.success) {
        return { false, storeResult.message.empty() ? "Failed to read storage." : storeResult.message, {} };
    }
    StoreData data = storeResult.value;

    auto prodIt = find_if(data.products.begin(), data.products.end(),
        [&](const Product& p) { return p.id == original.id; });
    if (prodIt == data.products.end()) {
        return { false, "Original product not found in catalogue.", {} };
    }

    // Verify the original snapshot matches the currently stored row
    bool snapshotMatches = (prodIt->barcode == original.barcode &&
        prodIt->categoryId == original.categoryId &&
        prodIt->acronym == original.acronym &&
        prodIt->name == original.name &&
        prodIt->unit == original.unit &&
        prodIt->unitPrice == original.unitPrice &&
        prodIt->taxRate == original.taxRate &&
        prodIt->discountPerUnit == original.discountPerUnit &&
        prodIt->onHand == original.onHand &&
        prodIt->barcodeImagePath == original.barcodeImagePath);
    if (!snapshotMatches) {
        return { false, "Product was modified by another operation. Please refresh and try again.", {} };
    }

    bool categoryExists = any_of(data.categories.begin(), data.categories.end(),
        [&](const Category& c) { return c.id == replacement.categoryId; });
    if (!categoryExists) {
        return { false, "Selected category does not exist.", {} };
    }

    // Check active hold reservations for this product
    Result<StockPosition> posResult = stockPosition(data, prodIt->id, "");
    if (!posResult.success) {
        return { false, posResult.message, {} };
    }

    if (replacement.onHand < posResult.value.reserved) {
        return { false, "On-hand stock cannot be reduced below currently reserved hold quantities.", {} };
    }

    bool hasActiveHold = (posResult.value.reserved > 0);
    if (!hasActiveHold) {
        for (const auto& sale : data.sales) {
            if (sale.archived || sale.status != OrderStatus::OnHold) {
                continue;
            }
            for (const auto& line : sale.lines) {
                if (line.productId == prodIt->id && line.quantity > 0) {
                    hasActiveHold = true;
                    break;
                }
            }
            if (hasActiveHold) {
                break;
            }
        }
    }

    if (has