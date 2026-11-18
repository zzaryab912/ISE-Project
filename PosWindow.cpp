#include "PosWindow.h"
#include "BarcodeAssetService.h"
#include "BarcodePlotter.h"
#include "CartService.h"
#include "CheckoutService.h"
#include "HistoryService.h"
#include "PosScanner.h"
#include "ProductService.h"

#include <QCloseEvent>
#include <QComboBox>
#include <QDateTime>
#include <QDir>
#include <QFormLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QScopedValueRollback>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QTabWidget>
#include <QTableWidget>
#include <QVBoxLayout>

#include <limits>

namespace {

QString text(const string& value) { return QString::fromStdString(value); }

QString fixedText(int64_t value, int scale, int places)
{
    return QString::number(value / scale) + "." +
        QString::number(value % scale).rightJustified(places, '0');
}

QString money(int64_t value) { return fixedText(value, 100, 2); }

QString quantityText(int64_t value, Unit unit)
{
    return unit == Unit::Count ? QString::number(value / 1000)
                               : fixedText(value, 1000, 3);
}

Result<int64_t> fixedInput(const QString& input, int places)
{
    static const QRegularExpression pattern(R"(^([0-9]+)(?:\.([0-9]+))?$)");
    const auto match = pattern.match(input.trimmed());
    const QString fraction = match.captured(2);
    if (!match.hasMatch() || fraction.size() > places) {
        return {false, "Enter a nonnegative number with at most " + to_string(places) +
                       " decimal places.", 0};
    }
    bool valid = false;
    const auto digits = match.captured(1) + fraction.leftJustified(places, '0');
    const auto value = digits.toLongLong(&valid);
    return valid ? Result<int64_t>{true, {}, value}
                 : Result<int64_t>{false, "The number is too large.", 0};
}

Result<int64_t> quantityInput(const QString& input, Unit unit)
{
    auto result = fixedInput(input, 3);
    if (result.success && unit == Unit::Count && result.value % 1000 != 0) {
        return {false, "Enter a whole quantity for Count.", 0};
    }
    return result;
}

QString statusText(OrderStatus status)
{
    switch (status) {
    case OrderStatus::OnHold: return "On hold";
    case OrderStatus::Paid: return "Paid";
    case OrderStatus::Archived: return "Archived";
    case OrderStatus::InProgress: return "In progress";
    }
    return {};
}

QString totalsText(const Totals& totals)
{
    return QString("Subtotal  %1     Tax  %2     Discount  %3\nPayable  PKR %4")
        .arg(money(totals.subtotal), money(totals.tax), money(totals.discount), money(totals.payable));
}

QLabel* wrappedLabel()
{
    auto* label = new QLabel;
    label->setTextFormat(Qt::PlainText);
    label->setWordWrap(true);
    return label;
}

void showImage(QLabel& label, const QImage& image)
{
    if (image.isNull()) {
        label.clear();
    } else {
        label.setPixmap(QPixmap::fromImage(image).scaled(
            label.contentsRect().size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
    }
}

class PaymentFields final : public QGroupBox {
public:
    PaymentFields() : QGroupBox("Payment")
    {
        auto* form = new QFormLayout(this);
        form->addRow("Amount (PKR)", amount_);
        mode_->addItem("Cash", static_cast<int>(PaymentMode::Cash));
        mode_->addItem("Card", static_cast<int>(PaymentMode::Card));
        mode_->addItem("Wallet", static_cast<int>(PaymentMode::Wallet));
        form->addRow("Method", mode_);
        form->addRow("Merchant (optional)", merchant_);
    }

    void setAmount(int64_t amount) { amount_->setText(money(amount)); }

    Result<Payment> read() const
    {
        const auto amount = fixedInput(amount_->text(), 2);
        if (!amount.success) return {false, "Payment amount: " + amount.message, {}};
        return {true, {}, {true, amount.value, static_cast<PaymentMode>(mode_->currentData().toInt()),
                            merchant_->text().trimmed().toStdString()}};
    }

private:
    QLineEdit* amount_ = new QLineEdit;
    QComboBox* mode_ = new QComboBox;
    QLineEdit* merchant_ = new QLineEdit;
};

class CurrentOrderPage final : public PosWindow::CurrentOrderView {
public:
    explicit CurrentOrderPage(PosWindow& window)
    {
        auto* body = new QHBoxLayout(this);
        auto* cart = new QVBoxLayout;
        body->addLayout(cart, 3);
        cart->addWidget(new QLabel("Current Order"));
        table_->setHorizontalHeaderLabels({"Product", "Unit", "Quantity", "Amount (PKR)"});
        table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
        table_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
        table_->setSelectionBehavior(QAbstractItemView::SelectRows);
        table_->setSelectionMode(QAbstractItemView::SingleSelection);
        table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
        cart->addWidget(table_, 1);

        auto* adjust = new QHBoxLayout;
        auto* change = new QPushButton("Set quantity");
        auto* remove = new QPushButton("Remove");
        adjust->addWidget(rowQuantity_);
        adjust->addWidget(change);
        adjust->addWidget(remove);
        cart->addLayout(adjust);
        cart->addWidget(cartError_);
        cart->addWidget(totals_);

        auto* customer = new QFormLayout;
        customer->addRow("Customer name", name_);
        customer->addRow("Contact (optional)", contact_);
        cart->addLayout(customer);
        cart->addWidget(payment_);
        cart->addWidget(billError_);
        auto* checkout = new QHBoxLayout;
        checkout->addWidget(pay_);
        checkout->addWidget(hold_);
        cart->addLayout(checkout);

        auto* scanner = new QVBoxLayout;
        body->addLayout(scanner, 2);
        scanner->addWidget(new QLabel("Camera"));
        preview_->setMinimumHeight(170);
        preview_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Expanding);
        preview_->setAlignment(Qt::AlignCenter);
        preview_->setStyleSheet("background: #18243a; color: white;");
        scanner->addWidget(preview_, 1);
        scanner->addWidget(scannerMessage_);
        scanner->addWidget(productName_);
        auto* addForm = new QFormLayout;
        addForm->addRow("Quantity to add", addQuantity_);
        scanner->addLayout(addForm);
        auto* description = description_;
        description->setCheckable(true);
        description->setEnabled(false);
        scanner->addWidget(description);
        scanner->addWidget(specifications_);
        specifications_->hide();
        scanner->addWidget(add_);
        productName_->setText("Scan a product to start.");
        addQuantity_->setEnabled(false);
        add_->setEnabled(false);
        rowQuantity_->setEnabled(false);
        change->setEnabled(false);
        remove->setEnabled(false);
        setBillSaving(false);

        connect(description, &QPushButton::toggled, specifications_, &QLabel::setVisible);
        connect(addQuantity_, &QLineEdit::textChanged, this, [this] {
            const auto quantity = readAddQuantity();
            add_->setEnabled(quantity.success);
        });
        connect(add_, &QPushButton::clicked, &window, &PosWindow::onAddClicked);
        connect(pay_, &QPushButton::clicked, &window, &PosWindow::onGenerateBillClicked);
        connect(hold_, &QPushButton::clicked, &window, &PosWindow::onGenerateAndHoldClicked);
        connect(table_, &QTableWidget::itemSelectionChanged, this, [this, change, remove] {
            const int row = table_->currentRow();
            const auto* quantity = row >= 0 ? table_->item(row, 2) : nullptr;
            const bool selected = quantity && quantity->isSelected();
            rowQuantity_->setEnabled(selected);
            change->setEnabled(selected);
            remove->setEnabled(selected);
            rowQuantity_->setText(selected ? quantity->text() : QString{});
        });
        connect(change, &QPushButton::clicked, this, [this, &window] {
            const int row = table_->currentRow();
            if (row < 0 || row >= static_cast<int>(items_.size())) return;
            const Product product = items_[row].product;
            const auto quantity = quantityInput(rowQuantity_->text(), product.unit);
            if (!quantity.success) {
                showCartError(quantity.message);
                return;
            }
            window.onCartQuantityChanged(product.id, quantity.value);
        });
        connect(remove, &QPushButton::clicked, this, [this, &window] {
            const int row = table_->currentRow();
            if (row < 0 || row >= static_cast<int>(items_.size())) return;
            const string id = items_[row].product.id;
            window.onCartQuantityChanged(id, 0);
        });
    }

    Result<Customer> readCustomer() const override
    {
        const QString name = name_->text().trimmed();
        if (name.isEmpty()) return {false, "Enter the customer's name.", {}};
        return {true, {}, {name.toStdString(), contact_->text().trimmed().toStdString()}};
    }
    Result<Payment> readPayment() const override { return payment_->read(); }
    Result<int64_t> readAddQuantity() const override
    {
        const auto checked = quantityInput(addQuantity_->text(), selectedUnit_);
        if (!checked.success) return checked;
        if (checked.value == 0) return {false, "Enter a positive quantity.", 0};
        return checked;
    }
    void showCameraPreview(const QImage& image) override { showImage(*preview_, image); }
    void showScannerMessage(const string& message) override { scannerMessage_->setText(text(message)); }
    void showScannedProduct(const Product& product) override
    {
        selectedUnit_ = product.unit;
        const QString unit = product.unit == Unit::Count ? "Count" : "Weight (kg)";
        productName_->setText(text(product.name) + " · " + text(product.barcode));
        specifications_->setText(
            QString("%1 · %2\nIn stock %3\nPrice PKR %4 · Tax PKR %5 · Discount PKR %6 per unit")
                .arg(text(product.acronym), unit, quantityText(product.available, product.unit),
                     money(product.unitPrice), money(product.taxPerUnit), money(product.discountPerUnit)));
        addQuantity_->setEnabled(true);
        addQuantity_->setPlaceholderText(unit);
        description_->setEnabled(true);
    }
    void resetAddQuantity() override
    {
        addQuantity_->clear();
        add_->setEnabled(false);
    }
    void showCart(const vector<CartItem>& items, const vector<optional<int64_t>>& amounts,
                  const optional<Totals>& totals) override
    {
        const int oldRow = table_->currentRow();
        const string selection = oldRow >= 0 && oldRow < static_cast<int>(items_.size())
            ? items_[oldRow].product.id : string{};
        table_->clearContents();
        items_ = items;
        table_->setRowCount(static_cast<int>(items.size()));
        int selectedRow = -1;
        for (int row = 0; row < table_->rowCount(); ++row) {
            const CartItem& item = items[row];
            table_->setItem(row, 0, new QTableWidgetItem(text(item.product.name)));
            table_->setItem(row, 1, new QTableWidgetItem(item.product.unit == Unit::Count ? "Count" : "kg"));
            table_->setItem(row, 2, new QTableWidgetItem(quantityText(item.quantity, item.product.unit)));
            table_->setItem(row, 3, new QTableWidgetItem(amounts[row] ? money(*amounts[row]) : QString("—")));
            if (item.product.id == selection) selectedRow = row;
        }
        table_->setCurrentCell(selectedRow, 0);
        if (selectedRow >= 0) rowQuantity_->setText(table_->item(selectedRow, 2)->text());
        totals_->setText(totals ? totalsText(*totals) : "Totals unavailable");
        if (totals) payment_->setAmount(totals->payable);
        canBill_ = !items.empty() && totals.has_value();
        setBillSaving(false);
    }
    void showCartError(const string& message) override { cartError_->setText(text(message)); }
    void showBillError(const string& message) override { billError_->setText(text(message)); }
    void setBillSaving(bool saving) override
    {
        pay_->setEnabled(!saving && canBill_);
        hold_->setEnabled(!saving && canBill_);
    }

private:
    QTableWidget* table_ = new QTableWidget(0, 4);
    QLabel* preview_ = wrappedLabel();
    QLabel* scannerMessage_ = wrappedLabel();
    QLabel* productName_ = wrappedLabel();
    QLabel* specifications_ = wrappedLabel();
    QLabel* cartError_ = wrappedLabel();
    QLabel* billError_ = wrappedLabel();
    QLabel* totals_ = wrappedLabel();
    QLineEdit* name_ = new QLineEdit;
    QLineEdit* contact_ = new QLineEdit;
    QLineEdit* addQuantity_ = new QLineEdit;
    QLineEdit* rowQuantity_ = new QLineEdit;
    QPushButton* description_ = new QPushButton("Description");
    QPushButton* add_ = new QPushButton("Add");
    QPushButton* pay_ = new QPushButton("Generate Bill");
    QPushButton* hold_ = new QPushButton("Generate Bill and Put on Hold");
    PaymentFields* payment_ = new PaymentFields;
    vector<CartItem> items_;
    Unit selectedUnit_ = Unit::Count;
    bool canBill_ = false;
};

class HistoryPage final : public PosWindow::OrderHistoryView {
public:
    explicit HistoryPage(PosWindow& window)
    {
        auto* body = new QHBoxLayout(this);
        auto* listColumn = new QVBoxLayout;
        listColumn->addWidget(new QLabel("Order History"));
        listColumn->addWidget(list_, 1);
        body->addLayout(listColumn, 2);
        auto* details = new QVBoxLayout;
        details->addWidget(new QLabel("Saved bill"));
        bill_->setReadOnly(true);
        bill_->setPlaceholderText("Select an order to view its bill.");
        details->addWidget(bill_, 1);
        details->addWidget(payment_);
        details->addWidget(error_);
        auto* actions = new QHBoxLayout;
        actions->addWidget(paid_);
        actions->addWidget(archive_);
        details->addLayout(actions);
        body->addLayout(details, 3);
        clearBill();

        connect(list_, &QListWidget::currentItemChanged, this, [&window](QListWidgetItem* item) {
            if (item) window.onOrderSelected(item->data(Qt::UserRole).toString().toStdString());
        });
        connect(paid_, &QPushButton::clicked, &window, &PosWindow::onMarkPaidClicked);
        connect(archive_, &QPushButton::clicked, &window, &PosWindow::onArchiveClicked);
    }

    Result<Payment> readPayment() const override { return payment_->read(); }
    void showError(const string& message) override { error_->setText(text(message)); }
    void showOrders(const vector<SaleRecord>& orders, const string& selectedId) override
    {
        const QSignalBlocker blocker(list_);
        list_->clear();
        for (const SaleRecord& order : orders) {
            auto* item = new QListWidgetItem(
                QString("%1 · %2\n%3 · %4").arg(text(order.ids.orderId), text(order.customer.name),
                      text(order.timestamp.date), statusText(order.status)), list_);
            item->setData(Qt::UserRole, text(order.ids.orderId));
            if (order.ids.orderId == selectedId) list_->setCurrentItem(item);
        }
    }
    void showBill(const SaleRecord& order) override
    {
        QString contents = QString("Bill %1\nOrder %2 · %3 %4\n%5\nCustomer: %6")
            .arg(text(order.ids.billId), text(order.ids.orderId), text(order.timestamp.date),
                 text(order.timestamp.time), statusText(order.status), text(order.customer.name));
        if (!order.customer.contact.empty()) contents += "\nContact: " + text(order.customer.contact);
        contents += "\n\n";
        for (const SaleLine& line : order.lines) {
            contents += QString("%1  × %2   |   PKR %3 / unit   |   Discount %4 / unit\n")
                .arg(text(line.acronym), quantityText(line.quantity, Unit::Weight),
                     money(line.unitPrice), money(line.discountPerUnit));
        }
        contents += "\n" + totalsText(order.totals);
        if (order.payment.recorded) {
            const QString method = order.payment.mode == PaymentMode::Cash ? "Cash" :
                order.payment.mode == PaymentMode::Card ? "Card" : "Wallet";
            contents += QString("\nPayment: PKR %1 · %2").arg(money(order.payment.amount), method);
            if (!order.payment.merchant.empty()) contents += " · " + text(order.payment.merchant);
        }
        bill_->setPlainText(contents);
        const bool held = order.status == OrderStatus::OnHold;
        payment_->setVisible(held);
        if (held) payment_->setAmount(order.totals.payable);
        paid_->setVisible(held);
        archive_->setEnabled(order.status != OrderStatus::Archived);
    }
    void clearBill() override
    {
        bill_->clear();
        payment_->hide();
        paid_->hide();
        archive_->setEnabled(false);
    }

private:
    QListWidget* list_ = new QListWidget;
    QPlainTextEdit* bill_ = new QPlainTextEdit;
    QLabel* error_ = wrappedLabel();
    PaymentFields* payment_ = new PaymentFields;
    QPushButton* paid_ = new QPushButton("Mark as Paid");
    QPushButton* archive_ = new QPushButton("Archive");
};

class ProductFields final : public QWidget {
public:
    explicit ProductFields(bool editing)
    {
        auto* form = new QFormLayout(this);
        form->addRow("Barcode", barcode);
        form->addRow("Acronym", acronym_);
        form->addRow("Name", name_);
        unit_->addItem("Count", static_cast<int>(Unit::Count));
        unit_->addItem("Weight (kg)", static_cast<int>(Unit::Weight));
        form->addRow("Unit", unit_);
        form->addRow("Unit price (PKR)", price_);
        form->addRow("Tax per unit (PKR)", tax_);
        form->addRow("Discount per unit (PKR)", discount_);
        form->addRow("Stock", stock_);
        barcode->setReadOnly(editing);
        tax_->setText("0.00");
        discount_->setText("0.00");
    }

    Result<Product> read() const
    {
        Product product;
        product.barcode = barcode->text().trimmed().toStdString();
        product.acronym = acronym_->text().trimmed().toStdString();
        product.name = name_->text().trimmed().toStdString();
        product.unit = static_cast<Unit>(unit_->currentData().toInt());
        const auto price = fixedInput(price_->text(), 2);
        const auto tax = fixedInput(tax_->text(), 2);
        const auto discount = fixedInput(discount_->text(), 2);
        const auto stock = quantityInput(stock_->text(), product.unit);
        if (!price.success) return {false, "Unit price: " + price.message, {}};
        if (!tax.success) return {false, "Tax: " + tax.message, {}};
        if (!discount.success) return {false, "Discount: " + discount.message, {}};
        if (!stock.success) return {false, "Stock: " + stock.message, {}};
        product.unitPrice = price.value;
        product.taxPerUnit = tax.value;
        product.discountPerUnit = discount.value;
        product.available = stock.value;
        return {true, {}, product};
    }

    void showProduct(const Product& product)
    {
        barcode->setText(text(product.barcode));
        acronym_->setText(text(product.acronym));
        name_->setText(text(product.name));
        unit_->setCurrentIndex(unit_->findData(static_cast<int>(product.unit)));
        price_->setText(money(product.unitPrice));
        tax_->setText(money(product.taxPerUnit));
        discount_->setText(money(product.discountPerUnit));
        stock_->setText(quantityText(product.available, product.unit));
    }

    QLineEdit* const barcode = new QLineEdit;

private:
    QLineEdit* acronym_ = new QLineEdit;
    QLineEdit* name_ = new QLineEdit;
    QComboBox* unit_ = new QComboBox;
    QLineEdit* price_ = new QLineEdit;
    QLineEdit* tax_ = new QLineEdit;
    QLineEdit* discount_ = new QLineEdit;
    QLineEdit* stock_ = new QLineEdit;
};

class CataloguePage final : public PosWindow::CatalogueView {
public:
    explicit CataloguePage(PosWindow& window)
    {
        auto* page = new QVBoxLayout(this);
        page->addWidget(tabs_, 1);
        page->addWidget(message_);
        const auto previewLabel = [](QLabel& label) {
            label.setAlignment(Qt::AlignCenter);
            label.setFixedHeight(100);
            label.setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
        };
        previewLabel(*savedImage_);
        previewLabel(*newImage_);

        auto* catalogue = new QWidget;
        auto* listLayout = new QHBoxLayout(catalogue);
        listLayout->addWidget(list_, 2);
        auto* detail = new QVBoxLayout;
        detail->addWidget(metadata_);
        detail->addWidget(savedImage_);
        detail->addWidget(edit_);
        auto* actions = new QHBoxLayout;
        actions->addWidget(update_);
        actions->addWidget(remove_);
        detail->addLayout(actions);
        detail->addStretch();
        listLayout->addLayout(detail, 3);
        tabs_->addTab(catalogue, "View Catalogue");

        auto* add = new QWidget;
        auto* addLayout = new QVBoxLayout(add);
        addLayout->addWidget(create_);
        addLayout->addWidget(newImage_);
        auto* buttons = new QHBoxLayout;
        auto* preview = new QPushButton("Generate Preview");
        auto* save = new QPushButton("Save Product");
        buttons->addWidget(preview);
        buttons->addWidget(save);
        addLayout->addLayout(buttons);
        addLayout->addStretch();
        tabs_->addTab(add, "Add Product");
        clearProduct();

        const auto select = [&window](QListWidgetItem* item) {
            if (item) window.onProductSelected(item->data(Qt::UserRole).toString().toStdString());
        };
        connect(list_, &QListWidget::itemClicked, this, select);
        connect(list_, &QListWidget::itemActivated, this, select);
        connect(preview, &QPushButton::clicked, &window, &PosWindow::onGeneratePreview);
        connect(save, &QPushButton::clicked, &window, &PosWindow::onSaveProduct);
        connect(update_, &QPushButton::clicked, &window, &PosWindow::onUpdateProduct);
        connect(remove_, &QPushButton::clicked, &window, &PosWindow::onDeleteProduct);
        connect(create_->barcode, &QLineEdit::textChanged, this, [this] { newImage_->clear(); });
        connect(tabs_, &QTabWidget::currentChanged, this, [this] { showError({}); });
    }

    QTabWidget& tabs() const { return *tabs_; }
    string readBarcode() const override { return create_->barcode->text().trimmed().toStdString(); }
    Result<Product> readNewProduct() const override { return create_->read(); }
    Result<Product> readEditedProduct() const override { return edit_->read(); }
    void showError(const string& message) override { message_->setText(text(message)); }
    void showProducts(const vector<Product>& products, const string& selectedId) override
    {
        list_->clear();
        for (const Product& product : products) {
            const QString stock = product.available == 0 ? "Out of stock" :
                quantityText(product.available, product.unit) + (product.unit == Unit::Count ? " in stock" : " kg");
            auto* item = new QListWidgetItem(text(product.name) + "\n" + stock, list_);
            item->setData(Qt::UserRole, text(product.id));
            if (product.id == selectedId) list_->setCurrentItem(item);
        }
    }
    void showProduct(const Product& product) override
    {
        edit_->setEnabled(true);
        update_->setEnabled(true);
        remove_->setEnabled(true);
        edit_->showProduct(product);
        metadata_->setText("Product ID " + text(product.id) + "\nImage " + text(product.barcodeImagePath));
        const QDir root(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation));
        showImage(*savedImage_, QImage(root.filePath(text(product.barcodeImagePath))));
    }
    void clearProduct() override
    {
        edit_->showProduct({});
        edit_->setEnabled(false);
        update_->setEnabled(false);
        remove_->setEnabled(false);
        metadata_->setText("Select a product to view its specifications.");
        savedImage_->clear();
    }
    void showBarcodePreview(const QImage& image) override { showImage(*newImage_, image); }
    void showSavedProduct(const Product& product) override
    {
        message_->setText("Saved product " + text(product.id) + " · " + text(product.barcodeImagePath));
        create_->showProduct({});
    }

private:
    QTabWidget* tabs_ = new QTabWidget;
    QListWidget* list_ = new QListWidget;
    QLabel* message_ = wrappedLabel();
    QLabel* metadata_ = wrappedLabel();
    QLabel* savedImage_ = wrappedLabel();
    QLabel* newImage_ = wrappedLabel();
    ProductFields* edit_ = new ProductFields(true);
    ProductFields* create_ = new ProductFields(false);
    QPushButton* update_ = new QPushButton("Save Changes");
    QPushButton* remove_ = new QPushButton("Delete Product");
};

} // namespace

