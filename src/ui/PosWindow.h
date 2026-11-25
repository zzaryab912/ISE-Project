#pragma once

#include "PosTypes.h"

#include <QMainWindow>
#include <QTimer>

using namespace std;

class CartService;
class CheckoutService;
class DiscountService;
class HistoryService;
class ManagerCataloguePage;
class ProductService;
class PosScanner;
class ReportService;
class ReportsPage;

class QImage;
class QLabel;
class QPushButton;
class QStackedWidget;

class PosWindow : public QMainWindow {
public:
    class CurrentOrderView : public QWidget {
    public:
        using QWidget::QWidget;
        virtual Result<Customer> readCustomer() const = 0;
        virtual Result<Payment> readPayment() const = 0;
        virtual void resetCheckoutFields() = 0;
        virtual void showCameraPreview(const QImage& image) = 0;
        virtual void showScannerMessage(const string& message) = 0;
        virtual void showScannedProduct(const ProductStock& item) = 0;
        virtual void showCart(const vector<CartItem>& items, const vector<optional<int64_t>>& amounts,
                              const optional<Totals>& totals) = 0;
        virtual void showCartError(const string& message) = 0;
        virtual void setBillSaving(bool saving) = 0;
        virtual void showBillError(const string& message) = 0;
        virtual void selectQuantity(const string& id) = 0;
        virtual void clearQuantityDraft(const string& id) = 0;
        virtual void setCustomer(const Customer& customer) = 0;
        virtual void setCheckoutOrder(const SaleRecord* sale) = 0;
        virtual bool hasDraft() const = 0;
        virtual bool hasQuantityDraft() const = 0;
    };

    class OrderHistoryView : public QWidget {
    public:
        using QWidget::QWidget;
        virtual bool showingArchives() const = 0;
        virtual void setArchiveView(bool archived) = 0;
        virtual void showError(const string& message) = 0;
        virtual void showNotice(const string& message) = 0;
        virtual void showPdfRecoveryWarning(const string& message) = 0;
        virtual void showOrders(const vector<SaleRecord>& orders, const string& selectedId) = 0;
        virtual void showBill(const SaleRecord& order) = 0;
        virtual void clearBill() = 0;
        virtual void showPaidEdit(const PaidEditDraft& draft) = 0;
        virtual void clearPaidEditApproval() = 0;
        virtual void setPaidEditBusy(bool busy) = 0;
        virtual void setPaidEditApproved(bool approved) = 0;
        virtual void setPdfActions(bool canGenerate, bool canOpen) = 0;
        virtual void setArchiveActionsAvailable(bool available) = 0;
        virtual void showUnarchiveShortages(const vector<StockShortage>& shortages) = 0;
    };

    PosWindow(ProductService& products, DiscountService& discounts, CartService& cart,
              PosScanner& scanner, CheckoutService& checkout, HistoryService& history,
              const ReportService& reports, ManagerPinService& access, QWidget* parent = nullptr);

    void onRoleSelected(Role role);
    void onChangePin(const string& currentPin, const string& newPin, const string& confirmation);
    void onCameraTick();
    void onHeldOrdersClicked();
    void onDiscardResumedOrder();
    void onCartQuantityChanged(const string& productId, int64_t quantity);
    void cancelPendingQuantity(const string& productId);
    void onGenerateBillClicked();
    void onGenerateAndHoldClicked();
    void onOrderSelected(const string& orderId);
    void onResumeOrder(const string& orderId);
    void onPreviewPaidEdit(const string& orderId, const vector<QuantityChange>& changes);
    void onApprovePaidEdit(const string& pin);
    void onCommitPaidEdit(int64_t settledDelta);
    void cancelPaidEdit();
    void onArchiveOrder(const string& orderId);
    void onUnarchiveOrder(const string& orderId);
    void onGeneratePdf(const string& orderId);
    void onOpenPdf(const string& orderId);
    void onGenerateReport(const ReportRequest& request);
    void showHistoryTab(bool archived);
    void showPage(const string& pageName);
    void setPdfRecoveryWarning(const string& message);

private:
    ProductService& products_;
    DiscountService& discounts_;
    CartService& cart_;
    PosScanner& scanner_;
    CheckoutService& checkout_;
    HistoryService& history_;
    const ReportService& reports_;
    ManagerPinService& access_;

    optional<Role> role_;
    QStackedWidget* pages_ = nullptr;
    QWidget* launchPage_ = nullptr;
    CurrentOrderView* currentOrderPage_ = nullptr;
    OrderHistoryView* historyPage_ = nullptr;
    ManagerCataloguePage* cataloguePage_ = nullptr;
    ReportsPage* reportsPage_ = nullptr;
    QWidget* settingsPage_ = nullptr;
    QWidget* sidebar_ = nullptr;
    QLabel* settingsFeedback_ = nullptr;
    QPushButton* changePin_ = nullptr;
    vector<QPushButton*> navigation_;
    QTimer cameraTimer_;
    string selectedOrderId_;
    string resumedOrderId_;
    optional<PricingResult> currentQuote_;
    optional<PaidEditDraft> paidEditDraft_;
    optional<ManagerApproval> paidEditApproval_;
    optional<Product> pendingProduct_;
    optional<SaleRecord> displayedBill_;
    string scannedBarcode_;
    string pdfRecoveryWarning_;
    bool busy_ = false;
    bool currentOrderOpened_ = false;
    bool choosingHeldOrder_ = false;

    void enterRole(Role role);
    void showRolePicker();
    void updateNavigation();
    void submitManagerPin(const string& pin, const string& confirmation, bool initialize);
    void stopCurrentOrderCamera();
    void closeEvent(QCloseEvent* event) override;
    void generateCurrentBill(OrderStatus status);
    void clearCurrentOrder();
    void addScannedProduct(const string& barcode);
    void displayOrder(const SaleRecord& order);
    void clearOrderSelection();
    string refreshCart();
    void refreshHistory();
};
