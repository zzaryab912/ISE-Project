#include "CheckoutService.h"

#include "DiscountService.h"
#include "HistoryService.h"
#include "IdGenerator.h"
#include "IPosStore.h"
#include "PricingCalculator.h"
#include "ProductService.h"

#include <algorithm>
#include <limits>
#include <string_view>
#include <unordered_set>
#include <utility>

using namespace std;

namespace {

bool hasLineBreak(const string& text)
{
    return text.find_first_of("\r\n") != string::npos;
}

// The date and time fields passed here are at most four digits long.
int decimalField(string_view field)
{
    int value = 0;
    for (char digit : field) {
        if (digit < '0' || digit > '9') {
            return -1;
        }
        value = value * 10 + (digit - '0');
    }
    return value;
}

bool validTimestamp(const Timestamp& timestamp)
{
    const string_view date = timestamp.date;
    const string_view time = timestamp.time;
    if (timestamp.epochSeconds <= 0 || date.size() != 10 || date[4] != '-' || date[7] != '-' ||
        time.size() != 8 || time[2] != ':' || time[5] != ':') {
        return false;
    }

    const int year = decimalField(date.substr(0, 4));
    const int month = decimalField(date.substr(5, 2));
    const int day = decimalField(date.substr(8, 2));
    if (year < 1 || month < 1 || month > 12 || day < 1) {
        return false;
    }

    constexpr int monthLengths[] = {31, 28, 31, 30, 31, 30,
                                    31, 31, 30, 31, 30, 31};
    const bool leapYear = year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
    const int lastDay = monthLengths[month - 1] + (month == 2 && leapYear ? 1 : 0);

    const int hour = decimalField(time.substr(0, 2));
    const int minute = decimalField(time.substr(3, 2));
    const int second = decimalField(time.substr(6, 2));
    return day <= lastDay && hour >= 0 && hour < 24 &&
           minute >= 0 && minute < 60 && second >= 0 && second < 60;
}

Status validateCustomer(const Customer& customer)
{
    if (customer.name.find_first_not_of(" \t\r\n\v\f") == string::npos) {
        return {false, "Enter the customer's name."};
    }
    if (hasLineBreak(customer.name) || hasLineBreak(customer.contact)) {
        return {false, "Customer name and contact must each be on one line."};
    }
    const string_view contact = customer.contact;
    if (!contact.empty()) {
        const string_view digits = contact.front() == '+' ? contact.substr(1) : contact;
        if (digits.size() < 7 || digits.size() > 15 ||
            !all_of(digits.begin(), digits.end(), [](char c) { return c >= '0' && c <= '9'; })) {
            return {false, "Contact must contain 7–15 digits, with an optional leading +."};
        }
    }
    return {true, {}};
}

Result<Payment> paymentFor(const Payment& input, OrderStatus status, int64_t payable)
{
    if (status == OrderStatus::OnHold) {
        if (input.recorded || input.amountReceived != 0 || input.changeReturned != 0 ||
            !input.merchant.empty()) {
            return {false, "A held bill must have no recorded payment.", {}};
        }
        return {true, {}, {}};
    }

    if (!input.recorded || payable < 0 || input.amountReceived < payable) {
        return {false, "Record a payment that covers the total payable.", {}};
    }
    if (hasLineBreak(input.merchant)) {
        return {false, "The payment merchant must be on one line.", {}};
    }

    Payment payment = input;
    switch (payment.mode) {
    case PaymentMode::Cash:
        payment.changeReturned = payment.amountReceived - payable;
        return {true, {}, std::move(payment)};
    case PaymentMode::Card:
    case PaymentMode::Wallet:
        if (payment.amountReceived != payable) {
            return {false, "Card and wallet payments must equal the total payable.", {}};
        }
        payment.changeReturned = 0;
        return {true, {}, std::move(payment)};
    }
    return {false, "Choose cash, card, or wallet as the payment mode.", {}};
}

auto findSale(StoreData& data, const string& orderId)
{
    return find_if(data.sales.begin(), data.sales.end(), [&orderId](const SaleRecord& sale) {
        return sale.ids.orderId == orderId;
    });
}

Status saveSale(IPosStore& store, StoreData& data, const SaleRecord& sale, bool create)
{
    if (create) {
        data.sales.push_back(sale);
    } else {
        // A stock service can replace the snapshot while applying its changes.
        const auto existing = findSale(data, sale.ids.orderId);
        if (existing == data.sales.end()) {
            return {false, "The order is missing from the updated stock snapshot."};
        }
        *existing = sale;
    }
    return store.save(data);
}

auto findProduct(const StoreData& data, const string& productId)
{
    return find_if(data.products.begin(), data.products.end(), [&productId](const Product& product) {
        return product.id == productId;
    });
}

bool isActiveHold(const SaleRecord& sale)
{
    return sale.status == OrderStatus::OnHold && !sale.archived && !sale.payment.recorded &&
           !sale.paidAt;
}

bool validQuantity(Unit unit, int64_t quantity)
{
    return quantity >= 0 &&
           (unit == Unit::Weight || (unit == Unit::Count && quantity % 1000 == 0));
}

Status applyStockChanges(const ProductService& products, StoreData& data,
                         const vector<SaleLine>& before, const vector<QuantityChange>& changes)
{
    for (const auto& change : changes) {
        if (change.restockQuantity > 0) {
            const auto product = findProduct(data, change.productId);
            if (product != data.products.end() &&
                product->onHand > numeric_limits<int64_t>::max() - change.restockQuantity) {
                return {false, "Restocking " + product->name + " would exceed the supported stock quantity."};
            }
        }
    }
    return products.applyPaidStockDifference(data, before, changes);
}

bool sameTotals(const Totals& left, const Totals& right)
{
    return left.subtotal == right.subtotal && left.productDiscount == right.productDiscount &&
           left.seasonalDiscount == right.seasonalDiscount &&
           left.specialDiscount == right.specialDiscount && left.tax == right.tax &&
           left.roundingAdjustment == right.roundingAdjustment && left.payable == right.payable;
}

bool sameDraft(const PaidEditDraft& left, const PaidEditDraft& right)
{
    return left.orderId == right.orderId && left.revision == right.revision &&
           left.amountDelta == right.amountDelta && sameTotals(left.before, right.before) &&
           sameTotals(left.after, right.after) &&
           equal(left.changes.begin(), left.changes.end(), right.changes.begin(), right.changes.end(),
                 [](const QuantityChange& a, const QuantityChange& b) {
                     return a.productId == b.productId && a.quantity == b.quantity &&
                            a.restockQuantity == b.restockQuantity;
                 });
}

struct PaidEditQuote {
    PaidEditDraft draft;
    PricingResult price;
    vector<LineAdjustment> adjustments;
};

Result<PaidEditQuote> repricePaidEdit(const SaleRecord& sale, const vector<QuantityChange>& changes,
                                     const Timestamp& now, const PricingCalculator& pricing)
{
    if (sale.status != OrderStatus::Paid || sale.archived || !sale.payment.recorded || !sale.paidAt ||
        sale.billChange == BillChange::CompleteRefund) {
        return {false, "Only an active paid bill that has not been completely refunded can be edited.", {}};
    }
    constexpr uint64_t editWindow = 7 * 24 * 60 * 60;
    const int64_t paidAt = sale.paidAt->epochSeconds;
    if (!validTimestamp(now) || !validTimestamp(*sale.paidAt) || now.epochSeconds < paidAt ||
        static_cast<uint64_t>(now.epochSeconds) - static_cast<uint64_t>(paidAt) > editWindow) {
        return {false, "Paid bills can only be edited within 168 hours of the original payment.", {}};
    }
    if (!sale.adjustments.empty() && now.epochSeconds < sale.adjustments.back().at.epochSeconds) {
        return {false, "The edit time is earlier than this bill's last adjustment.", {}};
    }
    if (sale.revision == numeric_limits<uint32_t>::max() || changes.empty() || sale.totals.payable < 0 ||
        sale.payment.amountReceived < 0) {
        return {false, "Enter quantity changes for a bill that can accept a new revision.", {}};
    }

    auto lines = sale.lines;
    vector<QuantityChange> revisedQuantities;
    revisedQuantities.reserve(changes.size());
    vector<LineAdjustment> adjustments;
    adjustments.reserve(changes.size());
    unordered_set<string> changedProducts;
    for (const auto& change : changes) {
        const auto line = find_if(lines.begin(), lines.end(), [&](const SaleLine& item) {
            return item.productId == change.productId;
        });
        if (line == lines.end() || !changedProducts.insert(change.productId).second) {
            return {false, "Choose each product on this bill at most once.", {}};
        }
        if (!validQuantity(line->unit, line->quantity) || !validQuantity(line->unit, change.quantity) ||
            !validQuantity(line->unit, change.restockQuantity) ||
            change.restockQuantity > max(int64_t{0}, line->quantity - change.quantity)) {
            return {false, "Invalid quantity or restock amount for " + line->name + ".", {}};
        }
        if (change.quantity == line->quantity) {
            continue;
        }
        revisedQuantities.push_back(change);
        adjustments.push_back({line->productId, line->quantity, change.quantity, change.restockQuantity});
        line->quantity = change.quantity;
    }
    if (revisedQuantities.empty()) {
        return {false, "Change at least one quantity on the paid bill.", {}};
    }

    // Settlement is meaningful only if the saved rates reproduce the bill the customer paid.
    const auto original = pricing.calculate(sale.lines, sale.terms);
    if (!original.success || !sameTotals(original.value.totals, sale.totals) ||
        !equal(sale.lines.begin(), sale.lines.end(), original.value.lines.begin(), original.value.lines.end(),
               [](const SaleLine& saved, const SaleLine& calculated) {
                   return saved.productId == calculated.productId && saved.lineTax == calculated.lineTax &&
                          saved.lineProductDiscount == calculated.lineProductDiscount;
               })) {
        return {false, "The saved rates do not reproduce this bill. Review its pricing before settlement.", {}};
    }

    auto priced = pricing.calculate(lines, sale.terms);
    if (!priced.success) {
        return {false, "Could not reprice the paid bill. " + priced.message, {}};
    }
    const int64_t delta = priced.value.totals.payable - sale.totals.payable;
    if (delta > 0) {
        // First tender and every subsequent collection share one persisted receipts total.
        int64_t remaining = numeric_limits<int64_t>::max() - sale.payment.amountReceived;
        for (const auto& adjustment : sale.adjustments) {
            if (adjustment.amountDelta > remaining) {
                return {false, "This bill's recorded collections exceed the supported range.", {}};
            }
            if (adjustment.amountDelta > 0) {
                remaining -= adjustment.amountDelta;
            }
        }
        if (delta > remaining) {
            return {false, "This collection would exceed the bill's supported receipts total.", {}};
        }
    }
    PaidEditDraft draft{sale.ids.orderId, sale.revision, std::move(revisedQuantities), sale.totals,
                        priced.value.totals, delta};
    return {true, {}, {std::move(draft), std::move(priced.value), std::move(adjustments)}};
}

} // namespace

