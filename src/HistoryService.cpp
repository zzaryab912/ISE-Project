#include "HistoryService.h"

#include "IPosStore.h"
#include "ProductService.h"

#include <algorithm>
#include <unordered_set>
#include <utility>

using namespace std;

namespace {

SaleRecord* findSale(StoreData& data, const string& orderId)
{
    const auto sale = find_if(
        data.sales.begin(), data.sales.end(),
        [&orderId](const SaleRecord& record) {
            return record.ids.orderId == orderId;
        });

    return sale == data.sales.end() ? nullptr : &*sale;
}

Status validatePayment(const Payment& payment, int64_t payable)
{
    if (!payment.recorded || payment.amount < 0 || payment.amount != payable) {
        return {false, "Record a payment equal to the saved total payable."};
    }
    if (payment.merchant.find_first_of("\r\n") != string::npos) {
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

bool samePayment(const Payment& left, const Payment& right)
{
    return left.recorded == right.recorded &&
           left.amount == right.amount &&
           left.mode == right.mode &&
           left.merchant == right.merchant;
}

Status validateHeldStock(const SaleRecord& sale, const vector<Product>& catalogue,
                         const ProductService& products)
{
    if (sale.lines.empty()) {
        return {false, "The held bill has no items."};
    }

    unordered_set<string> seenProducts;
    for (const SaleLine& line : sale.lines) {
        if (line.productId.empty() || line.barcode.empty() ||
            !seenProducts.insert(line.productId).second) {
            return {false, "The held bill contains an invalid or duplicate product."};
        }

        const auto live = find_if(
            catalogue.begin(), catalogue.end(),
            [&line](const Product& product) { return product.id == line.productId; });

        if (live == catalogue.end() || live->barcode != line.barcode) {
            return {false, "Product " + line.productId + " no longer matches the catalogue."};
        }

        // ProductService must prevent unit changes while a held bill references the product.
        if (line.quantity <= 0 || (live->unit != Unit::Count && live->unit != Unit::Weight) ||
            (live->unit == Unit::Count && line.quantity % 1000 != 0)) {
            return {false, "Invalid quantity or measurement unit for " + line.acronym + "."};
        }
        if (!products.hasStock(*live, line.quantity)) {
            return {false, "Requested quantity must be less than available stock for " + line.acronym + "."};
        }
    }

    return {true, {}};
}

} // namespace

HistoryService::HistoryService(IPosStore& store, ProductService& products)
    : store_(store), products_(products)
{
}

Result<vector<SaleRecord>> HistoryService::list() const
{
    auto data = store_.read();
    if (!data.success) {
        return {false, "Could not read order history. " + data.message, {}};
    }

    auto& sales = data.value.sales;
    sales.erase(
        remove_if(sales.begin(), sales.end(),
            [](const SaleRecord& sale) { return sale.status == OrderStatus::Archived; }),
        sales.end());

    return {true, {}, move(sales)};
}

Result<SaleRecord> HistoryService::get(const string& orderId) const
{
    auto data = store_.read();
    if (!data.success) {
        return {false, "Could not read order history. " + data.message, {}};
    }

    auto* sale = findSale(data.value, orderId);
    if (!sale) {
        return {false, "Order not found.", {}};
    }

    return {true, {}, move(*sale)};
}

Result<SaleRecord> HistoryService::markPaid(const string& orderId, const Payment& payment)
{
    auto data = store_.read();
    if (!data.success) {
        return {false, "Could not read order history. " + data.message, {}};
    }

    auto* sale = findSale(data.value, orderId);
    if (!sale) {
        return {false, "Order not found.", {}};
    }
    if (sale->status != OrderStatus::OnHold && sale->status != OrderStatus::Paid) {
        return {false, "Only a held bill can accept a payment.", {}};
    }

    const Status validPayment = validatePayment(payment, sale->totals.payable);
    if (!validPayment.success) {
        return {false, validPayment.message, {}};
    }

    if (sale->status == OrderStatus::Paid) {
        if (samePayment(sale->payment, payment)) {
            // A repeated request returns the existing bill without another save.
            return {true, {}, move(*sale)};
        }
        return {false, "This bill was already paid with different payment details.", {}};
    }
    if (sale->payment.recorded || sale->payment.amount != 0 || !sale->payment.merchant.empty()) {
        return {false, "The held bill already contains payment data.", {}};
    }

    const Status stock = validateHeldStock(*sale, data.value.products, products_);
    if (!stock.success) {
        return {false, stock.message, {}};
    }

    const Status deduction = products_.deductStock(data.value.products, sale->lines);
    if (!deduction.success) {
        return {false, "Could not deduct stock. " + deduction.message, {}};
    }

    // Save the payment and stock deduction together.
    sale->payment = payment;
    sale->status = OrderStatus::Paid;

    const Status saved = store_.save(data.value);
    if (!saved.success) {
        return {false, "Could not save the payment. " + saved.message, {}};
    }

    return {true, {}, move(*sale)};
}

Status HistoryService::archive(const string& orderId)
{
    auto data = store_.read();
    if (!data.success) {
        return {false, "Could not read order history. " + data.message};
    }

    auto* sale = findSale(data.value, orderId);
    if (!sale) {
        return {false, "Order not found."};
    }
    if (sale->status == OrderStatus::Archived) {
        return {true, {}};
    }
    if (sale->status != OrderStatus::OnHold && sale->status != OrderStatus::Paid) {
        return {false, "Only a saved bill can be archived."};
    }

    sale->status = OrderStatus::Archived;
    return store_.save(data.value);
}