PosWindow::PosWindow(ProductService& products, BarcodeAssetService& images,
                     CartService& cart, PosScanner& scanner,
                     CheckoutService& checkout, HistoryService& history,
                     QWidget* parent)
    : QMainWindow(parent),
      products_(products),
      images_(images),
      cart_(cart),
      scanner_(scanner),
      checkout_(checkout),
      history_(history) {
    setWindowTitle("Eyeris POS");
    resize(1163, 760);
    auto* shell = new QWidget;
    auto* layout = new QVBoxLayout(shell);
    auto* navigation = new QHBoxLayout;
    auto* pages = new QStackedWidget;
    for (const string name : {"Current Order", "Order History", "Product Catalogue"}) {
        auto* button = new QPushButton(text(name));
        navigation->addWidget(button);
        connect(button, &QPushButton::clicked, this, [this, name] { showPage(name); });
    }
    layout->addLayout(navigation);
    layout->addWidget(pages, 1);
    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setWidget(shell);
    setCentralWidget(scroll);
    auto* currentOrder = new CurrentOrderPage(*this);
    auto* historyPage = new HistoryPage(*this);
    auto* catalogue = new CataloguePage(*this);
    bindPages(*pages, *currentOrder, *historyPage, *catalogue, catalogue->tabs());

    cameraTimer_.setInterval(200);
    cameraTimer_.setTimerType(Qt::CoarseTimer);
    connect(&cameraTimer_, &QTimer::timeout, this, &PosWindow::onCameraTick);
}