CheckoutService::CheckoutService(IPosStore& store, ProductService& products,
                                 const PricingCalculator& pricing, const DiscountService& discounts,
                                 const IdGenerator& ids, HistoryService& history)
    : store_(store), products_(products), pricing_(pricing), discounts_(discounts),
      ids_(ids), history_(history)
{
}

Result<PricingResult> CheckoutService::quote(const vector<CartItem>& items, const string& heldOrderId,
                                             const Timestamp& now) const
{
    auto data = store_.read();
    if (!data.success) {
        return {false, "Could not read checkout data. " + data.message, {}};
    }
    if (!heldOrderId.empty()) {
        const auto held = findSale(data.value, heldOrderId);
        if (held == data.value.sales.end() || !isActiveHold(*held)) {
            return {false, "Order is no longer an unpaid held order.", {}};
        }
    }
    return quoteFromSnapshot(data.value, items, heldOrderId, now);
}

Result<PricingResult> CheckoutService::quoteFromSnapshot(const StoreData& data, const vector<CartItem>& items,
                                                         const string& heldOrderId, const Timestamp& now) const
{
    if (items.empty()) {
        return {false, "Add at least one item to generate a bill.", {}};
    }
    if (!validTimestamp(now)) {
        return {false, "Enter a valid date and time (YYYY-MM-DD and HH:mm:ss).", {}};
    }

    vector<SaleLine> lines;
    lines.reserve(items.size());
    unordered_set<string> seenProducts;
    for (const CartItem& item : items) {
        const Product& captured = item.product;
        const auto live = findProduct(data, captured.id);
        if (live == data.products.end() || (heldOrderId.empty() && live->barcode != captured.barcode)) {
            return {false, "Product " + captured.id + " no longer matches the catalogue.", {}};
        }
        if (live->unit != captured.unit || item.quantity == 0 || !validQuantity(live->unit, item.quantity)) {
            return {false, "Invalid quantity or changed measurement unit for " + live->name + ".", {}};
        }
        if (!seenProducts.insert(live->id).second) {
            return {false, "The cart has multiple rows for " + live->name + ".", {}};
        }

        const Product& price = heldOrderId.empty() ? captured : *live;
        lines.push_back({live->id, live->barcode, live->acronym, live->name, live->unit,
                         price.unitPrice, price.taxRate, price.discountPerUnit, item.quantity});
    }

    const auto shortages = products_.shortages(data, lines, heldOrderId);
    if (!shortages.success) {
        return {false, shortages.message, {}};
    }
    if (!shortages.value.empty()) {
        return {false, "Insufficient stock for " + shortages.value.front().name + ".", {}};
    }
    return pricing_.calculate(lines, discounts_.termsFor(data, now));
}

