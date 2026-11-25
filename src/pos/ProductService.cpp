#include "ProductService.h"
#include "IPosStore.h"
#include "BarcodeAssetService.h"
#include "IdGenerator.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <iomanip>
#include <sstream>
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