// gives PosWindow the real Qt pages when the interface is ready
void PosWindow::bindPages(QStackedWidget& pages, CurrentOrderView& currentOrder,
                          OrderHistoryView& history, CatalogueView& catalogue,
                          QTabWidget& catalogueTabs) {
    stopCurrentOrderCamera();
    pages_ = &pages;
    currentOrderPage_ = &currentOrder;
    historyPage_ = &history;
    cataloguePage_ = &catalogue;
    catalogueTabs_ = &catalogueTabs;

    for (QWidget* page : {currentOrderPage_, historyPage_, cataloguePage_}) {
        if (pages.indexOf(page) < 0) {
            pages.addWidget(page);
        }
    }
}

void PosWindow::onCameraTick() {                                        // called on each tick
    if (!currentOrderPage_) {
        return;
    }

    auto& view = *static_cast<CurrentOrderView*>(currentOrderPage_);
    const Result<ScanResult> scane = scanner_.poll();                   // poll asks scanner to read one frame and check for a barcode, returns a result object

    if (!scane.success) {       // stops the camera if reading the frame failed
        cameraStatus_ = {false, scane.message};
        cameraTimer_.stop();
        scanner_.stop();
        view.showCameraPreview({});
        view.showScannerMessage(cameraStatus_.message);
        return;
    }

    view.showCameraPreview(mat2QImage(scane.value.preview));
    if (!scane.value.decoded.found) {                                                        // keep showing the preview while waiting for a barcode
        view.showScannerMessage(scane.message);
        return;
    }
    if (!scane.value.codeChanged) {
        if (hasSelectedProduct_ && scane.value.decoded.code == selectedProduct_.barcode) {
            view.showScannerMessage({});
        }
        return;
    }

    const Result<Product> product = products_.findByBarcode(scane.value.decoded.code);      // looks up barcode in the catalogue. If there is no match, selection stays as it is
    if (!product.success) {
        view.showScannerMessage(product.message);
        return;
    }

    view.showScannerMessage({});
    if (hasSelectedProduct_ && selectedProduct_.id == product.value.id) {                   // preserves teh entered quantity if lookup returns product already selected
        return;
    }

    // for a different recognised product: this is the new selection now, and the addQuantity option is now set to zero again
    selectedProduct_ = product.value;
    hasSelectedProduct_ = true;
    addQuantity_ = 0;
    view.showScannedProduct(selectedProduct_);
    view.resetAddQuantity();
}

