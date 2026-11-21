#include "CheckoutService.h"

#include "IdGenerator.h"
#include "IPosStore.h"
#include "PricingCalculator.h"
#include "ProductService.h"

#include <algorithm>
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
    if (date.size() != 10 || date[4] != '-' || date[7] != '-' ||
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

Status validatePayment(const Payment& payment, OrderStatus status, int64_t payable)
{
    if (status == OrderStatus::OnHold) {
        if (payment.recorded || payment.amount != 0 || !payment.merchant.empty()) {
            return {false, "A held bill must have no recorded payment."};
        }
        return {true, {}};
    }

    if (!payment.recorded || payment.amount < 0 || payment.amount != payable) {
        return {false, "Record a payment equal to the total payable."};
    }
    if (hasLineBreak(payment.merchant)) {
        return {false, "The payment merchant must be on one line."};
    }

    switch (payment.mode) {
    case PaymentMode::Cash:
    case PaymentMode::Card:
    case PaymentMode::Wallet:
        return {true, {}};
    }
    return {false, "Choose cash, card, or wallet as the payment mode."};
}

Result<vector<SaleLine>> buildSaleLines(const vector<CartItem>& items,
                                       const vector<Product>& catalogue,
                                       const ProductService& products)
{
    vector<SaleLine> lines;
    lines.reserve(items.size());
    unordered_set<string> seenProducts;

    for (const CartItem& item : items) {
        const Product& captured = item.product;
        if (captured.id.empty() || captured.barcode.empty() || captured.acronym.empty() ||
            captured.acronym.size() > 8 || hasLineBreak(captured.acronym)) {
            return {false, "A cart item has invalid product details.", {}};
        }

        // CartService merges repeated additions into one row per product.
        if (!seenProducts.insert(captured.id).second) {
            return {false, "The cart has multiple rows for " + captured.acronym + ".", {}};
        }

        const auto live = find_if(
            catalogue.begin(), catalogue.end(),
            [&captured](const Product& product) { return product.id == captured.id; });

        if (live == catalogue.end() || live->barcode != captured.barcode) {
            return {false, "Product " + captured.id + " no longer matches the catalogue.", {}};
        }
        if (live->unit != captured.unit) {
            return {false, "Product measurement unit has changed. Restore the original unit "
                           "or remove this item from the order.", {}};
        }
        if (item.quantity <= 0 || (captured.unit != Unit::Count && captured.unit != Unit::Weight) ||
            (captured.unit == Unit::Count && item.quantity % 1000 != 0)) {
            return {false, "Invalid quantity or measurement unit for " + captured.acronym + ".", {}};
        }
        if (!products.hasStock(*live, item.quantity)) {
            return {false, "Insufficient stock for " + captured.acronym + ".", {}};
        }

        lines.push_back({captured.id, captured.barcode, captured.acronym,
                         captured.unitPrice, captured.discountPerUnit, item.quantity, captured.taxPerUnit});
    }

    return {true, {}, move(lines)};
}

} // namespace

CheckoutService::CheckoutService(IPosStore& store, ProductService& products,
                                 const PricingCalculator& pricing, const IdGenerator& ids)
    : store_(store), products_(products), pricing_(pricing), ids_(ids)
{
}

Result<SaleRecord> CheckoutService::generateBill(const vector<CartItem>& items,
                                                const Customer& customer,
                                                const Payment& payment,
                                                OrderStatus status,
                                                const Timestamp& timestamp)
{
    return save(items, customer, payment, status, timestamp, nullptr);
}

