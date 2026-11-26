#include "ReportService.h"

#include "IPosStore.h"
#include "ProductService.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <map>
#include <numeric>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

using namespace std;

namespace {

bool parseDecimal(string_view text, int& out)
{
    if (text.empty()) {
        return false;
    }
    int value = 0;
    for (char ch : text) {
        if (ch < '0' || ch > '9') {
            return false;
        }
        value = value * 10 + (ch - '0');
    }
    out = value;
    return true;
}

bool parseDate(string_view date, int& year, int& month, int& day)
{
    if (date.size() != 10 || date[4] != '-' || date[7] != '-') {
        return false;
    }
    if (!parseDecimal(date.substr(0, 4), year) ||
        !parseDecimal(date.substr(5, 2), month) ||
        !parseDecimal(date.substr(8, 2), day)) {
        return false;
    }
    if (year < 1 || month < 1 || month > 12 || day < 1) {
        return false;
    }
    constexpr int monthLengths[] = {31, 28, 31, 30, 31, 30,
                                    31, 31, 30, 31, 30, 31};
    const bool leap = year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
    const int lastDay = monthLengths[month - 1] + (month == 2 && leap ? 1 : 0);
    return day <= lastDay;
}

int daysInMonth(int year, int month)
{
    constexpr int monthLengths[] = {31, 28, 31, 30, 31, 30,
                                    31, 31, 30, 31, 30, 31};
    const bool leap = year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
    return monthLengths[month - 1] + (month == 2 && leap ? 1 : 0);
}

// Howard Hinnant's civil calendar conversion; day 0 = 1970-01-01 (Thursday).
int64_t daysFromCivil(int64_t year, int month, int day)
{
    year -= month <= 2;
    const int64_t era = (year >= 0 ? year : year - 399) / 400;
    const uint64_t yoe = static_cast<uint64_t>(year - era * 400);
    const uint64_t doy = (153 * static_cast<uint64_t>(month + (month > 2 ? -3 : 9)) + 2) / 5 +
                         static_cast<uint64_t>(day) - 1;
    const uint64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + static_cast<int64_t>(doe) - 719468;
}

void civilFromDays(int64_t z, int& year, int& month, int& day)
{
    z += 719468;
    const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    const uint64_t doe = static_cast<uint64_t>(z - era * 146097);
    const uint64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const int64_t y = static_cast<int64_t>(yoe) + era * 400;
    const uint64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const uint64_t mp = (5 * doy + 2) / 153;
    const uint64_t d = doy - (153 * mp + 2) / 5 + 1;
    const uint64_t m = mp + (mp < 10 ? 3 : -9);
    year = static_cast<int>(y + (m <= 2));
    month = static_cast<int>(m);
    day = static_cast<int>(d);
}

string formatDate(int year, int month, int day)
{
    ostringstream oss;
    oss << setw(4) << setfill('0') << year << '-'
        << setw(2) << setfill('0') << month << '-'
        << setw(2) << setfill('0') << day;
    return oss.str();
}

bool validKind(ReportKind kind)
{
    switch (kind) {
    case ReportKind::Incomplete:
    case ReportKind::Completed:
    case ReportKind::CashSales:
    case ReportKind::AllSales:
    case ReportKind::PaymentMix:
    case ReportKind::TopSold:
    case ReportKind::HighestStock:
    case ReportKind::LowestStock:
        return true;
    }
    return false;
}

bool validPeriod(ReportPeriod period)
{
    switch (period) {
    case ReportPeriod::Day:
    case ReportPeriod::Week:
    case ReportPeriod::Month:
    case ReportPeriod::Year:
    case ReportPeriod::AllTime:
    case ReportPeriod::Custom:
        return true;
    }
    return false;
}

bool validPaymentMode(PaymentMode mode)
{
    switch (mode) {
    case PaymentMode::Cash:
    case PaymentMode::Card:
    case PaymentMode::Wallet:
        return true;
    }
    return false;
}

bool validUnit(Unit unit)
{
    return unit == Unit::Count || unit == Unit::Weight;
}

Status resolveDateBounds(const ReportRequest& request, string& startDate, string& endDate)
{
    startDate.clear();
    endDate.clear();

    if (request.period == ReportPeriod::AllTime) {
        return {true, {}};
    }

    if (request.period == ReportPeriod::Custom) {
        int sy = 0, sm = 0, sd = 0;
        int ey = 0, em = 0, ed = 0;
        if (!parseDate(request.startDate, sy, sm, sd) ||
            !parseDate(request.endDate, ey, em, ed)) {
            return {false, "Enter valid custom start and end dates in YYYY-MM-DD format."};
        }
        if (request.startDate > request.endDate) {
            return {false, "Custom report start date cannot be after the end date."};
        }
        startDate = request.startDate;
        endDate = request.endDate;
        return {true, {}};
    }

    int year = 0, month = 0, day = 0;
    if (!parseDate(request.anchorDate, year, month, day)) {
        return {false, "Enter a valid report date in YYYY-MM-DD format."};
    }

    switch (request.period) {
    case ReportPeriod::Day:
        startDate = request.anchorDate;
        endDate = request.anchorDate;
        return {true, {}};

    case ReportPeriod::Week: {
        // Monday = 0 .. Sunday = 6; 1970-01-01 (day 0) was Thursday (3).
        const int64_t anchorDays = daysFromCivil(year, month, day);
        const int64_t weekdayMon0 = ((anchorDays + 3) % 7 + 7) % 7;
        const int64_t mondayDays = anchorDays - weekdayMon0;
        const int64_t sundayDays = mondayDays + 6;
        int my = 0, mm = 0, md = 0;
        int suy = 0, sum = 0, sud = 0;
        civilFromDays(mondayDays, my, mm, md);
        civilFromDays(sundayDays, suy, sum, sud);
        if (my < 1 || suy > 9999) {
            return {false, "Computed week bounds fall outside supported calendar years."};
        }
        startDate = formatDate(my, mm, md);
        endDate = formatDate(suy, sum, sud);
        return {true, {}};
    }

    case ReportPeriod::Month:
        startDate = formatDate(year, month, 1);
        endDate = formatDate(year, month, daysInMonth(year, month));
        return {true, {}};

    case ReportPeriod::Year:
        startDate = formatDate(year, 1, 1);
        endDate = formatDate(year, 12, 31);
        return {true, {}};

    case ReportPeriod::AllTime:
    case ReportPeriod::Custom:
        return {true, {}};
    }
    return {false, "Unsupported report period."};
}

bool dateInRange(const string& date, const string& startDate, const string& endDate)
{
    if (startDate.empty() && endDate.empty()) {
        return true;
    }
    return date >= startDate && date <= endDate;
}

bool checkedAdd(int64_t& target, int64_t delta)
{
    if (delta > 0 && target > numeric_limits<int64_t>::max() - delta) {
        return false;
    }
    if (delta < 0 && target < numeric_limits<int64_t>::min() - delta) {
        return false;
    }
    target += delta;
    return true;
}

bool checkedSub(int64_t& target, int64_t delta)
{
    if (delta == numeric_limits<int64_t>::min()) {
        if (target > -1) {
            return false;
        }
        target -= delta;
        return true;
    }
    return checkedAdd(target, -delta);
}

bool modeMatchesFilter(PaymentMode saleMode, const vector<PaymentMode>& allowed)
{
    if (allowed.empty()) {
        return true;
    }
    return any_of(allowed.begin(), allowed.end(),
                  [saleMode](PaymentMode mode) { return mode == saleMode; });
}

vector<ProductReportRow> rankTopTenGroups(vector<ProductReportRow> countRows,
                                          vector<ProductReportRow> weightRows,
                                          const optional<Unit>& unitFilter,
                                          bool sortAscending)
{
    auto comparator = [sortAscending](const ProductReportRow& a, const ProductReportRow& b) {
        const int64_t leftVal = a.stock.has_value() ? a.stock->available : a.quantitySold;
        const int64_t rightVal = b.stock.has_value() ? b.stock->available : b.quantitySold;
        if (leftVal != rightVal) {
            return sortAscending ? (leftVal < rightVal) : (leftVal > rightVal);
        }
        return a.productId < b.productId;
    };

    vector<ProductReportRow> combined;
    if (!unitFilter.has_value() || *unitFilter == Unit::Count) {
        sort(countRows.begin(), countRows.end(), comparator);
        if (countRows.size() > 10) {
            countRows.resize(10);
        }
        combined.insert(combined.end(),
                        make_move_iterator(countRows.begin()),
                        make_move_iterator(countRows.end()));
    }
    if (!unitFilter.has_value() || *unitFilter == Unit::Weight) {
        sort(weightRows.begin(), weightRows.end(), comparator);
        if (weightRows.size() > 10) {
            weightRows.resize(10);
        }
        combined.insert(combined.end(),
                        make_move_iterator(weightRows.begin()),
                        make_move_iterator(weightRows.end()));
    }
    return combined;
}

Status generateIncomplete(const StoreData& data,
                          const ProductService& products,
                          ReportResult& result)
{
    unordered_map<string, const Product*> liveProducts;
    for (const Product& product : data.products) {
        liveProducts.emplace(product.id, &product);
    }

    for (const SaleRecord& sale : data.sales) {
        if (sale.archived || sale.status != OrderStatus::OnHold) {
            continue;
        }

        OrderReportRow row;
        row.ids = sale.ids;
        row.customerName = sale.customer.name;
        row.at = sale.createdAt;
        row.status = sale.status;
        row.billChange = sale.billChange;
        row.mode = nullopt;
        row.totals = sale.totals;
        row.collected = 0;
        row.refunded = 0;

        for (const SaleLine& line : sale.lines) {
            if (line.quantity <= 0) {
                continue;
            }
            StockPosition stockPos;
            const auto liveIt = liveProducts.find(line.productId);
            if (liveIt != liveProducts.end()) {
                const Result<StockPosition> posResult =
                    products.stockPosition(data, line.productId, "");
                if (!posResult.success) {
                    return {false, posResult.message};
                }
                stockPos = posResult.value;
            }
            row.reservations.push_back({
                line.productId,
                line.name,
                line.unit,
                line.quantity,
                stockPos
            });
        }

        if (!checkedAdd(result.billTotal, sale.totals.payable)) {
            return {false, "Report bill total exceeds the supported range."};
        }
        result.orders.push_back(std::move(row));
    }

    sort(result.orders.begin(), result.orders.end(),
         [](const OrderReportRow& a, const OrderReportRow& b) {
             if (a.at.epochSeconds != b.at.epochSeconds) {
                 return a.at.epochSeconds < b.at.epochSeconds;
             }
             return a.ids.orderId < b.ids.orderId;
         });
    return {true, {}};
}

Status generateSalesReport(const StoreData& data,
                           const ReportRequest& request,
                           const string& startDate,
                           const string& endDate,
                           ReportResult& result)
{
    const bool includeAdjustOnly =
        (request.kind == ReportKind::CashSales || request.kind == ReportKind::AllSales);
    const vector<PaymentMode> effectiveModes =
        (request.kind == ReportKind::CashSales)
            ? vector<PaymentMode>{PaymentMode::Cash}
            : request.modes;

    for (const SaleRecord& sale : data.sales) {
        if (sale.archived || sale.status != OrderStatus::Paid ||
            !sale.paidAt.has_value() || !sale.payment.recorded) {
            continue;
        }
        if (!modeMatchesFilter(sale.payment.mode, effectiveModes)) {
            continue;
        }

        int64_t allDeltaPayable = 0;
        int64_t allDeltaDiscount = 0;
        for (const BillAdjustment& adj : sale.adjustments) {
            if (!checkedAdd(allDeltaPayable, adj.amountDelta) ||
                !checkedAdd(allDeltaDiscount, adj.productDiscountDelta) ||
                !checkedAdd(allDeltaDiscount, adj.seasonalDiscountDelta) ||
                !checkedAdd(allDeltaDiscount, adj.specialDiscountDelta)) {
                return {false, "Sale adjustment totals exceed the supported range."};
            }
        }

        int64_t currentDiscount = sale.totals.productDiscount;
        if (!checkedAdd(currentDiscount, sale.totals.seasonalDiscount) ||
            !checkedAdd(currentDiscount, sale.totals.specialDiscount)) {
            return {false, "Saved bill discount totals exceed the supported range."};
        }
        int64_t initialPayable = sale.totals.payable;
        int64_t initialDiscount = currentDiscount;
        if (!checkedSub(initialPayable, allDeltaPayable) ||
            !checkedSub(initialDiscount, allDeltaDiscount)) {
            return {false, "Reconstructed initial bill totals exceed the supported range."};
        }

        const bool paidInPeriod = dateInRange(sale.paidAt->date, startDate, endDate);

        int64_t periodAdjCollected = 0;
        int64_t periodAdjRefunded = 0;
        int64_t periodAdjDiscount = 0;
        uint64_t periodAdjRefundCount = 0;
        optional<Timestamp> latestPeriodAdjAt;

        for (const BillAdjustment& adj : sale.adjustments) {
            if (!dateInRange(adj.at.date, startDate, endDate)) {
                continue;
            }
            latestPeriodAdjAt = adj.at;
            if (adj.amountDelta > 0) {
                if (!checkedAdd(periodAdjCollected, adj.amountDelta)) {
                    return {false, "Collected report totals exceed the supported range."};
                }
            } else if (adj.amountDelta < 0) {
                if (!checkedSub(periodAdjRefunded, adj.amountDelta)) {
                    return {false, "Refund report totals exceed the supported range."};
                }
                ++periodAdjRefundCount;
            }
            if (!checkedAdd(periodAdjDiscount, adj.productDiscountDelta) ||
                !checkedAdd(periodAdjDiscount, adj.seasonalDiscountDelta) ||
                !checkedAdd(periodAdjDiscount, adj.specialDiscountDelta)) {
                return {false, "Discount report totals exceed the supported range."};
            }
        }

        const bool hadAdjInPeriod = latestPeriodAdjAt.has_value();
        if (!paidInPeriod && !(includeAdjustOnly && hadAdjInPeriod)) {
            continue;
        }

        OrderReportRow row;
        row.ids = sale.ids;
        row.customerName = sale.customer.name;
        row.at = paidInPeriod ? *sale.paidAt : *latestPeriodAdjAt;
        row.status = sale.status;
        row.billChange = sale.billChange;
        row.mode = sale.payment.mode;
        row.totals = sale.totals;
        row.collected = periodAdjCollected;
        if (paidInPeriod && !checkedAdd(row.collected, initialPayable)) {
            return {false, "Order collected total exceeds the supported range."};
        }
        row.refunded = periodAdjRefunded;

        int64_t rowDiscount = periodAdjDiscount;
        if (paidInPeriod && !checkedAdd(rowDiscount, initialDiscount)) {
            return {false, "Order discount total exceeds the supported range."};
        }

        if (!checkedAdd(result.billTotal, sale.totals.payable) ||
            !checkedAdd(result.collected, row.collected) ||
            !checkedAdd(result.refunded, row.refunded) ||
            !checkedAdd(result.totalDiscount, rowDiscount)) {
            return {false, "Sales report totals exceed the supported range."};
        }
        result.refundCount += periodAdjRefundCount;
        result.orders.push_back(std::move(row));
    }

    int64_t net = result.collected;
    if (!checkedSub(net, result.refunded)) {
        return {false, "Net collections exceed the supported range."};
    }
    result.netCollections = net;

    sort(result.orders.begin(), result.orders.end(),
         [](const OrderReportRow& a, const OrderReportRow& b) {
             if (a.at.epochSeconds != b.at.epochSeconds) {
                 return a.at.epochSeconds < b.at.epochSeconds;
             }
             return a.ids.orderId < b.ids.orderId;
         });
    return {true, {}};
}

Status generatePaymentMix(const StoreData& data,
                          const string& startDate,
                          const string& endDate,
                          ReportResult& result)
{
    constexpr array<PaymentMode, 3> modes = {
        PaymentMode::Cash, PaymentMode::Card, PaymentMode::Wallet};
    array<uint64_t, 3> counts = {0, 0, 0};
    uint64_t total = 0;

    for (const SaleRecord& sale : data.sales) {
        if (sale.archived || sale.status != OrderStatus::Paid ||
            !sale.paidAt.has_value() || !sale.payment.recorded) {
            continue;
        }
        if (!dateInRange(sale.paidAt->date, startDate, endDate)) {
            continue;
        }
        for (size_t i = 0; i < modes.size(); ++i) {
            if (sale.payment.mode == modes[i]) {
                ++counts[i];
                ++total;
                break;
            }
        }
    }

    array<int32_t, 3> shares = {0, 0, 0};
    if (total > 0) {
        array<uint64_t, 3> remainders = {0, 0, 0};
        int32_t allocated = 0;
        for (size_t i = 0; i < modes.size(); ++i) {
            // Counts are bounded by data.sales.size(), well within uint64_t * 10000.
            const uint64_t num = counts[i] * 10000ULL;
            shares[i] = static_cast<int32_t>(num / total);
            remainders[i] = num % total;
            allocated += shares[i];
        }
        const int32_t leftover = 10000 - allocated;
        array<size_t, 3> order = {0, 1, 2};
        stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) {
            return remainders[a] > remainders[b];
        });
        for (int32_t k = 0; k < leftover; ++k) {
            ++shares[order[static_cast<size_t>(k)]];
        }
    }

    result.paymentMix.reserve(modes.size());
    for (size_t i = 0; i < modes.size(); ++i) {
        result.paymentMix.push_back({modes[i], counts[i], shares[i]});
    }
    return {true, {}};
}