// when clicking on Add to order
void PosWindow::onAddClicked() {
    if (!currentOrderPage_ || !hasSelectedProduct_) {                                      // wait until a product is selected
        return;
    }

    auto& view = *static_cast<CurrentOrderView*>(currentOrderPage_);
    const Result<int64_t> quantity = view.readAddQuantity();
    if (!quantity.success) {
        view.showCartError(quantity.message);
        return;
    }
    addQuantity_ = quantity.value;

    const Status result = cart_.addByBarcode(selectedProduct_.barcode, addQuantity_);       // if add fails, it stops trying to add so cashier can change quantity manually. super neat right?
    if (!result.success) {
        view.showCartError(result.message);
        return;
    }

    addQuantity_ = 0;
    view.resetAddQuantity();
    view.showCartError(refreshCart());
}

// updates an existing row. zero removes the row here
void PosWindow::onCartQuantityChanged(const string& productId, int64_t quantity) {
    const Status result = cart_.setQuantity(productId, quantity);
    if (currentOrderPage_) {
        auto& view = *static_cast<CurrentOrderView*>(currentOrderPage_);
        const string refreshError = refreshCart();
        view.showCartError(result.success ? refreshError : result.message);
    }
}

// gets the actual rows and their amounts from the cart after a quantity change
string PosWindow::refreshCart() {
    auto& view = *static_cast<CurrentOrderView*>(currentOrderPage_);
    const vector<CartItem> items = cart_.items();
    vector<optional<int64_t>> amounts;
    amounts.reserve(items.size());
    string error;
    for (const CartItem& item : items) {
        const Result<Totals> line = cart_.lineTotals(item.product.id);
        if (line.success) {
            amounts.push_back(line.value.subtotal);
        } else {
            amounts.push_back(nullopt);
            if (error.empty()) {
                error = line.message;
            }
        }
    }

    const Result<Totals> totals = cart_.totals();
    view.showCart(items, amounts, totals.success ? optional<Totals>{totals.value} : nullopt);
    return totals.success || !error.empty() ? error : totals.message;
}

