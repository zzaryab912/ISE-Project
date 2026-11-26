#include "ManagerCataloguePage.h"

#include "DiscountService.h"
#include "ProductService.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDateEdit>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFormLayout>
#include <QFrame>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPixmap>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QTabWidget>
#include <QTableWidget>
#include <QVBoxLayout>

#include <limits>

using namespace std;

namespace {

QString text(const string& value) { return QString::fromStdString(value); }

QString fixed(int64_t value, int scale = 100, int decimals = 2)
{
    if (decimals == 0) return QString::number(value / scale);
    return QString::number(value / scale) + '.' +
        QString::number(value % scale).rightJustified(decimals, '0');
}

QString quantity(int64_t value, Unit unit)
{
    return fixed(value, 1000, unit == Unit::Count ? 0 : 3) +
        (unit == Unit::Count ? " items" : " kg");
}

Result<int64_t> decimalInput(const QString& input, int decimals)
{
    static const QRegularExpression pattern(R"(^(\d+)(?:\.(\d+))?$)");
    const auto match = pattern.match(input.trimmed());
    const auto fraction = match.captured(2);
    if (!match.hasMatch() || fraction.size() > decimals) return {false, "Enter a valid nonnegative value.", 0};
    bool valid = false;
    const int64_t value = (match.captured(1) + fraction.leftJustified(decimals, '0')).toLongLong(&valid);
    if (!valid) return {false, "Value is too large.", 0};
    return {true, {}, value};
}

QLabel* caption(const QString& value, const QString& role = {})
{
    auto* label = new QLabel(value);
    label->setObjectName(role);
    label->setTextFormat(Qt::PlainText);
    label->setWordWrap(true);
    return label;
}

QFrame* card(QVBoxLayout*& layout)
{
    auto* frame = new QFrame;
    frame->setObjectName("catalogueCard");
    layout = new QVBoxLayout(frame);
    layout->setContentsMargins(23, 20, 23, 20);
    layout->setSpacing(9);
    return frame;
}

QTableWidget* table(const QStringList& headers)
{
    auto* rows = new QTableWidget(0, headers.size());
    rows->setHorizontalHeaderLabels(headers);
    rows->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    rows->verticalHeader()->hide();
    rows->setEditTriggers(QAbstractItemView::NoEditTriggers);
    rows->setSelectionBehavior(QAbstractItemView::SelectRows);
    rows->setSelectionMode(QAbstractItemView::SingleSelection);
    rows->setAlternatingRowColors(true);
    return rows;
}

void addRow(QTableWidget& table, const QStringList& fields)
{
    const int row = table.rowCount();
    table.insertRow(row);
    for (int column = 0; column < fields.size(); ++column) {
        auto* item = new QTableWidgetItem(fields[column]);
        item->setToolTip(fields[column]);
        table.setItem(row, column, item);
    }
}

void message(QLabel& label, const string& status, bool success)
{
    label.setStyleSheet(success ? "color: #225f45;" : "color: #a22922;");
    label.setText(text(status));
}

QDateEdit* dateEditor(const QDate& day)
{
    auto* edit = new QDateEdit(day);
    edit->setCalendarPopup(true);
    edit->setDisplayFormat("dd MMM yyyy");
    return edit;
}

QString assetPath(const string& relative)
{
#ifdef EYERIS_PROJECT_DATA_DIR
    return QDir(QStringLiteral(EYERIS_PROJECT_DATA_DIR)).filePath(text(relative));
#else
    return QDir("data").filePath(text(relative));
#endif
}

} // namespace

