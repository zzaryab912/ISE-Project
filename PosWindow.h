#pragma once

#include "PosTypes.h"                                   // imports all the shared types, enums, classes

#include <QMainWindow>
#include <QTimer>
#include <optional>

using namespace std;

class ProductService;                                   // forward declarations, for all the services to be used here
class BarcodeAssetService;
class CartService;
class PosScanner;
class CheckoutService;
class HistoryService;

class QStackedWidget;
class QTabWidget;
class QWidget;
class QImage;

class PosWindow : public QMainWindow {
private:
    ProductService& products_;              // Product service, to look up, add or edit or delete products
    BarcodeAssetService& images_;           // to generate a barcode image preview for add product menu
    CartService& cart_;                     // for cart operations like add, remove, change quantity, and totals
    PosScanner& scanner_;                   // start and stop and read from barcode scanner
    CheckoutService& checkout_;             // checkout services, generate a paid or held bill
    HistoryService& history_;               // retrieve saved orders, mark as paid or archive

    Product selectedProduct_{};             // start product with default values, the rest of the lines here do the same thing
    bool hasSelectedProduct_ = false;
    string selectedOrderId_{};
    int64_t addQuantity_ = 0;
    SaleRecord displayedBill_{};
    bool hasDisplayedBill_ = false;
    QTimer cameraTimer_{};
    bool currentOrderOpened_ = false;
    Status cameraStatus_{};
    bool savingBill_ = false;
    optional<Product> selectedCatalogueProduct_;

    QStackedWidget* pages_ = nullptr;       // this is the widget that holds the pages, shows one at a time
    QWidget* currentOrderPage_ = nullptr;
    QWidget* historyPage_ = nullptr;
    QWidget* cataloguePage_ = nullptr;
    QTabWidget* catalogueTabs_ = nullptr;   // these are pointers to all the pages

    void stopCurrentOrderCamera();
    void closeEvent(QCloseEvent* event) override;
    void generateCurrentBill(OrderStatus status);
    void displayOrder(const SaleRecord& order);
    void clearOrderSelection();
    string refreshCart();
    void refreshHistory();
    void refreshCatalogue();

public:
    class CurrentOrderView : public QWidget {
    public:
        using QWidget::QWidget;
        virtual Result<Customer> readCustomer() const = 0;
        virtual Result<Payment> readPayment() const = 0;
        virtual void resetCheckoutFields() = 0;
        virtual Result<int64_t> readAddQuantity() const = 0;
        virtual void showCameraPreview(const QImage& image) = 0;
        virtual void showScannerMessage(const string& message) = 0;
        virtual void showScannedProduct(const Product& product) = 0;
        // resets the quantity to zero and disables Add until the next valid input
        virtual void resetAddQuantity() = 0;
        virtual void showCart(const vector<CartItem>& items, const vector<optional<int64_t>>& amounts,
                              const optional<Totals>& totals) = 0;
        virtual void showCartError(const string& message) = 0;
        virtual void setBillSaving(bool saving) = 0;
        virtual void showBillError(const string& message) = 0;
    };

    class OrderHistoryView : public QWidget {
    public:
        using QWidget::QWidget;
        virtual Result<Payment> readPayment() const = 0;
        virtual void showError(const string& message) = 0;
        virtual void showOrders(const vector<SaleRecord>& orders, const string& selectedId) = 0;
        virtual void showBill(const SaleRecord& order) = 0;
        virtual void clearBill() = 0;
    };

    class CatalogueView : public QWidget {
    public:
        using QWidget::QWidget;
        virtual string readBarcode() const = 0;
        virtual Result<Product> readNewProduct() const = 0;
        virtual Result<Product> readEditedProduct() const = 0;
        virtual void showError(const string& message) = 0;
        virtual void showProducts(const vector<Product>& products, const string& selectedId) = 0;
        virtual void showProduct(const Product& product) = 0;
        virtual void setHeldReference(bool) {}
        virtual void clearProduct() = 0;
        virtual void showBarcodePreview(const QImage& image) = 0;
        virtual void showSavedProduct(const Product& product) = 0;
    };

    PosWindow(ProductService& products, BarcodeAssetService& images,
              CartService& cart, PosScanner& scanner,
              CheckoutService& checkout, HistoryService& history,
              QWidget* parent = nullptr);

    void bindPages(QStackedWidget& pages, CurrentOrderView& currentOrder,
                   OrderHistoryView& history, CatalogueView& catalogue, QTabWidget& catalogueTabs);

    void onCameraTick();                                                        // reads next frame, updates preview, looks for a new barcode
    void onAddClicked();                                                        // Asks cart to add selected product in entered quantity
    void onCartQuantityChanged(const string& productId, int64_t quantity);      // updates existing row. zero removes the row here
    void onGenerateBillClicked();                                               // below are all the actions
    void onGenerateAndHoldClicked();
    void onOrderSelected(const string& orderId);                                // loads and displays the saved bill for the order id
    void onMarkPaidClicked();
    void onArchiveClicked();
    void onProductSelected(const string& productId);                            // displays the details on the selected product
    void onGeneratePreview();
    void onSaveProduct();
    void onUpdateProduct();                                                     // onsave, update, delete all talk to the productservice to carry out the actions
    void onDeleteProduct();
    void showPage(const string& pageName);                                      // switches the page in view. handles starting and stopping camera when switching pages too
    void showCatalogueTab(const string& tabName);                               // switches between tabs inside the product catalogue page
};