// generates and saves a paid bill using the entered payment details
void PosWindow::onGenerateBillClicked() {
    generateCurrentBill(OrderStatus::Paid);
}

// saves the complete bill on hold so it can be paid later
void PosWindow::onGenerateAndHoldClicked() {
    generateCurrentBill(OrderStatus::OnHold);
}

void PosWindow::generateCurrentBill(OrderStatus status) {
    if (!currentOrderPage_ || savingBill_) {
        return;
    }

    auto& view = *static_cast<CurrentOrderView*>(currentOrderPage_);
    const QScopedValueRollback<bool> saving(savingBill_, true);
    const Result<Customer> customer = view.readCustomer();
    if (!customer.success) {
        view.showBillError(customer.message);
        return;
    }

    Payment payment{};
    if (status == OrderStatus::Paid) {
        const Result<Payment> entered = view.readPayment();
        if (!entered.success) {
            view.showBillError(entered.message);
            return;
        }
        payment = entered.value;
        payment.recorded = true;
    }

    const QDateTime now = QDateTime::currentDateTime();
    const Timestamp timestamp{now.date().toString("yyyy-MM-dd").toStdString(),
                              now.time().toString("HH:mm:ss").toStdString()};
    view.showBillError({});
    view.setBillSaving(true);
    const Result<SaleRecord> result = checkout_.generateBill(cart_.items(), customer.value, payment, status, timestamp);
    if (!result.success) {
        view.showBillError(result.message);
        view.setBillSaving(false);
        return;
    }

    cart_.clear();
    addQuantity_ = 0;
    view.resetAddQuantity();
    view.showCartError(refreshCart());
    view.setBillSaving(false);
    displayOrder(result.value);
    showPage("Order History");
}