Result<SaleRecord> CheckoutService::generateBill(const vector<CartItem>& items, const Customer& customer,
                                                 const Payment& payment, OrderStatus status,
                                                 const Timestamp& now, const optional<int64_t>& expectedPayable)
{
    return save(items, customer, payment, status, now, expectedPayable, nullptr);
}

Result<HeldCheckout> CheckoutService::resumeHeldOrder(const string& orderId) const
{
    if (orderId.empty()) {
        return {false, "Order ID is required.", {}};
    }
    auto data = store_.read();
    if (!data.success) {
        return {false, "Could not read checkout data. " + data.message, {}};
    }
    const auto sale = findSale(data.value, orderId);
    if (sale == data.value.sales.end() || !isActiveHold(*sale)) {
        return {false, "Order is not an unpaid held order.", {}};
    }

    HeldCheckout held;
    held.sale = *sale;
    held.items.reserve(sale->lines.size());
    for (const auto& line : sale->lines) {
        const auto product = findProduct(data.value, line.productId);
        if (product == data.value.products.end() || product->unit != line.unit) {
            return {false, "Held product is missing or has changed its unit: " + line.name + ".", {}};
        }
        held.items.push_back({*product, line.quantity});
    }
    return {true, {}, std::move(held)};
}

Result<SaleRecord> CheckoutService::saveHeldOrder(const string& orderId, const vector<CartItem>& items,
                                                  const Customer& customer, const Payment& payment,
                                                  OrderStatus status, const Timestamp& now,
                                                  const optional<int64_t>& expectedPayable)
{
    if (orderId.empty()) {
        return {false, "Order ID is required.", {}};
    }
    return save(items, customer, payment, status, now, expectedPayable, &orderId);
}

