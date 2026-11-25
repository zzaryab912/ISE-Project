#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

using namespace std;

enum class Unit {
    Count,
    Weight
};

enum class OrderStatus {
    InProgress,
    OnHold,
    Paid
};

enum class BillChange {
    Unchanged,
    Modified,
    CompleteRefund
};

enum class PaymentMode {
    Cash,
    Card,
    Wallet
};

enum class Role {
    Cashier,
    Manager
};

enum class ReportKind {
    Incomplete,
    Completed,
    CashSales,
    AllSales,
    PaymentMix,
    TopSold,
    HighestStock,
    LowestStock
};

enum class ReportPeriod {
    Day,
    Week,
    Month,
    Year,
    AllTime,
    Custom
};

struct Status {
    bool success = false;
    string message {};
};

template <typename T>
struct Result {
    bool success = false;
    string message {};
    T value {};
};

struct Timestamp {
    string date {};
    string time {};
    int64_t epochSeconds = 0;
};

struct Category {
    string id {};
    string name {};
    string acronym {};
};

// Money is stored in paisa, quantities in thousandths, and rates in basis points.
struct Product {
    string id {};
    string barcode {};
    string categoryId {};
    string acronym {};
    string name {};
    Unit unit = Unit::Count;
    int64_t unitPrice = 0;
    int32_t taxRate = 0;
    int64_t discountPerUnit = 0;
    int64_t onHand = 0;
    string barcodeImagePath {};
};

struct StockPosition {
    int64_t onHand = 0;
    int64_t reserved = 0;
    int64_t available = 0;
};

struct ProductStock {
    Product product {};
    StockPosition stock {};
};

struct StockShortage {
    string productId {};
    string name {};
    Unit unit = Unit::Count;
    int64_t required = 0;
    int64_t available = 0;
};

struct Season {
    string startDate {};
    string endDate {};
    int32_t rate = 0;
    bool active = false;
};

struct SpecialDiscount {
    int32_t rate = 0;
    bool active = false;
};

struct PricingTerms {
    int32_t seasonalRate = 0;
    int32_t specialRate = 0;
};

struct CartItem {
    Product product {};
    int64_t quantity = 0;
};

struct Customer {
    string name {};
    string contact {};
};

struct Payment {
    bool recorded = false;
    int64_t amountReceived = 0;
    int64_t changeReturned = 0;
    PaymentMode mode = PaymentMode::Cash;
    string merchant {};
};

struct SaleIds {
    string orderId {};
    string billId {};
};

struct SaleLine {
    string productId {};
    string barcode {};
    string acronym {};
    string name {};
    Unit unit = Unit::Count;
    int64_t unitPrice = 0;
    int32_t taxRate = 0;
    int64_t discountPerUnit = 0;
    int64_t quantity = 0;
    int64_t lineProductDiscount = 0;
    int64_t lineTax = 0;
};

struct Totals {
    int64_t subtotal = 0;
    int64_t productDiscount = 0;
    int64_t seasonalDiscount = 0;
    int64_t specialDiscount = 0;
    int64_t tax = 0;
    int64_t roundingAdjustment = 0;
    int64_t payable = 0;
};

struct PricingResult {
    vector<SaleLine> lines {};
    vector<int64_t> lineSubtotals {};
    PricingTerms terms {};
    Totals totals {};
};

struct QuantityChange {
    string productId {};
    int64_t quantity = 0;
    int64_t restockQuantity = 0;
};

struct LineAdjustment {
    string productId {};
    int64_t previousQuantity = 0;
    int64_t quantity = 0;
    int64_t restockQuantity = 0;
};

struct BillAdjustment {
    uint32_t revision = 0;
    Timestamp at {};
    int64_t amountDelta = 0;
    int64_t productDiscountDelta = 0;
    int64_t seasonalDiscountDelta = 0;
    int64_t specialDiscountDelta = 0;
    vector<LineAdjustment> lines {};
};

struct SaleRecord {
    SaleIds ids {};
    Timestamp createdAt {};
    optional<Timestamp> paidAt {};
    Customer customer {};
    Payment payment {};
    OrderStatus status = OrderStatus::InProgress;
    bool archived = false;
    BillChange billChange = BillChange::Unchanged;
    uint32_t revision = 0;
    vector<SaleLine> lines {};
    PricingTerms terms {};
    Totals totals {};
    vector<BillAdjustment> adjustments {};
    string pdfPath {};
};

struct HeldCheckout {
    SaleRecord sale {};
    vector<CartItem> items {};
};

struct PaidEditDraft {
    string orderId {};
    uint32_t revision = 0;
    vector<QuantityChange> changes {};
    Totals before {};
    Totals after {};
    int64_t amountDelta = 0;
};

class ManagerPinService;
class CheckoutService;

class ManagerApproval {
public:
    ManagerApproval() = default;

private:
    optional<PaidEditDraft> draft_ {};

    friend class ManagerPinService;
    friend class CheckoutService;
};

struct StoreData {
    vector<Category> categories {};
    vector<Product> products {};
    vector<Season> seasons {};
    SpecialDiscount specialDiscount {};
    vector<string> issuedBarcodes {};
    vector<SaleRecord> sales {};
};

struct ReportRequest {
    ReportKind kind = ReportKind::Incomplete;
    ReportPeriod period = ReportPeriod::Day;
    string anchorDate {};
    string startDate {};
    string endDate {};
    vector<PaymentMode> modes {};
    optional<Unit> unit {};
};

struct ReservationReportRow {
    string productId {};
    string name {};
    Unit unit = Unit::Count;
    int64_t quantity = 0;
    StockPosition stock {};
};

struct OrderReportRow {
    SaleIds ids {};
    string customerName {};
    Timestamp at {};
    OrderStatus status = OrderStatus::InProgress;
    BillChange billChange = BillChange::Unchanged;
    optional<PaymentMode> mode {};
    Totals totals {};
    int64_t collected = 0;
    int64_t refunded = 0;
    vector<ReservationReportRow> reservations {};
};

struct ProductReportRow {
    string productId {};
    string name {};
    Unit unit = Unit::Count;
    int64_t quantitySold = 0;
    optional<Unit> stockUnit {};
    optional<StockPosition> stock {};
};

struct PaymentMixRow {
    PaymentMode mode = PaymentMode::Cash;
    uint64_t count = 0;
    int32_t shareBasisPoints = 0;
};

struct ReportResult {
    string startDate {};
    string endDate {};
    vector<OrderReportRow> orders {};
    vector<ProductReportRow> products {};
    vector<PaymentMixRow> paymentMix {};
    int64_t billTotal = 0;
    int64_t collected = 0;
    int64_t refunded = 0;
    int64_t netCollections = 0;
    int64_t totalDiscount = 0;
    uint64_t refundCount = 0;
    uint64_t outOfStockCount = 0;
};