ManagerCataloguePage::ManagerCataloguePage(ProductService& products, DiscountService& discounts,
                                           QWidget* parent)
    : QWidget(parent), products_(products), discounts_(discounts)
{
    setStyleSheet(R"(
        QLabel { background: transparent; color: #272727; font-size: 14px; }
        QLabel#catalogueHeading { font-size: 36px; font-weight: 500; }
        QLabel#catalogueTitle { font-size: 20px; font-weight: 600; }
        QLabel#catalogueHint { color: #646464; }
        QFrame#catalogueCard { background: white; border: 1px solid #e3e3e3; border-radius: 21px; }
        QTabWidget::pane { border: none; background: transparent; }
        QTabBar::tab { background: transparent; padding: 8px 21px; color: #575757; }
        QTabBar::tab:selected { border-bottom: 2px solid #161616; color: #171717; }
        QLineEdit, QComboBox, QDateEdit { min-height: 36px; padding: 0 10px; background: white;
            border: 1px solid #aeaeae; border-radius: 8px; color: #252525; }
        QLineEdit:focus, QComboBox:focus, QDateEdit:focus { border: 1px solid #252525; }
        QPushButton { background: white; color: #171818; border: 1px solid #dcdddd;
            border-radius: 8px; min-height: 38px; padding: 0 14px; }
        QPushButton:hover { background: #f3f3f3; }
        QPushButton:focus { border-color: #5576a4; }
        QPushButton[primary="true"] { background: #111; color: white; font-weight: 600; }
        QPushButton[primary="true"]:hover { background: #333; }
        QPushButton:disabled { background: #f0f1f1; color: #777978; }
        QTableWidget { border: none; background: white; alternate-background-color: #fafafa; }
        QHeaderView::section { background: #f5f5f5; border: none; border-bottom: 1px solid #e4e4e4;
            padding: 8px 6px; color: #4d4d4d; }
        QScrollArea, QWidget#catalogueFields { border: none; background: transparent; }
    )");
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(9);
    outer->addWidget(caption("Products & Discounts", "catalogueHeading"));
    auto* tabs = new QTabWidget;
    outer->addWidget(tabs, 1);

    auto* productsTab = new QWidget;
    auto* productLayout = new QVBoxLayout(productsTab);
    productLayout->setContentsMargins(0, 12, 0, 0);
    productLayout->setSpacing(16);
    tabs->addTab(productsTab, "Products");

    QVBoxLayout* promotionLayout;
    auto* promotions = card(promotionLayout);
    promotionLayout->addWidget(caption("Flat Discounts", "catalogueTitle"));
    promotionLayout->addWidget(caption(
        "Both percentages apply after product discounts and before tax.", "catalogueHint"));
    auto* seasonRow = new QHBoxLayout;
    seasonalSummary_ = caption({});
    auto* editSeasonButton = new QPushButton("Edit seasonal discount");
    auto* addSeasonButton = new QPushButton("Add season");
    seasonRow->addWidget(caption("Seasonal discount"));
    seasonRow->addWidget(seasonalSummary_, 1);
    seasonRow->addWidget(editSeasonButton);
    seasonRow->addWidget(addSeasonButton);
    promotionLayout->addLayout(seasonRow);
    auto* specialRow = new QHBoxLayout;
    specialSummary_ = caption({});
    auto* editSpecialButton = new QPushButton("Edit special discount");
    specialRow->addWidget(caption("Special discount"));
    specialRow->addWidget(specialSummary_, 1);
    specialRow->addWidget(editSpecialButton);
    promotionLayout->addLayout(specialRow);
    productLayout->addWidget(promotions);
    connect(editSeasonButton, &QPushButton::clicked, this, [this] { editSeason(false); });
    connect(addSeasonButton, &QPushButton::clicked, this, [this] { editSeason(true); });
    connect(editSpecialButton, &QPushButton::clicked, this, &ManagerCataloguePage::editSpecial);

    auto* productColumns = new QHBoxLayout;
    productColumns->setSpacing(24);
    QVBoxLayout* productListLayout;
    auto* productList = card(productListLayout);
    productListLayout->addWidget(caption("Products", "catalogueTitle"));
    productListLayout->addWidget(caption("Select a product to view and edit its details.", "catalogueHint"));
    auto* addProduct = new QPushButton("Add Product");
    productListLayout->addWidget(addProduct, 0, Qt::AlignLeft);
    productTable_ = table({"Product", "Barcode", "On hand", "Reserved", "Available"});
    productTable_->setAccessibleName("Products and stock");
    productListLayout->addWidget(productTable_, 1);
    productListLayout->addWidget(caption("Available stock = on hand − reserved.", "catalogueHint"));
    productColumns->addWidget(productList, 650);

    QVBoxLayout* detailsLayout;
    auto* details = card(detailsLayout);
    detailsLayout->addWidget(caption("Product details", "catalogueTitle"));
    detailsLayout->addWidget(caption("Manage pricing, stock and the product label.", "catalogueHint"));
    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    auto* fields = new QWidget;
    fields->setObjectName("catalogueFields");
    auto* fieldsLayout = new QVBoxLayout(fields);
    fieldsLayout->setContentsMargins(0, 0, 8, 0);
    auto* form = new QFormLayout;
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    name_ = new QLineEdit;
    acronym_ = new QLineEdit;
    acronym_->setMaxLength(8);
    unit_ = new QComboBox;
    unit_->addItem("Count", static_cast<int>(Unit::Count));
    unit_->addItem("Weight (kg)", static_cast<int>(Unit::Weight));
    category_ = new QComboBox;
    price_ = new QLineEdit;
    tax_ = new QLineEdit;
    discount_ = new QLineEdit;
    onHand_ = new QLineEdit;
    form->addRow("Product name", name_);
    form->addRow("Acronym (max 8)", acronym_);
    form->addRow("Measurement unit", unit_);
    form->addRow("Category", category_);
    form->addRow("Unit price (PKR)", price_);
    form->addRow("Tax rate (%)", tax_);
    form->addRow("Product discount (PKR/unit)", discount_);
    form->addRow("On hand", onHand_);
    stock_ = caption({});
    form->addRow("Stock", stock_);
    fieldsLayout->addLayout(form);
    barcode_ = caption({});
    barcode_->setAlignment(Qt::AlignCenter);
    barcode_->setMinimumHeight(76);
    barcodeStatus_ = caption({}, "catalogueHint");
    fieldsLayout->addWidget(barcode_);
    fieldsLayout->addWidget(barcodeStatus_, 0, Qt::AlignCenter);
    fieldsLayout->addStretch();
    scroll->setWidget(fields);
    detailsLayout->addWidget(scroll, 1);
    feedback_ = caption({});
    detailsLayout->addWidget(feedback_);
    auto* productActions = new QHBoxLayout;
    auto* preview = new QPushButton("Generate Barcode Preview");
    auto* save = new QPushButton("Save Product");
    save->setProperty("primary", true);
    removeProduct_ = new QPushButton("Delete Product");
    productActions->addWidget(removeProduct_);
    productActions->addStretch();
    productActions->addWidget(preview);
    productActions->addWidget(save);
    detailsLayout->addLayout(productActions);
    productColumns->addWidget(details, 828);
    productLayout->addLayout(productColumns, 1);

    auto* categoriesTab = new QWidget;
    auto* categoryColumns = new QHBoxLayout(categoriesTab);
    categoryColumns->setContentsMargins(0, 12, 0, 0);
    categoryColumns->setSpacing(24);
    QVBoxLayout* categoryListLayout;
    categoryColumns->addWidget(card(categoryListLayout), 650);
    categoryListLayout->addWidget(caption("Categories", "catalogueTitle"));
    auto* addCategory = new QPushButton("Add Category");
    categoryListLayout->addWidget(addCategory, 0, Qt::AlignLeft);
    categoryTable_ = table({"ID / Barcode prefix", "Category", "Acronym"});
    categoryTable_->setAccessibleName("Product categories");
    categoryListLayout->addWidget(categoryTable_, 1);
    QVBoxLayout* editCategoryLayout;
    categoryColumns->addWidget(card(editCategoryLayout), 828);
    editCategoryLayout->addWidget(caption("Category details", "catalogueTitle"));
    auto* categoryForm = new QFormLayout;
    categoryName_ = new QLineEdit;
    categoryAcronym_ = new QLineEdit;
    categoryForm->addRow("Category name", categoryName_);
    categoryForm->addRow("Acronym", categoryAcronym_);
    editCategoryLayout->addLayout(categoryForm);
    categoryFeedback_ = caption({});
    editCategoryLayout->addWidget(categoryFeedback_);
    auto* categoryActions = new QHBoxLayout;
    removeCategory_ = new QPushButton("Delete Category");
    auto* saveCategoryButton = new QPushButton("Save Category");
    saveCategoryButton->setProperty("primary", true);
    categoryActions->addWidget(removeCategory_);
    categoryActions->addStretch();
    categoryActions->addWidget(saveCategoryButton);
    editCategoryLayout->addLayout(categoryActions);
    editCategoryLayout->addStretch();
    tabs->addTab(categoriesTab, "Categories");

    connect(productTable_, &QTableWidget::itemSelectionChanged, this, [this] {
        const int row = productTable_->currentRow();
        if (row >= 0 && static_cast<size_t>(row) < catalogue_.size()) {
            showProduct(&catalogue_[static_cast<size_t>(row)]);
            feedback_->clear();
        }
    });
    connect(categoryTable_, &QTableWidget::itemSelectionChanged, this, [this] {
        const int row = categoryTable_->currentRow();
        if (row >= 0 && static_cast<size_t>(row) < categories_.size()) {
            showCategory(&categories_[static_cast<size_t>(row)]);
            categoryFeedback_->clear();
        }
    });
    connect(addProduct, &QPushButton::clicked, this, [this] {
        productTable_->clearSelection();
        showProduct(nullptr);
        feedback_->clear();
        name_->setFocus();
    });
    connect(addCategory, &QPushButton::clicked, this, [this] {
        categoryTable_->clearSelection();
        showCategory(nullptr);
        categoryFeedback_->clear();
        categoryName_->setFocus();
    });
    connect(category_, &QComboBox::currentIndexChanged, this, [this] {
        barcode_->clear();
        barcodeStatus_->setText(category_->currentData().toString().isEmpty() ?
            "Choose a saved category before generating a barcode." : "Generate a preview for the selected category.");
    });
    connect(preview, &QPushButton::clicked, this, [this] {
        previewBarcode(category_->currentData().toString().toStdString());
    });
    connect(save, &QPushButton::clicked, this, [this] {
        const auto input = readProduct();
        if (input.success) saveProduct(input.value);
        else showProductMessage(input.message, false);
    });
    connect(removeProduct_, &QPushButton::clicked, this, [this] {
        if (!selectedProduct_ || QMessageBox::question(this, "Delete Product",
            "Delete " + text(selectedProduct_->name) + " from the catalogue?") != QMessageBox::Yes) return;
        const auto outcome = products_.remove(selectedProduct_->id);
        if (outcome.success) { selectedProduct_.reset(); refresh(); }
        showProductMessage(outcome.message, outcome.success);
    });
    connect(saveCategoryButton, &QPushButton::clicked, this, [this] {
        saveCategory(selectedCategory_, {selectedCategory_ ? selectedCategory_->id : string{},
            categoryName_->text().trimmed().toStdString(), categoryAcronym_->text().trimmed().toStdString()});
    });
    connect(removeCategory_, &QPushButton::clicked, this, [this] {
        if (!selectedCategory_ || QMessageBox::question(this, "Delete Category",
            "Delete the category " + text(selectedCategory_->name) + "?") != QMessageBox::Yes) return;
        const auto outcome = products_.removeCategory(selectedCategory_->id);
        if (outcome.success) { selectedCategory_.reset(); refresh(); }
        showCategoryMessage(outcome.message, outcome.success);
    });
    refresh();
}

void ManagerCataloguePage::refresh()
{
    const string productId = selectedProduct_ ? selectedProduct_->id : string{};
    const string categoryId = selectedCategory_ ? selectedCategory_->id : string{};
    const QSignalBlocker productBlocker(productTable_);
    const QSignalBlocker categoryBlocker(categoryTable_);
    const QSignalBlocker choicesBlocker(category_);
    const auto categories = products_.listCategories();
    const auto products = products_.listWithStock();
    categories_ = categories.success ? categories.value : vector<Category>{};
    catalogue_ = products.success ? products.value : vector<ProductStock>{};
    category_->clear();
    category_->addItem("Choose a saved category", QString());
    categoryTable_->setRowCount(0);
    const Category* currentCategory = nullptr;
    for (const auto& category : categories_) {
        category_->addItem(text(category.name) + " (" + text(category.id) + ")", text(category.id));
        addRow(*categoryTable_, {text(category.id), text(category.name), text(category.acronym)});
        if (category.id == categoryId) {
            categoryTable_->selectRow(categoryTable_->rowCount() - 1);
            currentCategory = &category;
        }
    }
    productTable_->setRowCount(0);
    const ProductStock* currentProduct = nullptr;
    for (const auto& item : catalogue_) {
        const auto& product = item.product;
        addRow(*productTable_, {text(product.name), text(product.barcode), quantity(item.stock.onHand, product.unit),
            quantity(item.stock.reserved, product.unit), quantity(item.stock.available, product.unit)});
        if (product.id == productId) {
            productTable_->selectRow(productTable_->rowCount() - 1);
            currentProduct = &item;
        }
    }
    showProduct(currentProduct);
    showCategory(currentCategory);
    if (!categories.success) showCategoryMessage(categories.message, false);
    if (!products.success) showProductMessage(products.message, false);
    const auto seasons = discounts_.listSeasons();
    seasonalSummary_->setText(seasons.success ? "No season enabled" : text(seasons.message));
    if (seasons.success) {
        for (const auto& season : seasons.value) {
            if (season.active) {
                seasonalSummary_->setText(fixed(season.rate) + "%  ·  " + text(season.startDate) +
                    " – " + text(season.endDate) + "  ·  Enabled");
                break;
            }
        }
    }
    const auto special = discounts_.getSpecial();
    specialSummary_->setText(special.success ?
        fixed(special.value.rate) + "%  ·  " + (special.value.active ? "Enabled" : "Disabled") : text(special.message));
}

void ManagerCataloguePage::showProduct(const ProductStock* item)
{
    selectedProduct_ = item ? optional<Product>(item->product) : nullopt;
    const Product product = selectedProduct_.value_or(Product{});
    name_->setText(text(product.name));
    acronym_->setText(text(product.acronym));
    unit_->setCurrentIndex(unit_->findData(static_cast<int>(product.unit)));
    category_->setCurrentIndex(qMax(0, category_->findData(text(product.categoryId))));
    price_->setText(fixed(product.unitPrice));
    tax_->setText(fixed(product.taxRate));
    discount_->setText(fixed(product.discountPerUnit));
    onHand_->setText(fixed(product.onHand, 1000, product.unit == Unit::Count ? 0 : 3));
    stock_->setText(item ? "Reserved " + quantity(item->stock.reserved, product.unit) +
        "  ·  Available " + quantity(item->stock.available, product.unit) : "—");
    removeProduct_->setEnabled(item != nullptr);
    barcode_->clear();
    if (!item) {
        barcodeStatus_->setText("Choose a saved category before generating a barcode.");
        return;
    }
    const QPixmap saved(assetPath(product.barcodeImagePath));
    if (!saved.isNull()) barcode_->setPixmap(saved.scaled(420, 90, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    barcodeStatus_->setText(text(product.barcode) + (saved.isNull() ? "  ·  Saved image unavailable" : "  ·  Saved label"));
}

void ManagerCataloguePage::showCategory(const Category* category)
{
    selectedCategory_ = category ? optional<Category>(*category) : nullopt;
    categoryName_->setText(category ? text(category->name) : QString());
    categoryAcronym_->setText(category ? text(category->acronym) : QString());
    removeCategory_->setEnabled(category != nullptr);
}

Result<Product> ManagerCataloguePage::readProduct() const
{
    Product input = selectedProduct_.value_or(Product{});
    input.name = name_->text().trimmed().toStdString();
    input.acronym = acronym_->text().trimmed().toStdString();
    input.categoryId = category_->currentData().toString().toStdString();
    input.unit = static_cast<Unit>(unit_->currentData().toInt());
    if (input.categoryId.empty()) return {false, "Choose a saved category.", {}};
    const auto price = decimalInput(price_->text(), 2);
    const auto tax = decimalInput(tax_->text(), 2);
    const auto discount = decimalInput(discount_->text(), 2);
    const auto stock = decimalInput(onHand_->text(), input.unit == Unit::Count ? 0 : 3);
    if (!price.success) return {false, "Unit price: " + price.message, {}};
    if (!tax.success || tax.value > 10000) return {false, "Enter a tax rate from 0.00% to 100.00%.", {}};
    if (!discount.success) return {false, "Product discount: " + discount.message, {}};
    if (!stock.success || (input.unit == Unit::Count && stock.value > numeric_limits<int64_t>::max() / 1000))
        return {false, "Enter a valid stock quantity for the selected unit.", {}};
    input.unitPrice = price.value;
    input.taxRate = static_cast<int32_t>(tax.value);
    input.discountPerUnit = discount.value;
    input.onHand = input.unit == Unit::Count ? stock.value * 1000 : stock.value;
    return {true, {}, input};
}

void ManagerCataloguePage::saveProduct(const Product& input)
{
    const auto result = selectedProduct_ ? products_.update(*selectedProduct_, input) : products_.create(input);
    if (result.success) { selectedProduct_ = result.value; refresh(); }
    showProductMessage(result.message, result.success);
}

void ManagerCataloguePage::previewBarcode(const string& categoryId)
{
    if (categoryId.empty()) { showProductMessage("Choose a saved category first.", false); return; }
    const auto result = products_.previewBarcode(categoryId);
    if (!result.success) { showProductMessage(result.message, false); return; }
    barcode_->setPixmap(QPixmap::fromImage(result.value).scaled(420, 90, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    barcodeStatus_->setText("Preview only · the final barcode is assigned when saved.");
    feedback_->clear();
}

void ManagerCataloguePage::saveCategory(const optional<Category>& original, const Category& input)
{
    const auto result = products_.saveCategory(original, input);
    if (result.success) { selectedCategory_ = result.value; refresh(); }
    showCategoryMessage(result.message, result.success);
}

void ManagerCataloguePage::saveSeason(const optional<Season>& original, const Season& replacement)
{
    const auto result = discounts_.saveSeason(original, replacement);
    if (result.success) { selectedSeason_ = replacement; refresh(); }
    showProductMessage(result.message, result.success);
}

void ManagerCataloguePage::saveSpecial(const SpecialDiscount& special)
{
    const auto result = discounts_.saveSpecial(special);
    if (result.success) refresh();
    showProductMessage(result.message, result.success);
}

void ManagerCataloguePage::editSeason(bool create)
{
    const auto result = discounts_.listSeasons();
    if (!result.success) { showProductMessage(result.message, false); return; }
    if (!create && result.value.empty()) { showProductMessage("Add a season first.", false); return; }
    QDialog dialog(this);
    dialog.setWindowTitle(create ? "Add Seasonal Discount" : "Edit Seasonal Discount");
    dialog.setMinimumWidth(460);
    auto* form = new QFormLayout(&dialog);
    auto* seasonPicker = new QComboBox;
    auto* start = dateEditor(QDate::currentDate());
    auto* end = dateEditor(QDate::currentDate());
    auto* rate = new QLineEdit("0.00");
    auto* enabled = new QCheckBox("Enabled");
    if (!create) {
        for (const auto& season : result.value)
            seasonPicker->addItem(text(season.startDate + " – " + season.endDate) + (season.active ? " · Enabled" : ""));
        form->addRow("Season", seasonPicker);
    } else {
        seasonPicker->hide();
        delete seasonPicker;
        seasonPicker = nullptr;
    }
    const auto select = [&](int index) {
        if (index < 0) return;
        const auto& season = result.value.at(static_cast<size_t>(index));
        start->setDate(QDate::fromString(text(season.startDate), Qt::ISODate));
        end->setDate(QDate::fromString(text(season.endDate), Qt::ISODate));
        rate->setText(fixed(season.rate));
        enabled->setChecked(season.active);
    };
    if (seasonPicker) { select(0); connect(seasonPicker, &QComboBox::currentIndexChanged, &dialog, select); }
    form->addRow("Start date", start);
    form->addRow("End date", end);
    form->addRow("Discount (%)", rate);
    form->addRow(enabled);
    auto* error = caption({});
    form->addRow(error);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
    form->addRow(buttons);
    optional<Season> replacement;
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        const auto percent = decimalInput(rate->text(), 2);
        if (!percent.success || percent.value > 10000 || !start->hasAcceptableInput() ||
            !end->hasAcceptableInput() || start->date() > end->date()) {
            message(*error, "Enter valid dates and a discount from 0.00% to 100.00%.", false);
            return;
        }
        replacement = Season{start->date().toString(Qt::ISODate).toStdString(),
            end->date().toString(Qt::ISODate).toStdString(), static_cast<int32_t>(percent.value), enabled->isChecked()};
        dialog.accept();
    });
    if (dialog.exec() == QDialog::Accepted && replacement) {
        selectedSeason_ = seasonPicker ? optional<Season>(result.value.at(static_cast<size_t>(seasonPicker->currentIndex()))) : nullopt;
        saveSeason(selectedSeason_, *replacement);
    }
}

void ManagerCataloguePage::editSpecial()
{
    const auto special = discounts_.getSpecial();
    if (!special.success) { showProductMessage(special.message, false); return; }
    QDialog dialog(this);
    dialog.setWindowTitle("Edit Special Discount");
    dialog.setMinimumWidth(420);
    auto* form = new QFormLayout(&dialog);
    auto* rate = new QLineEdit(fixed(special.value.rate));
    auto* enabled = new QCheckBox("Enabled");
    enabled->setChecked(special.value.active);
    form->addRow("Discount (0–8%)", rate);
    form->addRow(enabled);
    auto* error = caption({});
    form->addRow(error);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
    form->addRow(buttons);
    optional<SpecialDiscount> replacement;
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        const auto percent = decimalInput(rate->text(), 2);
        if (!percent.success || percent.value > 800) {
            message(*error, "Enter a special discount from 0.00% to 8.00%.", false);
            return;
        }
        replacement = SpecialDiscount{static_cast<int32_t>(percent.value), enabled->isChecked()};
        dialog.accept();
    });
    if (dialog.exec() == QDialog::Accepted && replacement) saveSpecial(*replacement);
}

void ManagerCataloguePage::showProductMessage(const string& status, bool success)
{
    message(*feedback_, status, success);
}

void ManagerCataloguePage::showCategoryMessage(const string& status, bool success)
{
    message(*categoryFeedback_, status, success);
}