// loads the saved bill for the selected order id
void PosWindow::onOrderSelected(const string& orderId) {
    const Result<SaleRecord> result = history_.get(orderId);
    if (!result.success) {
        clearOrderSelection();
        if (historyPage_) {
            static_cast<OrderHistoryView*>(historyPage_)->showError(result.message);
        }
        return;
    }

    displayOrder(result.value);
}

void PosWindow::displayOrder(const SaleRecord& order) {
    selectedOrderId_ = order.ids.orderId;
    displayedBill_ = order;
    hasDisplayedBill_ = true;
    if (historyPage_) {
        auto& view = *static_cast<OrderHistoryView*>(historyPage_);
        view.showError({});
        view.showBill(displayedBill_);
    }
}

void PosWindow::clearOrderSelection() {
    selectedOrderId_.clear();
    displayedBill_ = {};
    hasDisplayedBill_ = false;
    if (historyPage_) {
        static_cast<OrderHistoryView*>(historyPage_)->clearBill();
    }
}

// records payment on the selected held order, using the existing bill
void PosWindow::onMarkPaidClicked() {
    if (!historyPage_ || !hasDisplayedBill_ || displayedBill_.status != OrderStatus::OnHold) {
        return;
    }

    auto& view = *static_cast<OrderHistoryView*>(historyPage_);
    Result<Payment> payment = view.readPayment();
    if (!payment.success) {
        view.showError(payment.message);
        return;
    }

    payment.value.recorded = true;
    const Result<SaleRecord> result = history_.markPaid(selectedOrderId_, payment.value);
    if (!result.success) {
        view.showError(result.message);
        return;
    }

    displayOrder(result.value);
    refreshHistory();
}

