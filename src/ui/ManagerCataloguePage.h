#pragma once

#include "PosTypes.h"

#include <QWidget>

using namespace std;

class DiscountService;
class ProductService;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QTableWidget;

class ManagerCataloguePage final : public QWidget {
private:
    ProductService& products_;
    DiscountService& discounts_;
    optional<Product> selectedProduct_;
    optional<Season> selectedSeason_;
    optional<Category> selectedCategory_;
    vector<Category> categories_;
    vector<ProductStock> catalogue_;
    QTableWidget* productTable_ = nullptr;
    QTableWidget* categoryTable_ = nullptr;
    QComboBox* unit_ = nullptr;
    QComboBox* category_ = nullptr;
    QLineEdit* name_ = nullptr;
    QLineEdit* acronym_ = nullptr;
    QLineEdit* price_ = nullptr;
    QLineEdit* tax_ = nullptr;
    QLineEdit* discount_ = nullptr;
    QLineEdit* onHand_ = nullptr;
    QLineEdit* categoryName_ = nullptr;
    QLineEdit* categoryAcronym_ = nullptr;
    QLabel* stock_ = nullptr;
    QLabel* barcode_ = nullptr;
    QLabel* barcodeStatus_ = nullptr;
    QLabel* seasonalSummary_ = nullptr;
    QLabel* specialSummary_ = nullptr;
    QLabel* feedback_ = nullptr;
    QLabel* categoryFeedback_ = nullptr;
    QPushButton* removeProduct_ = nullptr;
    QPushButton* removeCategory_ = nullptr;

    void showProduct(const ProductStock* item);
    void showCategory(const Category* category);
    void showProductMessage(const string& message, bool success);
    void showCategoryMessage(const string& message, bool success);
    void editSeason(bool create);
    void editSpecial();
    Result<Product> readProduct() const;
    void saveProduct(const Product& input);
    void previewBarcode(const string& categoryId);
    void saveCategory(const optional<Category>& original, const Category& input);
    void saveSeason(const optional<Season>& original, const Season& replacement);
    void saveSpecial(const SpecialDiscount& special);

public:
    ManagerCataloguePage(ProductService& products, DiscountService& discounts, QWidget* parent = nullptr);
    void refresh();
};