Status generateTopSold(const StoreData& data,
                       const ProductService& products,
                       const ReportRequest& request,
                       const string& startDate,
                       const string& endDate,
                       ReportResult& result)
{
    unordered_map<string, const Product*> liveProducts;
    for (const Product& product : data.products) {
        liveProducts.emplace(product.id, &product);
    }

    struct GroupKey {
        string productId;
        Unit unit;
        bool operator<(const GroupKey& other) const
        {
            if (productId != other.productId) {
                return productId < other.productId;
            }
            return static_cast<int>(unit) < static_cast<int>(other.unit);
        }
    };

    struct SoldAggregate {
        string historicalName;
        int64_t quantitySold = 0;
    };

    map<GroupKey, SoldAggregate> aggregates;

    for (const SaleRecord& sale : data.sales) {
        if (sale.archived || sale.status != OrderStatus::Paid || !sale.paidAt.has_value()) {
            continue;
        }

        unordered_map<string, const SaleLine*> lineById;
        unordered_map<string, int64_t> initialQty;
        for (const SaleLine& line : sale.lines) {
            lineById[line.productId] = &line;
            initialQty[line.productId] = line.quantity;
        }

        // Reverse adjustments to recover initial sold quantities at payment time.
        for (size_t idx = sale.adjustments.size(); idx > 0; --idx) {
            const BillAdjustment& adj = sale.adjustments[idx - 1];
            for (const LineAdjustment& la : adj.lines) {
                initialQty[la.productId] = la.previousQuantity;
            }
        }

        if (dateInRange(sale.paidAt->date, startDate, endDate)) {
            for (const SaleLine& line : sale.lines) {
                if (request.unit.has_value() && line.unit != *request.unit) {
                    continue;
                }
                const int64_t qty = initialQty[line.productId];
                auto& entry = aggregates[{line.productId, line.unit}];
                entry.historicalName = line.name;
                if (!checkedAdd(entry.quantitySold, qty)) {
                    return {false, "Sold quantity total exceeds the supported range."};
                }
            }
        }

        for (const BillAdjustment& adj : sale.adjustments) {
            if (!dateInRange(adj.at.date, startDate, endDate)) {
                continue;
            }
            for (const LineAdjustment& la : adj.lines) {
                const auto lineIt = lineById.find(la.productId);
                if (lineIt == lineById.end()) {
                    continue;
                }
                const SaleLine& line = *lineIt->second;
                if (request.unit.has_value() && line.unit != *request.unit) {
                    continue;
                }
                const int64_t delta = la.quantity - la.previousQuantity;
                auto& entry = aggregates[{line.productId, line.unit}];
                entry.historicalName = line.name;
                if (!checkedAdd(entry.quantitySold, delta)) {
                    return {false, "Sold quantity total exceeds the supported range."};
                }
            }
        }
    }

    vector<ProductReportRow> countRows;
    vector<ProductReportRow> weightRows;

    for (const auto& [key, agg] : aggregates) {
        if (agg.quantitySold <= 0) {
            continue;
        }

        ProductReportRow row;
        row.productId = key.productId;
        row.unit = key.unit;
        row.quantitySold = agg.quantitySold;

        const auto liveIt = liveProducts.find(key.productId);
        if (liveIt != liveProducts.end()) {
            const Product& live = *liveIt->second;
            row.name = live.name;
            row.stockUnit = live.unit;
            const Result<StockPosition> posResult =
                products.stockPosition(data, live.id, "");
            if (!posResult.success) {
                return {false, posResult.message};
            }
            row.stock = posResult.value;
        } else {
            row.name = agg.historicalName;
        }

        if (row.unit == Unit::Count) {
            countRows.push_back(std::move(row));
        } else {
            weightRows.push_back(std::move(row));
        }
    }

    auto sortTopSold = [](vector<ProductReportRow>& rows) {
        sort(rows.begin(), rows.end(),
             [](const ProductReportRow& a, const ProductReportRow& b) {
                 if (a.quantitySold != b.quantitySold) {
                     return a.quantitySold > b.quantitySold;
                 }
                 return a.productId < b.productId;
             });
        if (rows.size() > 10) {
            rows.resize(10);
        }
    };

    if (!request.unit.has_value() || *request.unit == Unit::Count) {
        sortTopSold(countRows);
        result.products.insert(result.products.end(),
                               make_move_iterator(countRows.begin()),
                               make_move_iterator(countRows.end()));
    }
    if (!request.unit.has_value() || *request.unit == Unit::Weight) {
        sortTopSold(weightRows);
        result.products.insert(result.products.end(),
                               make_move_iterator(weightRows.begin()),
                               make_move_iterator(weightRows.end()));
    }
    return {true, {}};
}

