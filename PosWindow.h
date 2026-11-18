#pragma once

#include "PosTypes.h"                                   // imports all the shared types, enums, classes

#include <QMainWindow>
#include <QTimer>

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

    QStackedWidget* pages_ = nullptr;       // this is the widget that holds the pages, shows one at a time
    QWidget* currentOrderPage_ = nullptr;
    QWidget* historyPage_ = nullptr;
    QWidget* cataloguePage_ = nullptr;
    QTabWidget* catalogueTabs_ = nullptr;   // these are pointers to all the pages

public:
    PosWindow(ProductService& products, BarcodeAssetService& images,
              CartService& cart, PosScanner& scanner,
              CheckoutService& checkout, HistoryService& history,
              QWidget* parent = nullptr);

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