// archives the selected order and clears it from the bill panel state
void PosWindow::onArchiveClicked() {
    if (selectedOrderId_.empty()) {
        return;
    }

    const Status result = history_.archive(selectedOrderId_);
    if (!result.success) {
        if (historyPage_) {
            static_cast<OrderHistoryView*>(historyPage_)->showError(result.message);
        }
        return;
    }

    clearOrderSelection();
    if (historyPage_) {
        refreshHistory();
    }
}

void PosWindow::refreshHistory() {
    auto& view = *static_cast<OrderHistoryView*>(historyPage_);
    const Result<vector<SaleRecord>> orders = history_.list();
    if (!orders.success) {
        view.showError(orders.message);
        return;
    }

    view.showError({});
    view.showOrders(orders.value, selectedOrderId_);
}

// displays the details and barcode image for the selected catalogue product
void PosWindow::onProductSelected(const string& productId) {
    if (!cataloguePage_) {
        return;
    }

    auto& view = *static_cast<CatalogueView*>(cataloguePage_);
    const Result<Product> product = products_.findById(productId);
    if (!product.success) {
        selectedCatalogueProduct_.reset();
        view.clearProduct();
        view.showError(product.message);
        return;
    }

    selectedCatalogueProduct_ = product.value;
    view.showError({});
    view.showProduct(*selectedCatalogueProduct_);
}