Status generateStockRanking(const StoreData& data,
                            const ProductService& products,
                            const ReportRequest& request,
                            bool lowestFirst,
                            ReportResult& result)
{
    vector<ProductReportRow> countRows;
    vector<ProductReportRow> weightRows;

    for (const Product& product : data.products) {
        if (request.unit.has_value() && product.unit != *request.unit) {
            continue;
        }
        const Result<StockPosition> posResult =
            products.stockPosition(data, product.id, "");
        if (!posResult.success) {
            return {false, posResult.message};
        }
        const StockPosition& pos = posResult.value;
        if (pos.available == 0) {
            ++result.outOfStockCount;
        }

        ProductReportRow row;
        row.productId = product.id;
        row.name = product.name;
        row.unit = product.unit;
        row.quantitySold = 0;
        row.stockUnit = product.unit;
        row.stock = pos;

        if (product.unit == Unit::Count) {
            countRows.push_back(std::move(row));
        } else {
            weightRows.push_back(std::move(row));
        }
    }

    result.products = rankTopTenGroups(
        std::move(countRows), std::move(weightRows), request.unit, lowestFirst);
    return {true, {}};
}

} // namespace

ReportService::ReportService(const IPosStore& store, const ProductService& products)
    : store_(store), products_(products)
{
}