Result<SaleRecord> CheckoutService::save(const vector<CartItem>& items, const Customer& customer,
                                        const Payment& payment, OrderStatus status,
                                        const Timestamp& timestamp, const string* orderId)
{
    if (status != OrderStatus::Paid && status != OrderStatus::OnHold) {
        return {false, "A new bill must be paid or put on hold.", {}};
    }
    if (items.empty()) {
        return {false, "Add at least one item to generate a bill.", {}};
    }
    if (customer.name.find_first_not_of(" \t\r\n\v\f") == string::npos) {
        return {false, "Enter the customer's name.", {}};
    }
    if (hasLineBreak(customer.name) || hasLineBreak(customer.contact)) {
        return {false, "Customer name and contact must each be on one line.", {}};
    }
    const string_view contact = customer.contact;
    const size_t firstDigit = !contact.empty() && contact.front() == '+' ? 1 : 0;
    const size_t digitCount = contact.size() - firstDigit;
    if (!contact.empty() && (digitCount < 7 || digitCount > 15 ||
        !all_of(contact.begin() + firstDigit, contact.end(),
                [](char c) { return c >= '0' && c <= '9'; }))) {
        return {false, "Contact must contain 7–15 digits, with an optional leading +.", {}};
    }
    if (!validTimestamp(timestamp)) {
        return {false, "Enter a valid date and time (YYYY-MM-DD and HH:mm:ss).", {}};
    }

    // Validate and deduct against the same store copy that will be saved.
    auto data = store_.read();
    if (!data.success) {
        return {false, "Could not read checkout data. " + data.message, {}};
    }

    auto original = data.value.sales.end();
    if (orderId) {
        original = find_if(data.value.sales.begin(), data.value.sales.end(),
                           [&](const SaleRecord& sale) { return sale.ids.orderId == *orderId; });
        if (original == data.value.sales.end() || original->status != OrderStatus::OnHold || original->payment.recorded)
            return {false, "Order is no longer an unpaid held order.", {}};
    }
    auto lines = buildSaleLines(items, data.value.products, products_);
    if (!lines.success) {
        return {false, lines.message, {}};
    }

    // Reuse the cart's rate validation and rounding, using the captured prices.
    const auto totals = pricing_.calculate(items);
    if (!totals.success) {
        return {false, "Could not calculate the bill. " + totals.message, {}};
    }

    bool unchangedLegacy = original != data.value.sales.end() && original->lines.size() == items.size();
    bool legacy = false;
    if (unchangedLegacy) {
        for (size_t i = 0; i < items.size(); ++i) {
            const auto& line = original->lines[i];
            const auto& item = items[i];
            auto live = find_if(data.value.products.begin(), data.value.products.end(), [&](const Product& p) { return p.id == line.productId; });
            legacy |= !line.taxPerUnit.has_value();
            if (live == data.value.products.end() || item.product.id != line.productId || item.product.barcode != line.barcode ||
                item.product.acronym != line.acronym || item.product.unitPrice != line.unitPrice ||
                item.product.discountPerUnit != line.discountPerUnit || item.quantity != line.quantity ||
                (line.taxPerUnit && item.product.taxPerUnit != *line.taxPerUnit)) unchangedLegacy = false;
        }
    }
    unchangedLegacy = unchangedLegacy && legacy;
    Totals finalTotals = unchangedLegacy ? original->totals : totals.value;
    const Status paymentStatus = validatePayment(payment, status, finalTotals.payable);
    if (!paymentStatus.success) {
        return {false, paymentStatus.message, {}};
    }

    SaleRecord sale;
    if (original != data.value.sales.end()) {
        sale.ids = original->ids;
        sale.timestamp = original->timestamp;
    } else {
        auto ids = ids_.saleIds(data.value, timestamp);
        if (!ids.success) return {false, "Could not generate sale IDs. " + ids.message, {}};
        sale.ids = move(ids.value);
        sale.timestamp = timestamp;
    }
    sale.customer = customer;
    sale.status = status;
    sale.lines = move(lines.value);
    sale.totals = finalTotals;
    if (unchangedLegacy) sale.lines = original->lines;

    if (status == OrderStatus::Paid) {
        sale.payment = payment;
        const Status deduction = products_.deductStock(data.value.products, sale.lines);
        if (!deduction.success) {
            return {false, "Could not deduct stock. " + deduction.message, {}};
        }
    }

    // Submit the sale and any stock change together through one save.
    if (original != data.value.sales.end()) *original = sale;
    else data.value.sales.push_back(sale);
    const Status saved = store_.save(data.value);
    if (!saved.success) {
        return {false, "Could not save the bill. " + saved.message, {}};
    }

    return {true, {}, move(sale)};
}

Result<HeldCheckout> CheckoutService::resumeHeldOrder(const string& orderId) const
{
    auto data = store_.read();
    if (!data.success) return {false, data.message, {}};
    auto sale = find_if(data.value.sales.begin(), data.value.sales.end(), [&](const SaleRecord& s) { return s.ids.orderId == orderId; });
    if (sale == data.value.sales.end() || sale->status != OrderStatus::OnHold || sale->payment.recorded)
        return {false, "Order is not an unpaid held order.", {}};
    HeldCheckout held;
    held.sale = *sale;
    for (const auto& line : sale->lines) {
        auto product = find_if(data.value.products.begin(), data.value.products.end(), [&](const Product& p) { return p.id == line.productId && p.barcode == line.barcode; });
        if (product == data.value.products.end()) return {false, "Held product no longer exists.", {}};
        CartItem item{*product, line.quantity};
        item.product.acronym = line.acronym;
        item.product.unitPrice = line.unitPrice;
        item.product.discountPerUnit = line.discountPerUnit;
        if (line.taxPerUnit) item.product.taxPerUnit = *line.taxPerUnit;
        else held.usesLegacyTax = true;
        held.items.push_back(move(item));
    }
    return {true, {}, move(held)};
}

Result<SaleRecord> CheckoutService::saveHeldOrder(const string& orderId, const vector<CartItem>& items,
                                                   const Customer& customer, const Payment& payment, OrderStatus status)
{
    auto held = resumeHeldOrder(orderId);
    if (!held.success) return {false, held.message, {}};
    return save(items, customer, payment, status, held.value.sale.timestamp, &orderId);
}
