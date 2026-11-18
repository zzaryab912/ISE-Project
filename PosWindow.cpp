#include "PosWindow.h"
#include "BarcodeAssetService.h"
#include "CartService.h"
#include "CheckoutService.h"
#include "HistoryService.h"
#include "PosScanner.h"
#include "ProductService.h"

#include <QCloseEvent>
#include <QDateTime>
#include <QImage>
#include <QScopedValueRollback>
#include <QStackedWidget>
#include <QTabWidget>

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
          cameraTimer_.setInterval(200);                                                // sets timer tick every 200 ms
          cameraTimer_.setTimerType(Qt::CoarseTimer);                                   // camera checked rougly five times a second
          connect(&cameraTimer_, &QTimer::timeout, this, &PosWindow::onCameraTick);     // on each camera tick, call onCameraTick
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
    const Result<ScanResult> scane = scanner_.poll();                   // poll asks scanner to read one frame and check for a barcode, returns a result object

    if (!scane.success) {       // stops the camera if reading the frame failed
        cameraStatus_ = {false, scane.message};
        cameraTimer_.stop();
        scanner_.stop();
        return;
    }

    if (!scane.value.decoded.found || !scane.value.codeChanged) {                           // if no new barcode is found, return, keep scanning
        return;
    }

    const Result<Product> product = products_.findByBarcode(scane.value.decoded.code);      // looks up barcode in the catalogue. If there is no match, selection stays as it is
    if (!product.success) {
        return;
    }

    if (hasSelectedProduct_ && selectedProduct_.id == product.value.id) {                   // preserves teh entered quantity if lookup returns product already selected
        return;
    }

    // for a different recognised product: this is the new selection now, and the addQuantity option is now set to zero again
    selectedProduct_ = product.value;
    hasSelectedProduct_ = true;
    addQuantity_ = 0;
}

// when clicking on Add to order
void PosWindow::onAddClicked() {
    if (!hasSelectedProduct_ || addQuantity_ <= 0) {                                        // if product isnt selected, or quantity to be added is zero or smaller
        return;
    }

    const Status result = cart_.addByBarcode(selectedProduct_.barcode, addQuantity_);       // if add fails, it stops trying to add so cashier can change quantity manually. super neat right?
    if (!result.success) {
        return;
    }

    addQuantity_ = 0;
}

// updates an existing row. zero removes the row here
void PosWindow::onCartQuantityChanged(const string& productId, int64_t quantity) {
    const Status result = cart_.setQuantity(productId, quantity);
    if (!result.success) {
        return;
    }
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
    view.showEmptyCart();
    view.setBillSaving(false);
    displayOrder(result.value);
    showPage("Order History");
}

// loads the saved bill for the selected order id
void PosWindow::onOrderSelected(const string& orderId) {
    const Result<SaleRecord> result = history_.get(orderId);
    if (!result.success) {
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

    selectedOrderId_.clear();
    displayedBill_ = {};
    hasDisplayedBill_ = false;
    if (historyPage_) {
        static_cast<OrderHistoryView*>(historyPage_)->clearBill();
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

    view.showSavedProduct(saved.value);
    refreshCatalogue();
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
        currentOrderOpened_ = true;
        cameraStatus_ = scanner_.start(0);
        if (cameraStatus_.success) {
            cameraTimer_.start();
        }
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