Result<SaleRecord> CheckoutService::save(const vector<CartItem>& items, const Customer& customer,
                                         const Payment& payment, OrderStatus status, const Timestamp& now,
                                         const optional<int64_t>& expectedPayable, const string* heldOrderId)
{
    if (status != OrderStatus::Paid && status != OrderStatus::OnHold) {
        return {false, "A bill must be paid or put on hold.", {}};
    }
    const auto validCustomer = validateCustomer(customer);
    if (!validCustomer.success) {
        return {false, validCustomer.message, {}};
    }

    auto data = store_.read();
    if (!data.success) {
        return {false, "Could not read checkout data. " + data.message, {}};
    }
    auto original = data.value.sales.end();
    if (heldOrderId) {
        original = findSale(data.value, *heldOrderId);
        if (original == data.value.sales.end() || !isActiveHold(*original)) {
            return {false, "Order is no longer an unpaid held order.", {}};
        }
        if (now.epochSeconds < original->createdAt.epochSeconds) {
            return {false, "The checkout time is earlier than this order's creation time.", {}};
        }
    }

    const string activeHold = heldOrderId ? *heldOrderId : string{};
    auto priced = quoteFromSnapshot(data.value, items, activeHold, now);
    if (!priced.success) {
        return {false, priced.message, {}};
    }
    const int64_t payable = priced.value.totals.payable;
    if (status == OrderStatus::Paid && (!expectedPayable || *expectedPayable != payable)) {
        return {false, "Review the current quote and confirm the total before paying.", {}};
    }
    auto checkedPayment = paymentFor(payment, status, payable);
    if (!checkedPayment.success) {
        return {false, checkedPayment.message, {}};
    }

    SaleRecord sale;
    if (heldOrderId) {
        sale.ids = original->ids;
        sale.createdAt = original->createdAt;
    } else {
        auto ids = ids_.saleIds(data.value, now);
        if (!ids.success) {
            return {false, "Could not generate sale IDs. " + ids.message, {}};
        }
        sale.ids = std::move(ids.value);
        sale.createdAt = now;
    }

    sale.customer = customer;
    sale.status = status;
    sale.payment = std::move(checkedPayment.value);
    sale.lines = std::move(priced.value.lines);
    sale.terms = priced.value.terms;
    sale.totals = priced.value.totals;
    if (status == OrderStatus::Paid) {
        const auto deducted = products_.deductStock(data.value, sale.lines, activeHold);
        if (!deducted.success) {
            return {false, "Could not deduct stock. " + deducted.message, {}};
        }
        sale.paidAt = now;
    }

    // Save the bill, stock deduction, and release of the hold together.
    auto saved = saveSale(store_, data.value, sale, heldOrderId == nullptr);
    if (!saved.success) {
        return {false, "Could not save the bill. " + saved.message, {}};
    }
    if (status == OrderStatus::Paid) {
        return completePaidSave(std::move(sale), std::move(saved.message));
    }
    return {true, std::move(saved.message), std::move(sale)};
}

