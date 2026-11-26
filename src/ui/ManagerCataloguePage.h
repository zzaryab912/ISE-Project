#pragma once

#include "PosTypes.h"

#include <QWidget>

using namespace std;

class DiscountService;
class ProductService;
class QComboBox;
class QCheckBox;
class QDateEdit;
class QLabel;
class QLineEdit;
class QPushButton;
class QTableWidget;
class QVBoxLayout;

class ManagerCataloguePage final : public QWidget {
private:
    struct SeasonEditor {
        QWidget* form = nullptr;
        QDateEdit* start = nullptr;
        QDateEdit* end = nullptr;
        QLineEdit* rate = nullptr;
        QCheckBox* enabled = nullptr;
        QLabel* feedback = nullptr;
    };

    ProductService& products_;
    DiscountService& discounts_;
    optional<Product> selectedProduct_;
    optional<Season> selectedSeason_;
    optional<Category> selectedCategory_;
    vector<Category> categories_;
    vector<ProductStock> catalogue_;
    vector<Season> seasons_;
    QTableWidget* productTable_ = nullptr;
    QTableWidget* categoryTable_ = nullptr;
    QComboBox* unit_ = nullptr;
    QComboBox* category_ = nullptr;
    QLineEdit* name_ = nullptr;
    QLineEdit* acronym_ = nullptr;
    QLineEdit* productId_ = nullptr;
    QLineEdit* barcodeValue_ = nullptr;
    QLineEdit* price_ = nullptr;
    QLineEdit* tax_ = nullptr;
    QLineEdit* discount_ = nullptr;
    QLineEdit* onHand_ = nullptr;
    QLineEdit* categoryName_ = nullptr;
    QLineEdit* categoryAcronym_ = nullptr;
    QLabel* stock_ = nullptr;
    QLabel* barcode_ = nullptr;
    QLabel* barcodeStatus_ = nullptr;
    QComboBox* seasonChoice_ = nullptr;
    SeasonEditor existingSeason_;
    SeasonEditor newSeason_;
    QLineEdit* specialRate_ = nullptr;
    QCheckBox* specialActive_ = nullptr;
    QLabel* specialFeedback_ = nullptr;
    QLabel* feedback_ = nullptr;
    QLabel* categoryFeedback_ = nullptr;
    QPushButton* removeProduct_ = nullptr;
    QPushButton* removeCategory_ = nullptr;

    void showProduct(const ProductStock* item);
    void showCategory(const Category* category);
    void showProductMessage(const string& message, bool success);
    void showCategoryMessage(const string& message, bool success);
    QWidget* createDiscountsTab();
    void addSeasonEditor(QVBoxLayout& layout, SeasonEditor& editor, bool creating);
    void submitSeason(SeasonEditor& editor, const optional<Season>& original);
    void refreshSeasons(bool keepDraft = false);
    void refreshSpecial();
    void showSeason(int index);
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