// generates the barcode preview in memory for the add product form
void PosWindow::onGeneratePreview() {
    if (!cataloguePage_) {
        return;
    }

    auto& view = *static_cast<CatalogueView*>(cataloguePage_);
    const QImage image = images_.preview(view.readBarcode());
    view.showBarcodePreview(image);
    view.showError(image.isNull() ? "Could not generate a preview. Check the barcode." : "");
}

// sends the new product to the product service and shows what was saved
void PosWindow::onSaveProduct() {
    if (!cataloguePage_) {
        return;
    }

    auto& view = *static_cast<CatalogueView*>(cataloguePage_);
    const Result<Product> input = view.readNewProduct();
    if (!input.success) {
        view.showError(input.message);
        return;
    }

    const Result<Product> saved = products_.create(input.value);
    if (!saved.success) {
        view.showError(saved.message);
        return;
    }

    refreshCatalogue();
    view.showSavedProduct(saved.value);
}

// sends the original product and the edited version to the product service
void PosWindow::onUpdateProduct() {
    if (!cataloguePage_ || !selectedCatalogueProduct_) {
        return;
    }

    auto& view = *static_cast<CatalogueView*>(cataloguePage_);
    const Result<Product> replacement = view.readEditedProduct();
    if (!replacement.success) {
        view.showError(replacement.message);
        return;
    }

    const Result<Product> saved = products_.update(*selectedCatalogueProduct_, replacement.value);
    if (!saved.success) {
        view.showError(saved.message);
        return;
    }

    selectedCatalogueProduct_ = saved.value;
    view.showProduct(*selectedCatalogueProduct_);
    refreshCatalogue();
}

// asks the product service to delete the selected catalogue product
void PosWindow::onDeleteProduct() {
    if (!cataloguePage_ || !selectedCatalogueProduct_) {
        return;
    }

    auto& view = *static_cast<CatalogueView*>(cataloguePage_);
    const Status removed = products_.remove(selectedCatalogueProduct_->id);
    if (!removed.success) {
        view.showError(removed.message);
        return;
    }

    selectedCatalogueProduct_.reset();
    view.clearProduct();
    refreshCatalogue();
}

void PosWindow::refreshCatalogue() {
    auto& view = *static_cast<CatalogueView*>(cataloguePage_);
    const Result<vector<Product>> products = products_.list();
    if (!products.success) {
        view.showError(products.message);
        return;
    }

    view.showError({});
    view.showProducts(products.value, selectedCatalogueProduct_ ? selectedCatalogueProduct_->id : string{});
}

// switches the page in view and starts or stops the camera when needed
void PosWindow::showPage(const string& pageName) {
    if (!pages_) {
        return;
    }

    QWidget* page = nullptr;
    if (pageName == "Current Order") {
        page = currentOrderPage_;
    } else if (pageName == "Order History") {
        page = historyPage_;
    } else if (pageName == "Product Catalogue") {
        page = cataloguePage_;
    }

    if (!page || pages_->indexOf(page) < 0) {
        return;
    }

    if (page != currentOrderPage_) {
        stopCurrentOrderCamera();
    }
    pages_->setCurrentWidget(page);

    if (page == currentOrderPage_ && !currentOrderOpened_) {
        auto& view = *static_cast<CurrentOrderView*>(currentOrderPage_);
        view.showCartError(refreshCart());
        currentOrderOpened_ = true;
        cameraStatus_ = scanner_.start(0);
        if (cameraStatus_.success) {
            cameraTimer_.start();
        }
        view.showScannerMessage(cameraStatus_.message);
    }

    if (page == historyPage_) {
        refreshHistory();
    } else if (page == cataloguePage_) {
        refreshCatalogue();
    }
}

// switches between tabs inside the product catalogue page
void PosWindow::showCatalogueTab(const string& tabName) {
    if (!catalogueTabs_) {
        return;
    }

    const QString title = QString::fromStdString(tabName);
    for (int index = 0; index < catalogueTabs_->count(); ++index) {
        if (catalogueTabs_->tabText(index) == title) {
            catalogueTabs_->setCurrentIndex(index);
            return;
        }
    }
}

// stops the timer and releases the camera when leaving Current Order
void PosWindow::stopCurrentOrderCamera() {
    if (!currentOrderOpened_) {
        return;
    }

    cameraTimer_.stop();
    scanner_.stop();
    currentOrderOpened_ = false;
}

// closing the window has to release the camera too
void PosWindow::closeEvent(QCloseEvent* event) {
    QMainWindow::closeEvent(event);
    if (event->isAccepted()) {
        stopCurrentOrderCamera();
    }
}