Result<PaidEditDraft> CheckoutService::previewPaidEdit(const string& orderId,
                                                       const vector<QuantityChange>& changes,
                                                       const Timestamp& now) const
{
    if (orderId.empty()) {
        return {false, "Order ID is required.", {}};
    }
    auto data = store_.read();
    if (!data.success) {
        return {false, "Could not read checkout data. " + data.message, {}};
    }
    const auto sale = findSale(data.value, orderId);
    if (sale == data.value.sales.end()) {
        return {false, "Order not found.", {}};
    }
    auto edit = repricePaidEdit(*sale, changes, now, pricing_);
    if (!edit.success) {
        return {false, edit.message, {}};
    }
    // The store returned a copy, so this reuses the commit-time stock rules without saving.
    const auto originalLines = sale->lines;
    const auto stock = applyStockChanges(products_, data.value, originalLines, edit.value.draft.changes);
    if (!stock.success) {
        return {false, stock.message, {}};
    }
    return {true, {}, std::move(edit.value.draft)};
}

Result<SaleRecord> CheckoutService::applyPaidEdit(const PaidEditDraft& draft, const ManagerApproval& approval,
                                                 int64_t settledDelta, const Timestamp& now)
{
    if (!approval.draft_ || !sameDraft(*approval.draft_, draft)) {
        return {false, "The manager must approve the exact paid edit being saved.", {}};
    }
    if (settledDelta != draft.amountDelta) {
        return {false, "Confirm the exact collection or refund amount from the approved edit.", {}};
    }

    const auto settlementFailure = [settledDelta](string message) -> Result<SaleRecord> {
        if (settledDelta != 0) {
            message += " Reconcile any money already collected or refunded before trying again.";
        }
        return {false, std::move(message), {}};
    };

    auto data = store_.read();
    if (!data.success) {
        return settlementFailure("Could not read checkout data. " + data.message);
    }
    const auto sale = findSale(data.value, draft.orderId);
    if (sale == data.value.sales.end()) {
        return settlementFailure("Order not found.");
    }
    if (sale->revision != draft.revision) {
        return settlementFailure("This bill has already changed. Preview the current bill again.");
    }
    auto edit = repricePaidEdit(*sale, draft.changes, now, pricing_);
    if (!edit.success) {
        return settlementFailure(edit.message);
    }
    if (!sameDraft(draft, edit.value.draft)) {
        return settlementFailure("The quote changed. Review the new draft and get manager approval again.");
    }
    SaleRecord revised = *sale;
    const auto stock = applyStockChanges(products_, data.value, revised.lines, draft.changes);
    if (!stock.success) {
        return settlementFailure(stock.message);
    }

    const Totals& after = edit.value.price.totals;
    ++revised.revision;
    revised.adjustments.push_back({revised.revision, now, settledDelta,
                                 after.productDiscount - revised.totals.productDiscount,
                                 after.seasonalDiscount - revised.totals.seasonalDiscount,
                                 after.specialDiscount - revised.totals.specialDiscount,
                                 std::move(edit.value.adjustments)});
    revised.lines = std::move(edit.value.price.lines);
    revised.totals = after;
    const bool fullyRefunded = all_of(revised.lines.begin(), revised.lines.end(),
                                     [](const SaleLine& line) { return line.quantity == 0; });
    revised.billChange = fullyRefunded ? BillChange::CompleteRefund : BillChange::Modified;
    revised.pdfPath.clear();

    auto saved = saveSale(store_, data.value, revised, false);
    if (!saved.success) {
        return settlementFailure("Could not save the approved bill edit. " + saved.message);
    }
    return completePaidSave(std::move(revised), std::move(saved.message));
}

Result<SaleRecord> CheckoutService::completePaidSave(SaleRecord sale, string message)
{
    auto withPdf = history_.generatePdf(sale.ids.orderId);
    if (withPdf.success && withPdf.message.empty()) {
        return {true, std::move(message), std::move(withPdf.value)};
    }
    if (!message.empty()) {
        message += " ";
    }
    if (withPdf.success) {
        return {true, message + withPdf.message, std::move(withPdf.value)};
    }
    return {true, message + "Bill saved. PDF generation can be retried in Order History. " +
                  withPdf.message, std::move(sale)};
}
