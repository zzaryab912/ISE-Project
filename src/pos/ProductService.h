#pragma once

#include "PosTypes.h"
#include <QImage>

using namespace std;

class IPosStore;
class BarcodeAssetService;
class IdGenerator;

/**
 * @brief Manages product categories, catalogue records, barcode allocation,
 *        and inventory/reservation rules for the POS system.
 */
class ProductService {
private:
    IPosStore& store_;
    BarcodeAssetService& images_;
    const IdGenerator& ids_;

public:
    /**
     * @brief Constructs the ProductService with references to shared services.
     */
    ProductService(IPosStore& store, BarcodeAssetService& images, const IdGenerator& ids);

    // --- Catalogue & Category Queries ---

    /**
     * @brief Retrieves all products currently in the catalogue.
     */
    Result<vector<Product>> list() const;

    /**
     * @brief Retrieves all product categories.
     */
    Result<vector<Category>> listCategories() const;

    /**
     * @brief Finds a live product by its internal system ID.
     */
    Result<Product> findById(const string& productId) const;

    /**
     * @brief Finds a live product by its active 6-digit barcode.
     */
    Result<Product> findByBarcode(const string& barcode) const;

    /**
     * @brief Lists all products along with their computed stock positions from one snapshot.
     */
    Result<vector<ProductStock>> listWithStock() const;

    /**
     * @brief Resolves a product by ID with live stock, optionally excluding a held order's own reservation.
     */
    Result<ProductStock> findWithStock(const string& productId, const string& excludeHeldOrderId) const;

    /**
     * @brief Resolves a product by barcode with live stock, optionally excluding a held order's own reservation.
     */
    Result<ProductStock> findWithStockByBarcode(const string& barcode, const string& excludeHeldOrderId) const;

    // --- Category Mutations ---

    /**
     * @brief Creates a new category (when original is empty) or updates an existing one.
     */
    Result<Category> saveCategory(const optional<Category>& original, const Category& input);

    /**
     * @brief Removes a category if no products or issued barcodes reference it.
     */
    Status removeCategory(const string& categoryId);

    // --- Barcode Preview & Product Mutations ---

    /**
     * @brief Generates a preview barcode image for the given category without issuing the code.
     */
    Result<QImage> previewBarcode(const string& categoryId) const;

    /**
     * @brief Validates and creates a new product, assigning its ID, barcode, and PNG asset.
     */
    Result<Product> create(const Product& input);

    /**
     * @brief Updates an existing product, preserving its ID and enforcing hold/reservation rules.
     */
    Result<Product> update(const Product& original, const Product& replacement);

    /**
     * @brief Deletes a product if no active OnHold order references it.
     */
    Status remove(const string& productId);

    // --- Reusable Stock & Reservation Rules ---

    /**
     * @brief Computes physical (onHand), reserved, and available stock from a StoreData snapshot.
     */
    Result<StockPosition> stockPosition(const StoreData& data, const string& productId, const string& excludeHeldOrderId) const;

    /**
     * @brief Checks all requested sale lines against live stock and returns any shortages.
     */
    Result<vector<StockShortage>> shortages(const StoreData& data, const vector<SaleLine>& requested, const string& excludeHeldOrderId) const;

    /**
     * @brief Validates and deducts physical stock in the supplied snapshot without writing to disk.
     */
    Status deductStock(StoreData& data, const vector<SaleLine>& lines, const string& excludeHeldOrderId) const;

    /**
     * @brief Applies net stock increases or restockable decreases for an approved paid bill edit.
     */
    Status applyPaidStockDifference(StoreData& data, const vector<SaleLine>& before, const vector<QuantityChange>& changes) const;
};