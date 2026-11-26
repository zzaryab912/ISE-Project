#include "ManagerCataloguePage.h"

#include "DiscountService.h"
#include "ProductService.h"
#include "PosWindow.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDateEdit>
#include <QDir>
#include <QGridLayout>
#include <QFrame>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPixmap>
#include <QPushButton>
#include <QRegularExpression>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QTabBar>
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

QWidget* field(const QString& title, QWidget* control)
{
    auto* container = new QWidget;
    auto* layout = new QVBoxLayout(container);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6);
    auto* label = caption(title);
    label->setBuddy(control);
    control->setAccessibleName(title);
    control->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    control->setFixedHeight(40);
    layout->addWidget(label);
    layout->addWidget(control);
    return container;
}

QFrame* divider()
{
    auto* line = new QFrame;
    line->setObjectName("catalogueDivider");
    line->setFixedHeight(1);
    return line;
}

QPushButton* secondaryButton(const QString& title)
{
    auto* button = new QPushButton(title);
    button->setObjectName("catalogueSecondary");
    button->setCursor(Qt::PointingHandCursor);
    return button;
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
    label.setVisible(!status.empty());
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
        QLabel { background:transparent; color:#242424; font-size:14px; }
        QLabel#catalogueHeading { font-size:36px; font-weight:500; }
        QLabel#catalogueTitle { font-size:20px; font-weight:600; }
        QLabel#catalogueHint { color:#656565; }
        QFrame#catalogueCard { background:white; border:1px solid #e3e3e3; border-radius:21px; }
        QFrame#catalogueDivider { background:#e6e6e6; border:none; }
        QTabBar::tab { background:transparent; padding:0 20px 0 0; min-height:34px;
            color:#686868; border-bottom:2px solid transparent; margin-right:14px; }
        QTabBar::tab:selected { border-bottom-color:#242424; font-weight:600; color:#242424; }
        QTabBar::tab:focus { color:#242424; }
        QLineEdit, QDateEdit { padding:0 12px; background:#fafafa;
            border:1px solid #e3e3e3; border-radius:8px; color:#252525; }
        QLineEdit[readOnly="true"] { background:#f0f0f0; color:#535353; }
        QLineEdit:focus, QDateEdit:focus { border-color:#858585; }
        QDateEdit::drop-down { border:none; width:30px; background:transparent; }
        QDateEdit::down-arrow { image:none; width:0; height:0; }
        QPushButton#catalogueSecondary { background:white; color:#242424; border:1px solid #e3e3e3;
            border-radius:8px; min-height:38px; padding:0 14px; }
        QPushButton#catalogueSecondary:hover { background:#f3f3f3; }
        QPushButton#catalogueSecondary:pressed { background:#ededed; }
        QPushButton#catalogueSecondary:focus { border-color:#858585; }
        QPushButton#catalogueSecondary:disabled { background:#f2f2f2; color:#767676; }
        QCheckBox { color:#242424; spacing:8px; }
        QCheckBox::indicator { width:16px; height:16px; border-radius:4px;
            background:#ffffff; border:1px solid #a0abaa; }
        QCheckBox::indicator:checked { background:#4e6766; border:3px solid #e5ebea; }
        QCheckBox::indicator:hover { border-color:#466765; }
        QTableWidget { border:none; background:white; alternate-background-color:#fafafa;
            selection-background-color:#e8eeee; selection-color:#242424; }
        QHeaderView::section { background:#f7f7f7; border:none; border-bottom:1px solid #e6e6e6;
            padding:8px 6px; color:#4d4d4d; }
    )");
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);
    auto* heading = caption("Products & Discounts", "catalogueHeading");
    heading->setFixedHeight(43);
    outer->addWidget(heading);
    outer->addSpacing(14);
    auto* tabBar = new QTabBar;
    tabBar->setAccessibleName("Products and discounts tabs");
    tabBar->setExpanding(false);
    tabBar->setDrawBase(false);
    tabBar->setUsesScrollButtons(false);
    tabBar->addTab("Products");
    tabBar->addTab("Categories");
    tabBar->addTab("Discounts");
    outer->addWidget(tabBar, 0, Qt::AlignLeft);
    outer->addWidget(divider());
    outer->addSpacing(14);
    auto* pages = new QStackedWidget;
    outer->addWidget(pages, 1);
    connect(tabBar, &QTabBar::currentChanged, pages, &QStackedWidget::setCurrentIndex);

    auto* productsTab = new QWidget;
    auto* productColumns = new QHBoxLayout(productsTab);
    productColumns->setContentsMargins(0, 0, 0, 0);
    productColumns->setSpacing(24);
    pages->addWidget(productsTab);

    QVBoxLayout* productListLayout;
    auto* productList = card(productListLayout);
    auto* productHeader = new QHBoxLayout;
    auto* productTitles = new QVBoxLayout;
    productTitles->setSpacing(4);
    productTitles->addWidget(caption("Products", "catalogueTitle"));
    productTitles->addWidget(caption("Select a product to view or edit it.", "catalogueHint"));
    productHeader->addLayout(productTitles, 1);
    auto* addProduct = PosUi::primaryButton("Add Product");
    productHeader->addWidget(addProduct, 0, Qt::AlignTop);
    productListLayout->addLayout(productHeader);
    productListLayout->addSpacing(7);
    productTable_ = table({"Product", "Barcode", "On hand", "Reserved", "Available"});
    productTable_->setAccessibleName("Products and stock");
    productTable_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    for (int column = 1; column < productTable_->columnCount(); ++column)
        productTable_->horizontalHeader()->setSectionResizeMode(column, QHeaderView::ResizeToContents);
    productListLayout->addWidget(productTable_, 1);
    productColumns->addWidget(productList, 650);

    QVBoxLayout* detailsLayout;
    auto* details = card(detailsLayout);
    detailsLayout->addWidget(caption("Product details", "catalogueTitle"));
    detailsLayout->addWidget(caption("Manage pricing, stock and the product label.", "catalogueHint"));
    detailsLayout->addWidget(divider());
    auto* form = new QGridLayout;
    form->setContentsMargins(0, 4, 0, 2);
    form->setHorizontalSpacing(16);
    form->setVerticalSpacing(13);
    for (int column = 0; column < 6; ++column) form->setColumnStretch(column, 1);
    productId_ = new QLineEdit;
    productId_->setReadOnly(true);
    barcodeValue_ = new QLineEdit;
    barcodeValue_->setReadOnly(true);
    name_ = new QLineEdit;
    acronym_ = new QLineEdit;
    acronym_->setMaxLength(8);
    unit_ = PosUi::choice("Measurement unit");
    unit_->addItem("Count", static_cast<int>(Unit::Count));
    unit_->addItem("Weight (kg)", static_cast<int>(Unit::Weight));
    category_ = PosUi::choice("Category");
    price_ = new QLineEdit;
    tax_ = new QLineEdit;
    discount_ = new QLineEdit;
    onHand_ = new QLineEdit;
    form->addWidget(field("Product ID", productId_), 0, 0, 1, 2);
    form->addWidget(field("Barcode value", barcodeValue_), 0, 2, 1, 2);
    form->addWidget(field("Acronym (max 8)", acronym_), 0, 4, 1, 2);
    form->addWidget(field("Product name", name_), 1, 0, 1, 3);
    form->addWidget(field("Measurement unit", unit_), 1, 3, 1, 3);
    form->addWidget(field("Category", category_), 2, 0, 1, 3);
    form->addWidget(field("On hand", onHand_), 2, 3, 1, 3);
    form->addWidget(field("Unit price (PKR)", price_), 3, 0, 1, 2);
    form->addWidget(field("Tax rate (%)", tax_), 3, 2, 1, 2);
    form->addWidget(field("Discount / unit (PKR)", discount_), 3, 4, 1, 2);
    detailsLayout->addLayout(form);
    stock_ = caption({}, "catalogueHint");
    detailsLayout->addWidget(stock_);
    detailsLayout->addWidget(divider());
    detailsLayout->addWidget(caption("Barcode image", "catalogueTitle"));
    auto* barcodeRow = new QHBoxLayout;
    barcode_ = caption({});
    barcode_->setFixedSize(232, 82);
    barcode_->setAlignment(Qt::AlignCenter);
    barcodeStatus_ = caption({}, "catalogueHint");
    barcodeRow->addWidget(barcode_, 0, Qt::AlignLeft);
    barcodeRow->addWidget(barcodeStatus_, 1);
    detailsLayout->addLayout(barcodeRow);
    detailsLayout->addStretch(1);
    feedback_ = caption({});
    feedback_->hide();
    detailsLayout->addWidget(feedback_);
    auto* productActions = new QHBoxLayout;
    auto* preview = secondaryButton("Generate Barcode Preview");
    auto* save = PosUi::primaryButton("Save Product");
    removeProduct_ = secondaryButton("Delete Product");
    productActions->addWidget(removeProduct_);
    productActions->addStretch();
    productActions->addWidget(preview);
    productActions->addWidget(save);
    detailsLayout->addWidget(divider());
    detailsLayout->addSpacing(4);
    detailsLayout->addLayout(productActions);
    productColumns->addWidget(details, 828);

    auto* categoriesTab = new QWidget;
    auto* categoryColumns = new QHBoxLayout(categoriesTab);
    categoryColumns->setContentsMargins(0, 0, 0, 0);
    categoryColumns->setSpacing(24);
    QVBoxLayout* categoryListLayout;
    categoryColumns->addWidget(card(categoryListLayout), 650);
    auto* categoryHeader = new QHBoxLayout;
    categoryHeader->addWidget(caption("Categories", "catalogueTitle"), 1);
    auto* addCategory = secondaryButton("Add Category");
    categoryHeader->addWidget(addCategory);
    categoryListLayout->addLayout(categoryHeader);
    categoryListLayout->addSpacing(7);
    categoryTable_ = table({"ID / Barcode prefix", "Category", "Acronym"});
    categoryTable_->setAccessibleName("Product categories");
    categoryListLayout->addWidget(categoryTable_, 1);
    QVBoxLayout* editCategoryLayout;
    auto* categoryDetails = card(editCategoryLayout);
    categoryColumns->addWidget(categoryDetails, 828, Qt::AlignTop);
    editCategoryLayout->addWidget(caption("Category details", "catalogueTitle"));
    editCategoryLayout->addSpacing(7);
    auto* categoryForm = new QHBoxLayout;
    categoryForm->setSpacing(16);
    categoryName_ = new QLineEdit;
    categoryAcronym_ = new QLineEdit;
    categoryForm->addWidget(field("Category name", categoryName_), 3);
    categoryForm->addWidget(field("Acronym", categoryAcronym_), 1);
    editCategoryLayout->addLayout(categoryForm);
    categoryFeedback_ = caption({});
    categoryFeedback_->hide();
    editCategoryLayout->addWidget(categoryFeedback_);
    editCategoryLayout->addSpacing(7);
    editCategoryLayout->addWidget(divider());
    auto* categoryActions = new QHBoxLayout;
    removeCategory_ = secondaryButton("Delete Category");
    auto* saveCategoryButton = PosUi::primaryButton("Save Category");
    categoryActions->addWidget(removeCategory_);
    categoryActions->addStretch();
    categoryActions->addWidget(saveCategoryButton);
    editCategoryLayout->addLayout(categoryActions);
    pages->addWidget(categoriesTab);
    pages->addWidget(createDiscountsTab());

    connect(productTable_, &QTableWidget::itemSelectionChanged, this, [this] {
        const int row = productTable_->currentRow();
        if (row >= 0 && static_cast<size_t>(row) < catalogue_.size()) {
            showProduct(&catalogue_[static_cast<size_t>(row)]);
            feedback_->hide();
        }
    });
    connect(categoryTable_, &QTableWidget::itemSelectionChanged, this, [this] {
        const int row = categoryTable_->currentRow();
        if (row >= 0 && static_cast<size_t>(row) < categories_.size()) {
            showCategory(&categories_[static_cast<size_t>(row)]);
            categoryFeedback_->hide();
        }
    });
    connect(addProduct, &QPushButton::clicked, this, [this] {
        productTable_->clearSelection();
        showProduct(nullptr);
        feedback_->hide();
        name_->setFocus();
    });
    connect(addCategory, &QPushButton::clicked, this, [this] {
        categoryTable_->clearSelection();
        showCategory(nullptr);
        categoryFeedback_->hide();
        categoryName_->setFocus();
    });
    connect(category_, &QComboBox::currentIndexChanged, this, [this] {
        barcode_->clear();
        barcode_->hide();
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
    refreshSeasons();
    refreshSpecial();
}

void ManagerCataloguePage::showProduct(const ProductStock* item)
{
    selectedProduct_ = item ? optional<Product>(item->product) : nullopt;
    const Product product = selectedProduct_.value_or(Product{});
    productId_->setText(item ? text(product.id) : "—");
    barcodeValue_->setText(item ? text(product.barcode) : "—");
    name_->setText(text(product.name));
    acronym_->setText(text(product.acronym));
    unit_->setCurrentIndex(unit_->findData(static_cast<int>(product.unit)));
    category_->setCurrentIndex(qMax(0, category_->findData(text(product.categoryId))));
    price_->setText(fixed(product.unitPrice));
    tax_->setText(fixed(product.taxRate));
    discount_->setText(fixed(product.discountPerUnit));
    onHand_->setText(fixed(product.onHand, 1000, product.unit == Unit::Count ? 0 : 3));
    stock_->setVisible(item != nullptr);
    stock_->setText(item ? "Reserved " + quantity(item->stock.reserved, product.unit) +
        "  ·  Available " + quantity(item->stock.available, product.unit) : "—");
    removeProduct_->setEnabled(item != nullptr);
    barcode_->clear();
    barcode_->hide();
    if (!item) {
        barcodeStatus_->setText("Choose a saved category before generating a barcode.");
        return;
    }
    const QPixmap saved(assetPath(product.barcodeImagePath));
    if (!saved.isNull()) {
        barcode_->setPixmap(saved.scaled(barcode_->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
        barcode_->show();
    }
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
    barcode_->setPixmap(QPixmap::fromImage(result.value).scaled(
        barcode_->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
    barcode_->show();
    barcodeStatus_->setText("Preview only · the final barcode is assigned when saved.");
    feedback_->hide();
}

void ManagerCataloguePage::saveCategory(const optional<Category>& original, const Category& input)
{
    const auto result = products_.saveCategory(original, input);
    if (result.success) { selectedCategory_ = result.value; refresh(); }
    showCategoryMessage(result.message, result.success);
}

QWidget* ManagerCataloguePage::createDiscountsTab()
{
    auto* page = new QWidget;
    auto* columns = new QHBoxLayout(page);
    columns->setContentsMargins(0, 0, 0, 0);
    columns->setSpacing(24);

    QVBoxLayout* seasonLayout;
    auto* seasonal = card(seasonLayout);
    columns->addWidget(seasonal, 3, Qt::AlignTop);
    seasonLayout->addWidget(caption("Seasonal discount", "catalogueTitle"));
    seasonLayout->addWidget(caption("Set the date range and percentage for one active season.", "catalogueHint"));
    seasonLayout->addSpacing(7);
    auto* selection = new QHBoxLayout;
    selection->setSpacing(12);
    seasonChoice_ = PosUi::choice("Season");
    auto* add = secondaryButton("Add Season");
    selection->addWidget(field("Season", seasonChoice_), 1);
    selection->addWidget(add, 0, Qt::AlignBottom);
    seasonLayout->addLayout(selection);

    auto* dates = new QHBoxLayout;
    dates->setSpacing(16);
    seasonStart_ = PosUi::dateEditor(QDate::currentDate());
    seasonEnd_ = PosUi::dateEditor(QDate::currentDate());
    seasonRate_ = new QLineEdit;
    dates->addWidget(field("Starts", seasonStart_), 1);
    dates->addWidget(field("Ends", seasonEnd_), 1);
    dates->addWidget(field("Discount (%)", seasonRate_), 1);
    seasonLayout->addLayout(dates);
    seasonActive_ = new QCheckBox("Enable this season");
    seasonLayout->addWidget(seasonActive_);
    seasonFeedback_ = caption({});
    seasonFeedback_->hide();
    seasonLayout->addWidget(seasonFeedback_);
    seasonLayout->addWidget(divider());
    auto* saveSeasonButton = PosUi::primaryButton("Save Season");
    seasonLayout->addWidget(saveSeasonButton, 0, Qt::AlignRight);

    QVBoxLayout* specialLayout;
    auto* special = card(specialLayout);
    columns->addWidget(special, 2, Qt::AlignTop);
    specialLayout->addWidget(caption("Special discount", "catalogueTitle"));
    specialLayout->addWidget(caption("Set a discount of up to 8% for eligible orders.", "catalogueHint"));
    specialLayout->addSpacing(7);
    specialRate_ = new QLineEdit;
    specialLayout->addWidget(field("Discount (%)", specialRate_));
    specialActive_ = new QCheckBox("Enable special discount");
    specialLayout->addWidget(specialActive_);
    specialFeedback_ = caption({});
    specialFeedback_->hide();
    specialLayout->addWidget(specialFeedback_);
    specialLayout->addWidget(divider());
    auto* saveSpecialButton = PosUi::primaryButton("Save Special Discount");
    saveSpecialButton->setMinimumWidth(177);
    specialLayout->addWidget(saveSpecialButton, 0, Qt::AlignRight);

    connect(seasonChoice_, &QComboBox::currentIndexChanged, this, &ManagerCataloguePage::showSeason);
    connect(add, &QPushButton::clicked, this, [this] {
        seasonChoice_->setCurrentIndex(0);
        showSeason(0);
        seasonStart_->setFocus();
    });
    connect(saveSeasonButton, &QPushButton::clicked, this, [this] {
        const auto rate = decimalInput(seasonRate_->text(), 2);
        if (!rate.success || rate.value > 10000 || !seasonStart_->hasAcceptableInput() ||
            !seasonEnd_->hasAcceptableInput() || seasonStart_->date() > seasonEnd_->date()) {
            message(*seasonFeedback_, "Enter a valid date range and a discount from 0% to 100%.", false);
            return;
        }
        saveSeason(selectedSeason_, {
            seasonStart_->date().toString(Qt::ISODate).toStdString(),
            seasonEnd_->date().toString(Qt::ISODate).toStdString(),
            static_cast<int32_t>(rate.value), seasonActive_->isChecked()
        });
    });
    connect(saveSpecialButton, &QPushButton::clicked, this, [this] {
        const auto rate = decimalInput(specialRate_->text(), 2);
        if (!rate.success || rate.value > 800) {
            message(*specialFeedback_, "Enter a special discount between 0% and 8%.", false);
            return;
        }
        saveSpecial({static_cast<int32_t>(rate.value), specialActive_->isChecked()});
    });
    return page;
}

void ManagerCataloguePage::refreshSeasons()
{
    const auto result = discounts_.listSeasons();
    if (!result.success) {
        message(*seasonFeedback_, result.message, false);
        return;
    }
    seasons_ = result.value;
    const QSignalBlocker blocker(seasonChoice_);
    seasonChoice_->clear();
    seasonChoice_->addItem("New season");
    int preferred = 0;
    for (const auto& season : seasons_) {
        const auto start = QDate::fromString(text(season.startDate), Qt::ISODate);
        const auto end = QDate::fromString(text(season.endDate), Qt::ISODate);
        seasonChoice_->addItem(start.toString("d MMM yyyy") + " – " + end.toString("d MMM yyyy") +
            (season.active ? " · Active" : ""));
        if (selectedSeason_ && season.startDate == selectedSeason_->startDate &&
            season.endDate == selectedSeason_->endDate) preferred = seasonChoice_->count() - 1;
    }
    seasonChoice_->setCurrentIndex(preferred);
    showSeason(preferred);
}

void ManagerCataloguePage::showSeason(int index)
{
    selectedSeason_ = index > 0 && static_cast<size_t>(index) <= seasons_.size() ?
        optional<Season>(seasons_[static_cast<size_t>(index - 1)]) : nullopt;
    const auto today = QDate::currentDate();
    seasonStart_->setDate(selectedSeason_ ? QDate::fromString(text(selectedSeason_->startDate), Qt::ISODate) : today);
    seasonEnd_->setDate(selectedSeason_ ? QDate::fromString(text(selectedSeason_->endDate), Qt::ISODate) : today);
    seasonRate_->setText(fixed(selectedSeason_ ? selectedSeason_->rate : 0));
    seasonActive_->setChecked(selectedSeason_ && selectedSeason_->active);
    seasonFeedback_->hide();
}

void ManagerCataloguePage::refreshSpecial()
{
    const auto result = discounts_.getSpecial();
    if (!result.success) {
        message(*specialFeedback_, result.message, false);
        return;
    }
    specialRate_->setText(fixed(result.value.rate));
    specialActive_->setChecked(result.value.active);
}

void ManagerCataloguePage::saveSeason(const optional<Season>& original, const Season& replacement)
{
    const auto result = discounts_.saveSeason(original, replacement);
    if (result.success) {
        selectedSeason_ = replacement;
        refreshSeasons();
    }
    message(*seasonFeedback_, result.success ? "Season saved." : result.message, result.success);
}

void ManagerCataloguePage::saveSpecial(const SpecialDiscount& special)
{
    const auto result = discounts_.saveSpecial(special);
    if (result.success) refreshSpecial();
    message(*specialFeedback_, result.success ? "Special discount saved." : result.message, result.success);
}

void ManagerCataloguePage::showProductMessage(const string& status, bool success)
{
    message(*feedback_, status, success);
}

void ManagerCataloguePage::showCategoryMessage(const string& status, bool success)
{
    message(*categoryFeedback_, status, success);
}