Result<ReportResult> ReportService::generate(const ReportRequest& request) const
{
    if (!validKind(request.kind)) {
        return {false, "Unsupported report kind.", {}};
    }
    if (!validPeriod(request.period)) {
        return {false, "Unsupported report period.", {}};
    }
    for (PaymentMode mode : request.modes) {
        if (!validPaymentMode(mode)) {
            return {false, "Unsupported payment mode filter.", {}};
        }
    }
    if (request.unit.has_value() && !validUnit(*request.unit)) {
        return {false, "Unsupported measurement unit filter.", {}};
    }

    const bool usesDateRange =
        request.kind == ReportKind::Completed ||
        request.kind == ReportKind::CashSales ||
        request.kind == ReportKind::AllSales ||
        request.kind == ReportKind::PaymentMix ||
        request.kind == ReportKind::TopSold;

    string startDate;
    string endDate;
    if (usesDateRange) {
        const Status dateStatus = resolveDateBounds(request, startDate, endDate);
        if (!dateStatus.success) {
            return {false, dateStatus.message, {}};
        }
    }

    const Result<StoreData> storeResult = store_.read();
    if (!storeResult.success) {
        return {false,
                storeResult.message.empty() ? "Failed to read storage." : storeResult.message,
                {}};
    }

    ReportResult result;
    result.startDate = startDate;
    result.endDate = endDate;

    Status reportStatus;
    switch (request.kind) {
    case ReportKind::Incomplete:
        reportStatus = generateIncomplete(storeResult.value, products_, result);
        break;
    case ReportKind::Completed:
    case ReportKind::CashSales:
    case ReportKind::AllSales:
        reportStatus = generateSalesReport(
            storeResult.value, request, startDate, endDate, result);
        break;
    case ReportKind::PaymentMix:
        reportStatus = generatePaymentMix(
            storeResult.value, startDate, endDate, result);
        break;
    case ReportKind::TopSold:
        reportStatus = generateTopSold(
            storeResult.value, products_, request, startDate, endDate, result);
        break;
    case ReportKind::HighestStock:
        reportStatus = generateStockRanking(
            storeResult.value, products_, request, false, result);
        break;
    case ReportKind::LowestStock:
        reportStatus = generateStockRanking(
            storeResult.value, products_, request, true, result);
        break;
    }

    if (!reportStatus.success) {
        return {false, reportStatus.message, {}};
    }
    return {true, {}, std::move(result)};
}
