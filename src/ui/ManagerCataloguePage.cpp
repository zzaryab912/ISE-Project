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
#include <QListWidget>
#include <QMessageBox>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QStyledItemDelegate>
#include <QTabBar>
#include <QTableWidget>
#include <QVBoxLayout>

#include <limits>
#include <tuple>

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

constexpr int productRowInset = 16;
constexpr int availableColumnWidth = 124;
constexpr int priceColumnWidth = 152;

enum ProductRowRole {
    AcronymRole = Qt::UserRole,
    AvailableRole,
    PriceRole,
    StockStatusRole
};

class ProductRows final : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    QSize sizeHint(const QStyleOptionViewItem&, const QModelIndex&) const override
    {
        return {0, 112};
    }

    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override
    {
        const QRect row = option.rect;
        const bool selected = option.state.testFlag(QStyle::State_Selected);
        const bool hovered = option.state.testFlag(QStyle::State_MouseOver);
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        if (selected || hovered) {
            painter->setPen(selected ? QColor("#d9e1df") : QColor("#ecefef"));
            painter->setBrush(selected ? QColor("#f1f5f4") : QColor("#fafbfb"));
            painter->drawRoundedRect(row.adjusted(0, 4, -1, -5), 11, 11);
        }
        if (index.row() + 1 < index.model()->rowCount()) {
            painter->setPen(QColor("#e7e9e9"));
            painter->drawLine(row.left() + 1, row.bottom(), row.right() - 1, row.bottom());
        }

        const int left = row.left() + productRowInset;
        const int price = row.right() + 1 - productRowInset - priceColumnWidth;
        const int available = price - availableColumnWidth;
        const int identityWidth = available - left - 12;
        QFont font = option.font;
        font.setPixelSize(15);
        font.setWeight(QFont::Medium);
        painter->setFont(font);
        painter->setPen(QColor("#242424"));
        const auto title = painter->fontMetrics().elidedText(index.data().toString(), Qt::ElideRight, identityWidth);
        painter->drawText(QRect(left, row.top() + 20, identityWidth, 22),
                          Qt::AlignLeft | Qt::AlignVCenter, title);
        font.setWeight(QFont::Normal);
        painter->setFont(font);
        for (const auto& [role, x, width] : {std::tuple{AvailableRole, available, availableColumnWidth},
                                             std::tuple{PriceRole, price, priceColumnWidth}}) {
            const auto value = painter->fontMetrics().elidedText(index.data(role).toString(), Qt::ElideRight, width);
            painter->drawText(QRect(x, row.top() + 20, width, 22), Qt::AlignRight | Qt::AlignVCenter, value);
        }
        font.setPixelSize(12);
        painter->setFont(font);
        painter->setPen(QColor("#686868"));
        painter->drawText(QRect(left, row.top() + 46, identityWidth, 18), Qt::AlignVCenter,
                          index.data(AcronymRole).toString());
        painter->drawText(QRect(left, row.top() + 71, identityWidth, 18), Qt::AlignVCenter,
                          index.data(StockStatusRole).toString());
        painter->restore();
    }
};

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
    rows->horizontalHeader()->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    rows->verticalHeader()->hide();
    rows->verticalHeader()->setDefaultSectionSize(44);
    rows->setEditTriggers(QAbstractItemView::NoEditTriggers);
    rows->setSelectionBehavior(QAbstractItemView::SelectRows);
    rows->setSelectionMode(QAbstractItemView::SingleSelection);
    rows->setShowGrid(false);
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
        QLabel#catalogueTitle { font-size:20px; font-weight:500; }
        QLabel#catalogueHint { color:#656565; }
        QFrame#catalogueCard { background:white; border:1px solid #e3e3e3; border-radius:21px; }
        QFrame#catalogueDivider { background:#e6e6e6; border:none; }
        QTabBar::tab { background:transparent; padding:0 20px 0 0; min-height:34px;
            color:#686868; border-bottom:2px solid transparent; margin-right:14px; }
        QTabBar::tab:selected { border-bottom-color:#242424; font-weight:500; color:#242424; }
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
        QCheckBox::indicator { width:16px; height:16px; border-radius:6px;
            background:#ffffff; border:1px solid #a0abaa; }
        QCheckBox::indicator:checked { background:#4e6766; border:3px solid #e5ebea; border-radius:7px; }
        QCheckBox::indicator:hover { border-color:#466765; }
        QTableWidget { border:none; background:white; color:#242424; outline:none;
            selection-background-color:#e8eeee; selection-color:#242424; }
        QTableWidget::item { color:#242424; padding:0 10px; border:none; border-bottom:1px solid #e9e9e9; }
        QTableWidget::item:selected { background:#eef2f2; color:#242424; }
        QHeaderView::section { background:white; border:none; border-bottom:1px solid #e3e3e3;
            padding:9px 10px; color:#656565; font-size:13px; font-weight:500; }
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
    productListLayout->addWidget(divider());
    auto* productHeadings = new QHBoxLayout;
    productHeadings->setContentsMargins(productRowInset, 0, productRowInset, 0);
    productHeadings->setSpacing(0);
    productHeadings->addWidget(caption("Product", "catalogueHint"), 1);
    auto* availableHeading = caption("Available", "catalogueHint");
    availableHeading->setFixedWidth(availableColumnWidth);
    availableHeading->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    productHeadings->addWidget(availableHeading);
    auto* priceHeading = caption("Unit price", "catalogueHint");
    priceHeading->setFixedWidth(priceColumnWidth);
    priceHeading->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    productHeadings->addWidget(priceHeading);
    productListLayout->addLayout(productHeadings);
    productList_ = new QListWidget;
    productList_->setAccessibleName("Products and stock");
    productList_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    productList_->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    productList_->setUniformItemSizes(true);
    productList_->setMouseTracking(true);
    productList_->setItemDelegate(new ProductRows(productList_));
    productList_->setStyleSheet(
        "QListWidget {background:white; border:0; outline:0;}"
        "QScrollBar:vertical {background:transparent; width:8px; margin-left:2px;}"
        "QScrollBar::handle:vertical {background:#d2d8d7; border-radius:3px; min-height:25px;}"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {height:0;}");
    connect(productList_->verticalScrollBar(), &QScrollBar::rangeChanged, this,
            [productHeadings, scrollbar = productList_->verticalScrollBar()](int min, int max) {
        const int width = max > min ? scrollbar->sizeHint().width() : 0;
        productHeadings->setContentsMargins(productRowInset, 0, productRowInset + width, 0);
    });
    productListLayout->addWidget(productList_, 1);
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

    connect(productList_, &QListWidget::currentRowChanged, this, [this](int row) {
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
        productList_->setCurrentRow(-1);
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
    const QSignalBlocker productBlocker(productList_);
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
    const int scrollPosition = productList_->verticalScrollBar()->value();
    productList_->clear();
    const ProductStock* currentProduct = nullptr;
    for (const auto& item : catalogue_) {
        const auto& product = item.product;
        auto* row = new QListWidgetItem(text(product.name), productList_);
        const auto available = quantity(item.stock.available, product.unit);
        const auto price = "PKR " + fixed(product.unitPrice);
        row->setData(AcronymRole, text(product.acronym));
        row->setData(AvailableRole, available);
        row->setData(PriceRole, price);
        if (!item.stock.available)
            row->setData(StockStatusRole, item.stock.onHand ? "Fully reserved" : "Out of stock");
        row->setData(Qt::AccessibleDescriptionRole, text(product.acronym) + " · Available " + available +
                     " · " + price + " per " + (product.unit == Unit::Count ? "item" : "kg"));
        row->setToolTip(row->text() + "\nAvailable " + available + " · " + price);
        if (product.id == productId) {
            productList_->setCurrentItem(row);
            currentProduct = &item;
        }
    }
    productList_->verticalScrollBar()->setValue(scrollPosition);
    if (currentProduct) productList_->scrollToItem(productList_->currentItem());
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

    auto* seasonal = new QVBoxLayout;
    seasonal->setSpacing(24);
    columns->addLayout(seasonal, 3);
    QVBoxLayout* savedLayout;
    seasonal->addWidget(card(savedLayout));
    savedLayout->addWidget(caption("Saved seasons", "catalogueTitle"));
    savedLayout->addSpacing(4);
    seasonChoice_ = PosUi::choice("Season");
    seasonChoice_->setPlaceholderText("No saved seasons");
    savedLayout->addWidget(field("Season", seasonChoice_));
    addSeasonEditor(*savedLayout, existingSeason_, false);

    QVBoxLayout* createLayout;
    seasonal->addWidget(card(createLayout));
    createLayout->addWidget(caption("Create a season", "catalogueTitle"));
    createLayout->addWidget(caption("Only one season can be enabled at a time.", "catalogueHint"));
    createLayout->addSpacing(4);
    addSeasonEditor(*createLayout, newSeason_, true);
    seasonal->addStretch(1);

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

void ManagerCataloguePage::addSeasonEditor(QVBoxLayout& layout, SeasonEditor& editor, bool creating)
{
    const auto today = QDate::currentDate();
    editor.form = new QWidget;
    editor.form->setAccessibleName(creating ? "New season form" : "Saved season form");
    auto* formLayout = new QVBoxLayout(editor.form);
    formLayout->setContentsMargins(0, 0, 0, 0);
    formLayout->setSpacing(9);
    auto* dates = new QHBoxLayout;
    dates->setSpacing(16);
    editor.start = PosUi::dateEditor(today);
    editor.end = PosUi::dateEditor(today);
    editor.rate = new QLineEdit("0.00");
    dates->addWidget(field("Starts", editor.start), 1);
    dates->addWidget(field("Ends", editor.end), 1);
    dates->addWidget(field("Discount (%)", editor.rate), 1);
    formLayout->addLayout(dates);
    editor.enabled = new QCheckBox("Enable this season");
    formLayout->addWidget(editor.enabled);
    formLayout->addWidget(divider());
    auto* save = PosUi::primaryButton(creating ? "Create Season" : "Save Changes");
    formLayout->addWidget(save, 0, Qt::AlignRight);
    layout.addWidget(editor.form);
    editor.feedback = caption({});
    editor.feedback->hide();
    layout.addWidget(editor.feedback);
    connect(save, &QPushButton::clicked, this, [this, creating] {
        if (creating) submitSeason(newSeason_, nullopt);
        else if (selectedSeason_) submitSeason(existingSeason_, selectedSeason_);
    });
}

void ManagerCataloguePage::submitSeason(SeasonEditor& editor, const optional<Season>& original)
{
    const auto rate = decimalInput(editor.rate->text(), 2);
    if (!rate.success || rate.value > 10000 || !editor.start->hasAcceptableInput() ||
        !editor.end->hasAcceptableInput() || editor.start->date() > editor.end->date()) {
        message(*editor.feedback, "Enter a valid date range and a discount from 0% to 100%.", false);
        return;
    }
    saveSeason(original, {
        editor.start->date().toString(Qt::ISODate).toStdString(),
        editor.end->date().toString(Qt::ISODate).toStdString(),
        static_cast<int32_t>(rate.value), editor.enabled->isChecked()
    });
}

void ManagerCataloguePage::refreshSeasons(bool keepDraft)
{
    const auto result = discounts_.listSeasons();
    if (!result.success) {
        message(*existingSeason_.feedback, result.message, false);
        return;
    }
    seasons_ = result.value;
    const QSignalBlocker blocker(seasonChoice_);
    seasonChoice_->clear();
    int preferred = -1;
    int active = -1;
    for (const auto& season : seasons_) {
        const auto start = QDate::fromString(text(season.startDate), Qt::ISODate);
        const auto end = QDate::fromString(text(season.endDate), Qt::ISODate);
        seasonChoice_->addItem(start.toString("d MMM yyyy") + " – " + end.toString("d MMM yyyy") +
            (season.active ? " · Active" : ""));
        if (season.active) active = seasonChoice_->count() - 1;
        if (selectedSeason_ && season.startDate == selectedSeason_->startDate &&
            season.endDate == selectedSeason_->endDate) preferred = seasonChoice_->count() - 1;
    }
    if (preferred < 0 && !seasons_.empty()) preferred = active >= 0 ? active : 0;
    seasonChoice_->setEnabled(!seasons_.empty());
    seasonChoice_->setCurrentIndex(preferred);
    if (keepDraft && selectedSeason_ && preferred >= 0) {
        const auto& current = seasons_[static_cast<size_t>(preferred)];
        if (current.startDate == selectedSeason_->startDate && current.endDate == selectedSeason_->endDate &&
            current.rate == selectedSeason_->rate && current.active == selectedSeason_->active) return;
    }
    showSeason(preferred);
}

void ManagerCataloguePage::showSeason(int index)
{
    selectedSeason_ = index >= 0 && static_cast<size_t>(index) < seasons_.size() ?
        optional<Season>(seasons_[static_cast<size_t>(index)]) : nullopt;
    existingSeason_.form->setVisible(selectedSeason_.has_value());
    existingSeason_.feedback->hide();
    if (!selectedSeason_) return;
    existingSeason_.start->setDate(QDate::fromString(text(selectedSeason_->startDate), Qt::ISODate));
    existingSeason_.end->setDate(QDate::fromString(text(selectedSeason_->endDate), Qt::ISODate));
    existingSeason_.rate->setText(fixed(selectedSeason_->rate));
    existingSeason_.enabled->setChecked(selectedSeason_->active);
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
    const bool editing = original.has_value();
    const auto result = discounts_.saveSeason(original, replacement);
    if (result.success) {
        if (editing) selectedSeason_ = replacement;
        refreshSeasons(!editing);
        if (!editing) {
            const auto today = QDate::currentDate();
            newSeason_.start->setDate(today);
            newSeason_.end->setDate(today);
            newSeason_.rate->setText("0.00");
            newSeason_.enabled->setChecked(false);
        }
    }
    auto* feedback = editing ? existingSeason_.feedback : newSeason_.feedback;
    message(*feedback, result.success ? (editing ? "Changes saved." : "Season created.") : result.message,
            result.success);
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
