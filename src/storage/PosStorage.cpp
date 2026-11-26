#include "PosStorage.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <map>
#include <numeric>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

using namespace std;

namespace {

constexpr array<const char*, 8> kInc3Files = {
    "PCategory.txt",
    "Product.txt",
    "Order.txt",
    "OrderDetail.txt",
    "Bill.txt",
    "BillDetail.txt",
    "Seasons.txt",
    "PosMeta.txt"
};

constexpr array<const char*, 4> kInc2Files = {
    "Product.txt",
    "Order.txt",
    "OrderDetail.txt",
    "Bill.txt"
};

constexpr array<const char*, 4> kInc3OnlyFiles = {
    "PCategory.txt",
    "BillDetail.txt",
    "Seasons.txt",
    "PosMeta.txt"
};

constexpr int32_t kMaxBasisPoints = 10000;
constexpr int32_t kMaxSpecialBasisPoints = 800;
constexpr int64_t kMaxMoney = numeric_limits<int64_t>::max();

struct Field {
    string value;
    bool quoted = false;
};

// =========================================================================
// String & token helpers
// =========================================================================

string trim(const string& s)
{
    const size_t start = s.find_first_not_of(" \t\r\n");
    if (start == string::npos) {
        return {};
    }
    const size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

string toLower(const string& s)
{
    string out = s;
    transform(out.begin(), out.end(), out.begin(), [](unsigned char ch) {
        return static_cast<char>(tolower(ch));
    });
    return out;
}

bool hasLineBreak(const string& s)
{
    return s.find_first_of("\r\n") != string::npos;
}

bool isNumeric(string_view s)
{
    if (s.empty()) {
        return false;
    }
    for (unsigned char c : s) {
        if (!isdigit(c)) {
            return false;
        }
    }
    return true;
}

bool parseUint64(string_view s, uint64_t& value)
{
    if (!isNumeric(s)) {
        return false;
    }
    const char* end = s.data() + s.size();
    const auto res = from_chars(s.data(), end, value);
    return res.ec == errc{} && res.ptr == end;
}

bool parseInt64(string_view s, int64_t& value)
{
    uint64_t parsed = 0;
    if (!parseUint64(s, parsed) || parsed > static_cast<uint64_t>(kMaxMoney)) {
        return false;
    }
    value = static_cast<int64_t>(parsed);
    return true;
}

bool parseUint32(string_view s, uint32_t& value)
{
    uint64_t parsed = 0;
    if (!parseUint64(s, parsed) || parsed > numeric_limits<uint32_t>::max()) {
        return false;
    }
    value = static_cast<uint32_t>(parsed);
    return true;
}

vector<Field> slices(const string& line)
{
    vector<Field> fields;
    istringstream stream(line);

    while (stream >> ws) {
        if (stream.eof()) {
            break;
        }

        Field field;
        if (stream.peek() == '"') {
            field.quoted = true;
            stream >> quoted(field.value);
            if (stream.fail()) {
                throw runtime_error("Invalid quoted string.");
            }
            if (hasLineBreak(field.value)) {
                throw runtime_error("String fields cannot contain line breaks.");
            }
            const int separator = stream.peek();
            if (separator != char_traits<char>::eof() &&
                !isspace(static_cast<unsigned char>(separator))) {
                throw runtime_error("Missing whitespace after quoted string.");
            }
        } else {
            field.quoted = false;
            if (!(stream >> field.value)) {
                throw runtime_error("Invalid field.");
            }
            if (field.value.find('"') != string::npos) {
                throw runtime_error("Unexpected quote inside unquoted field.");
            }
        }

        fields.push_back(std::move(field));
    }

    return fields;
}

vector<Field> removeRecordMarker(const string& line)
{
    vector<Field> fields = slices(line);
    if (fields.empty() || fields.back().quoted || fields.back().value != "#") {
        throw runtime_error("Missing # record marker.");
    }
    fields.pop_back();
    return fields;
}

string removeQuotes(const Field& field, const string& fieldName)
{
    if (!field.quoted) {
        throw runtime_error(fieldName + " must be enclosed in double quotes.");
    }
    return field.value;
}

string removeUnquoted(const Field& field, const string& fieldName)
{
    if (field.quoted) {
        throw runtime_error(fieldName + " must not be enclosed in double quotes.");
    }
    return field.value;
}

// =========================================================================
// Validation & scalar parsing
// =========================================================================

bool validDate(string_view date)
{
    if (date.size() != 10 || date[4] != '-' || date[7] != '-') {
        return false;
    }
    uint64_t year = 0, month = 0, day = 0;
    if (!parseUint64(date.substr(0, 4), year) ||
        !parseUint64(date.substr(5, 2), month) ||
        !parseUint64(date.substr(8, 2), day)) {
        return false;
    }
    if (year == 0 || month < 1 || month > 12 || day < 1) {
        return false;
    }
    constexpr uint64_t daysByMonth[] = {31, 28, 31, 30, 31, 30,
                                        31, 31, 30, 31, 30, 31};
    const bool leap = (year % 4 == 0) && (year % 100 != 0 || year % 400 == 0);
    return day <= daysByMonth[month - 1] + (month == 2 && leap ? 1ULL : 0ULL);
}

bool validTime(string_view time)
{
    if (time.size() != 8 || time[2] != ':' || time[5] != ':') {
        return false;
    }
    uint64_t hour = 0, minute = 0, second = 0;
    if (!parseUint64(time.substr(0, 2), hour) ||
        !parseUint64(time.substr(3, 2), minute) ||
        !parseUint64(time.substr(6, 2), second)) {
        return false;
    }
    return hour < 24 && minute < 60 && second < 60;
}

bool validTimestamp(const Timestamp& ts)
{
    return validDate(ts.date) && validTime(ts.time) && ts.epochSeconds > 0;
}

bool validBarcode(string_view barcode)
{
    return barcode.size() == 6 && barcode.front() >= '1' && barcode.front() <= '9' &&
           isNumeric(barcode);
}

bool validCategoryId(string_view categoryId)
{
    return categoryId.size() == 2 && categoryId[0] >= '1' && categoryId[0] <= '9' &&
           isdigit(static_cast<unsigned char>(categoryId[1]));
}

bool validRelativeSubpath(string_view pathStr, optional<string_view> requiredRoot)
{
    if (pathStr.empty() || pathStr.find_first_of("\r\n\\") != string_view::npos) {
        return false;
    }
    if (pathStr.front() == '/' || pathStr.back() == '/') {
        return false;
    }

    vector<string_view> segments;
    size_t start = 0;
    while (start <= pathStr.size()) {
        const size_t slash = pathStr.find('/', start);
        const string_view seg = (slash == string_view::npos)
                                    ? pathStr.substr(start)
                                    : pathStr.substr(start, slash - start);
        if (seg.empty() || seg == "." || seg == "..") {
            return false;
        }
        segments.push_back(seg);
        if (slash == string_view::npos) {
            break;
        }
        start = slash + 1;
    }

    if (segments.empty() || segments.front().find(':') != string_view::npos) {
        return false;
    }
    if (requiredRoot.has_value()) {
        return segments.size() >= 2 && segments.front() == *requiredRoot;
    }
    return true;
}

int64_t parseMoney(const Field& field, const string& fieldName)
{
    const string s = removeUnquoted(field, fieldName);
    const size_t dot = s.find('.');
    if (dot == string::npos || dot != s.rfind('.') || s.size() - dot - 1 != 2) {
        throw runtime_error(fieldName + " must have exactly two decimal places.");
    }
    const string whole = s.substr(0, dot);
    const string fraction = s.substr(dot + 1);
    uint64_t rupees = 0, paisa = 0;
    if (!parseUint64(whole, rupees) || !parseUint64(fraction, paisa)) {
        throw runtime_error(fieldName + " is invalid or out of range.");
    }
    const uint64_t limit = static_cast<uint64_t>(kMaxMoney);
    if (rupees > (limit - paisa) / 100ULL) {
        throw runtime_error(fieldName + " is out of range.");
    }
    return static_cast<int64_t>(rupees * 100ULL + paisa);
}

int64_t parseSignedMoney(const Field& field, const string& fieldName)
{
    string s = removeUnquoted(field, fieldName);
    bool negative = false;
    if (!s.empty() && s.front() == '-') {
        negative = true;
        s.erase(s.begin());
    }
    const size_t dot = s.find('.');
    if (dot == string::npos || dot != s.rfind('.') || s.size() - dot - 1 != 2) {
        throw runtime_error(fieldName + " must have exactly two decimal places.");
    }
    const string whole = s.substr(0, dot);
    const string fraction = s.substr(dot + 1);
    uint64_t rupees = 0, paisa = 0;
    if (!parseUint64(whole, rupees) || !parseUint64(fraction, paisa)) {
        throw runtime_error(fieldName + " is invalid or out of range.");
    }
    const uint64_t limit = negative
        ? (static_cast<uint64_t>(kMaxMoney) + 1ULL)
        : static_cast<uint64_t>(kMaxMoney);
    if (rupees > (limit - paisa) / 100ULL) {
        throw runtime_error(fieldName + " is out of range.");
    }
    const uint64_t mag = rupees * 100ULL + paisa;
    if (negative) {
        if (mag == 0) {
            throw runtime_error(fieldName + " cannot be negative zero.");
        }
        if (mag == limit) {
            return numeric_limits<int64_t>::min();
        }
        return -static_cast<int64_t>(mag);
    }
    return static_cast<int64_t>(mag);
}

int32_t parseBasisPoints(const Field& field, const string& fieldName, int32_t maxBps)
{
    const int64_t bps = parseMoney(field, fieldName);
    if (bps < 0 || bps > maxBps) {
        throw runtime_error(fieldName + " is out of the supported percentage range.");
    }
    return static_cast<int32_t>(bps);
}

int64_t parseQuantity(const Field& field, bool allowZero)
{
    const string s = removeUnquoted(field, "Quantity");
    const size_t dot = s.find('.');
    if (dot != string::npos && dot != s.rfind('.')) {
        throw runtime_error("Invalid quantity.");
    }
    const string whole = (dot == string::npos) ? s : s.substr(0, dot);
    string fraction = (dot == string::npos) ? string{} : s.substr(dot + 1);
    if (!isNumeric(whole) ||
        (dot != string::npos && (fraction.empty() || !isNumeric(fraction) || fraction.size() > 3))) {
        throw runtime_error("Invalid quantity.");
    }
    while (fraction.size() < 3) {
        fraction.push_back('0');
    }
    uint64_t intPart = 0, decPart = 0;
    if (!parseUint64(whole, intPart) ||
        (!fraction.empty() && !parseUint64(fraction, decPart))) {
        throw runtime_error("Quantity is out of range.");
    }
    const uint64_t limit = static_cast<uint64_t>(kMaxMoney);
    if (intPart > (limit - decPart) / 1000ULL) {
        throw runtime_error("Quantity is out of range.");
    }
    const int64_t quantity = static_cast<int64_t>(intPart * 1000ULL + decPart);
    if (allowZero ? quantity < 0 : quantity <= 0) {
        throw runtime_error("Invalid quantity.");
    }
    return quantity;
}

bool parseBool(const Field& field, const string& fieldName)
{
    const string s = removeUnquoted(field, fieldName);
    if (s == "true") {
        return true;
    }
    if (s == "false") {
        return false;
    }
    throw runtime_error(fieldName + " must be true or false.");
}

Unit parseUnit(const Field& field)
{
    const string s = removeUnquoted(field, "Unit");
    if (s == "count") {
        return Unit::Count;
    }
    if (s == "weight") {
        return Unit::Weight;
    }
    throw runtime_error("Unit must be count or weight.");
}

// =========================================================================
// Formatting helpers
// =========================================================================

void writeQuoted(ostream& out, const string& value)
{
    if (hasLineBreak(value)) {
        throw runtime_error("String fields cannot contain line breaks.");
    }
    out << quoted(value) << ' ';
}

string moneyString(int64_t value)
{
    if (value < 0) {
        throw runtime_error("Negative value in unsigned money field.");
    }
    ostringstream out;
    out << value / 100 << '.' << setw(2) << setfill('0') << value % 100;
    return out.str();
}

string signedMoneyString(int64_t value)
{
    const uint64_t mag = value < 0
        ? (uint64_t{0} - static_cast<uint64_t>(value))
        : static_cast<uint64_t>(value);
    ostringstream out;
    if (value < 0) {
        out << '-';
    }
    out << (mag / 100ULL) << '.' << setw(2) << setfill('0') << (mag % 100ULL);
    return out.str();
}

string basisPointsString(int32_t bps)
{
    if (bps < 0) {
        throw runtime_error("Negative rate in percentage field.");
    }
    return moneyString(bps);
}

string quantityString(Unit unit, int64_t value)
{
    if (value < 0) {
        throw runtime_error("Negative quantity cannot be serialized.");
    }
    if (unit == Unit::Count) {
        if (value % 1000 != 0) {
            throw runtime_error("Count quantity must be a whole number.");
        }
        return to_string(value / 1000);
    }
    if (unit == Unit::Weight) {
        ostringstream out;
        out << (value / 1000) << '.' << setw(3) << setfill('0') << (value % 1000);
        return out.str();
    }
    throw runtime_error("Invalid measurement unit.");
}

string unitString(Unit unit)
{
    switch (unit) {
    case Unit::Count:
        return "count";
    case Unit::Weight:
        return "weight";
    }
    throw runtime_error("Invalid unit.");
}

string orderStatusString(OrderStatus status)
{
    switch (status) {
    case OrderStatus::OnHold:
        return "on-hold";
    case OrderStatus::Paid:
        return "paid";
    case OrderStatus::InProgress:
        break;
    }
    throw runtime_error("Order status must be on-hold or paid.");
}

string billStatusString(const SaleRecord& sale)
{
    if (sale.archived) {
        return "archived";
    }
    if (sale.billChange == BillChange::CompleteRefund) {
        return "refunded";
    }
    return orderStatusString(sale.status);
}

string paymentModeString(PaymentMode mode)
{
    switch (mode) {
    case PaymentMode::Cash:
        return "cash";
    case PaymentMode::Card:
        return "card";
    case PaymentMode::Wallet:
        return "wallet";
    }
    throw runtime_error("Invalid payment mode.");
}

string billChangeString(BillChange change)
{
    switch (change) {
    case BillChange::Unchanged:
        return "unchanged";
    case BillChange::Modified:
        return "modified";
    case BillChange::CompleteRefund:
        return "complete-refund";
    }
    throw runtime_error("Invalid bill change state.");
}

// =========================================================================
// Exact 128-bit pricing calculation & verification
// =========================================================================

struct UInt128 {
    uint64_t hi = 0;
    uint64_t lo = 0;
};

UInt128 multiply64(uint64_t a, uint64_t b)
{
    const uint64_t aLo = a & 0xffffffffULL;
    const uint64_t aHi = a >> 32;
    const uint64_t bLo = b & 0xffffffffULL;
    const uint64_t bHi = b >> 32;

    const uint64_t p0 = aLo * bLo;
    const uint64_t p1 = aLo * bHi;
    const uint64_t p2 = aHi * bLo;
    const uint64_t p3 = aHi * bHi;

    const uint64_t mid = (p0 >> 32) + (p1 & 0xffffffffULL) + (p2 & 0xffffffffULL);
    const uint64_t lo = (p0 & 0xffffffffULL) | (mid << 32);
    const uint64_t hi = p3 + (p1 >> 32) + (p2 >> 32) + (mid >> 32);
    return {hi, lo};
}

UInt128 add64(UInt128 value, uint64_t addend)
{
    value.lo += addend;
    if (value.lo < addend) {
        ++value.hi;
    }
    return value;
}

bool mulDivModInt64(int64_t a, int64_t b, int64_t divisor, int64_t roundingBias,
                    int64_t& quotient, int64_t& remainder)
{
    if (a < 0 || b < 0 || divisor <= 0 || roundingBias < 0) {
        return false;
    }
    UInt128 num = multiply64(static_cast<uint64_t>(a), static_cast<uint64_t>(b));
    if (roundingBias > 0) {
        num = add64(num, static_cast<uint64_t>(roundingBias));
    }

    const uint64_t div = static_cast<uint64_t>(divisor);
    uint64_t qHi = 0, qLo = 0, rem = 0;
    for (int bit = 127; bit >= 0; --bit) {
        const uint64_t nextBit = (bit >= 64)
            ? ((num.hi >> (bit - 64)) & 1ULL)
            : ((num.lo >> bit) & 1ULL);
        rem = (rem << 1) | nextBit;
        if (rem >= div) {
            rem -= div;
            if (bit >= 64) {
                qHi |= (1ULL << (bit - 64));
            } else {
                qLo |= (1ULL << bit);
            }
        }
    }

    if (qHi != 0 || qLo > static_cast<uint64_t>(kMaxMoney)) {
        return false;
    }
    quotient = static_cast<int64_t>(qLo);
    remainder = static_cast<int64_t>(rem);
    return true;
}

bool roundedThousandths(int64_t ratePerUnit, int64_t quantity, int64_t& amount)
{
    int64_t rem = 0;
    return mulDivModInt64(ratePerUnit, quantity, 1000, 500, amount, rem);
}

bool roundedPercent(int64_t amount, int32_t basisPoints, int64_t& result)
{
    int64_t rem = 0;
    return mulDivModInt64(amount, basisPoints, kMaxBasisPoints, kMaxBasisPoints / 2, result, rem);
}

bool roundedPreDiscountTaxRate(int64_t taxPerUnit, int64_t unitPrice, int32_t& taxRateOut)
{
    if (taxPerUnit < 0 || unitPrice < 0) {
        return false;
    }
    if (unitPrice == 0) {
        if (taxPerUnit > 0) {
            return false;
        }
        taxRateOut = 0;
        return true;
    }
    int64_t rate = 0;
    int64_t rem = 0;
    if (!mulDivModInt64(taxPerUnit, kMaxBasisPoints, unitPrice, unitPrice / 2, rate, rem)) {
        return false;
    }
    if (rate < 0 || rate > kMaxBasisPoints) {
        return false;
    }
    taxRateOut = static_cast<int32_t>(rate);
    return true;
}

PricingResult computePricing(const vector<SaleLine>& lines, const PricingTerms& terms)
{
    if (terms.seasonalRate < 0 || terms.seasonalRate > kMaxBasisPoints ||
        terms.specialRate < 0 || terms.specialRate > kMaxSpecialBasisPoints ||
        terms.seasonalRate + terms.specialRate > kMaxBasisPoints) {
        throw runtime_error("Invalid seasonal or special discount rate.");
    }

    PricingResult result;
    result.lines = lines;
    result.terms = terms;
    const int32_t combinedRate = terms.seasonalRate + terms.specialRate;

    vector<int64_t> lineBases(lines.size(), 0);
    int64_t baseTotal = 0;

    for (size_t i = 0; i < result.lines.size(); ++i) {
        SaleLine& line = result.lines[i];
        line.lineProductDiscount = 0;
        line.lineTax = 0;

        if (line.quantity < 0 || line.unitPrice < 0 ||
            line.discountPerUnit < 0 || line.discountPerUnit > line.unitPrice ||
            line.taxRate < 0 || line.taxRate > kMaxBasisPoints ||
            (line.unit != Unit::Count && line.unit != Unit::Weight) ||
            (line.unit == Unit::Count && line.quantity % 1000 != 0)) {
            throw runtime_error("Invalid sale line pricing inputs.");
        }

        int64_t lineGross = 0;
        int64_t lineProdDisc = 0;
        if (!roundedThousandths(line.unitPrice, line.quantity, lineGross) ||
            !roundedThousandths(line.discountPerUnit, line.quantity, lineProdDisc) ||
            lineGross > kMaxMoney - result.totals.subtotal ||
            lineProdDisc > kMaxMoney - result.totals.productDiscount) {
            throw runtime_error("Sale line amounts exceed supported range.");
        }

        result.lineSubtotals.push_back(lineGross);
        line.lineProductDiscount = lineProdDisc;
        lineBases[i] = lineGross - lineProdDisc;

        result.totals.subtotal += lineGross;
        result.totals.productDiscount += lineProdDisc;
        baseTotal += lineBases[i];
    }

    int64_t combinedDiscount = 0;
    if (baseTotal > 0 && combinedRate > 0) {
        if (!roundedPercent(baseTotal, combinedRate, combinedDiscount)) {
            throw runtime_error("Discount amounts exceed supported range.");
        }
        if (combinedDiscount > 0) {
            int64_t seasonalRem = 0, specialRem = 0;
            if (!mulDivModInt64(combinedDiscount, terms.seasonalRate, combinedRate, 0,
                                result.totals.seasonalDiscount, seasonalRem) ||
                !mulDivModInt64(combinedDiscount, terms.specialRate, combinedRate, 0,
                                result.totals.specialDiscount, specialRem)) {
                throw runtime_error("Discount split exceeds supported range.");
            }
            const int64_t leftover =
                combinedDiscount - result.totals.seasonalDiscount - result.totals.specialDiscount;
            if (leftover > 0) {
                if (seasonalRem >= specialRem) {
                    result.totals.seasonalDiscount += leftover;
                } else {
                    result.totals.specialDiscount += leftover;
                }
            }
        }
    }

    vector<int64_t> allocated(result.lines.size(), 0);
    if (baseTotal > 0 && combinedDiscount > 0) {
        vector<int64_t> remainders(result.lines.size(), 0);
        int64_t allocatedSum = 0;
        for (size_t i = 0; i < result.lines.size(); ++i) {
            if (!mulDivModInt64(combinedDiscount, lineBases[i], baseTotal, 0,
                                allocated[i], remainders[i])) {
                throw runtime_error("Discount allocation exceeds supported range.");
            }
            allocatedSum += allocated[i];
        }
        const int64_t leftover = combinedDiscount - allocatedSum;
        if (leftover > 0) {
            vector<size_t> order(result.lines.size());
            iota(order.begin(), order.end(), 0);
            stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) {
                return remainders[a] > remainders[b];
            });
            for (int64_t k = 0; k < leftover; ++k) {
                ++allocated[order[static_cast<size_t>(k)]];
            }
        }
    }

    for (size_t i = 0; i < result.lines.size(); ++i) {
        const int64_t discountedLine = lineBases[i] - allocated[i];
        int64_t lineTax = 0;
        if (!roundedPercent(discountedLine, result.lines[i].taxRate, lineTax) ||
            lineTax > kMaxMoney - result.totals.tax) {
            throw runtime_error("Tax amounts exceed supported range.");
        }
        result.lines[i].lineTax = lineTax;
        result.totals.tax += lineTax;
    }

    const int64_t netMerchandise = baseTotal - combinedDiscount;
    if (result.totals.tax > kMaxMoney - netMerchandise) {
        throw runtime_error("Payable amount exceeds supported range.");
    }
    const int64_t exactPayable = netMerchandise + result.totals.tax;
    const int64_t paiseRemainder = exactPayable % 100;
    int64_t roundingAdjustment = 0;
    if (paiseRemainder >= 50) {
        roundingAdjustment = 100 - paiseRemainder;
        if (roundingAdjustment > kMaxMoney - exactPayable) {
            throw runtime_error("Payable amount exceeds supported range.");
        }
    } else if (paiseRemainder > 0) {
        roundingAdjustment = -paiseRemainder;
    }

    result.totals.roundingAdjustment = roundingAdjustment;
    result.totals.payable = exactPayable + roundingAdjustment;
    return result;
}

bool sameTotals(const Totals& a, const Totals& b)
{
    return a.subtotal == b.subtotal &&
           a.productDiscount == b.productDiscount &&
           a.seasonalDiscount == b.seasonalDiscount &&
           a.specialDiscount == b.specialDiscount &&
           a.tax == b.tax &&
           a.roundingAdjustment == b.roundingAdjustment &&
           a.payable == b.payable;
}

int64_t computeTotalAmountReceived(const SaleRecord& sale)
{
    int64_t total = sale.payment.amountReceived;
    if (total < 0) {
        throw runtime_error("Amount received cannot be negative.");
    }
    for (const BillAdjustment& adj : sale.adjustments) {
        if (adj.amountDelta > 0) {
            if (adj.amountDelta > kMaxMoney - total) {
                throw runtime_error("Total collected amount exceeds supported range.");
            }
            total += adj.amountDelta;
        }
    }
    return total;
}

void verifySalePricingAndRevisions(const SaleRecord& sale)
{
    const string& orderId = sale.ids.orderId;
    if (sale.lines.empty()) {
        throw runtime_error("Order " + orderId + " has no lines.");
    }

    unordered_set<string> lineProducts;
    unordered_map<string, size_t> lineIndex;
    for (size_t i = 0; i < sale.lines.size(); ++i) {
        const SaleLine& line = sale.lines[i];
        if (!isNumeric(line.productId) || !validBarcode(line.barcode) ||
            trim(line.acronym).empty() || hasLineBreak(line.acronym) ||
            trim(line.name).empty() || hasLineBreak(line.name) ||
            (line.unit != Unit::Count && line.unit != Unit::Weight) ||
            line.quantity < 0 ||
            (sale.status == OrderStatus::OnHold && line.quantity <= 0) ||
            (line.unit == Unit::Count && line.quantity % 1000 != 0) ||
            line.unitPrice < 0 || line.taxRate < 0 || line.taxRate > kMaxBasisPoints ||
            line.discountPerUnit < 0 || line.discountPerUnit > line.unitPrice ||
            !lineProducts.insert(line.productId).second) {
            throw runtime_error("Invalid sale line in order " + orderId + ".");
        }
        lineIndex[line.productId] = i;
    }

    const PricingResult priced = computePricing(sale.lines, sale.terms);
    if (!sameTotals(priced.totals, sale.totals)) {
        throw runtime_error("Saved bill totals do not match calculated pricing for order " + orderId + ".");
    }
    for (size_t i = 0; i < sale.lines.size(); ++i) {
        if (sale.lines[i].lineProductDiscount != priced.lines[i].lineProductDiscount ||
            sale.lines[i].lineTax != priced.lines[i].lineTax) {
            throw runtime_error("Saved line tax or product discount mismatch for order " + orderId + ".");
        }
    }

    if (sale.status == OrderStatus::OnHold) {
        if (sale.paidAt.has_value() || sale.payment.recorded ||
            sale.payment.amountReceived != 0 || sale.payment.changeReturned != 0 ||
            !sale.payment.merchant.empty() ||
            sale.billChange != BillChange::Unchanged ||
            sale.revision != 0 || !sale.adjustments.empty() || !sale.pdfPath.empty()) {
            throw runtime_error("Invalid payment or revision state on held order " + orderId + ".");
        }
        return;
    }

    if (sale.status != OrderStatus::Paid) {
        throw runtime_error("Unsupported order status for order " + orderId + ".");
    }
    if (!sale.paidAt.has_value() || !validTimestamp(*sale.paidAt) ||
        sale.paidAt->epochSeconds < sale.createdAt.epochSeconds) {
        throw runtime_error("Invalid payment timestamp for order " + orderId + ".");
    }
    if (!sale.payment.recorded ||
        (sale.payment.mode != PaymentMode::Cash &&
         sale.payment.mode != PaymentMode::Card &&
         sale.payment.mode != PaymentMode::Wallet) ||
        hasLineBreak(sale.payment.merchant) ||
        sale.payment.amountReceived < 0 || sale.payment.changeReturned < 0) {
        throw runtime_error("Invalid payment details for order " + orderId + ".");
    }
    if (sale.revision != sale.adjustments.size()) {
        throw runtime_error("Revision count does not match adjustment rows for order " + orderId + ".");
    }

    if (sale.archived && !sale.pdfPath.empty()) {
        throw runtime_error("Archived bill " + sale.ids.billId + " cannot hold a PDF path.");
    }
    if (!sale.archived && !sale.pdfPath.empty() &&
        !validRelativeSubpath(sale.pdfPath, "bills")) {
        throw runtime_error("Invalid PDF path for bill " + sale.ids.billId + ".");
    }

    // Reconstruct every revision backwards to verify adjustment deltas and initial tender.
    vector<SaleLine> runningLines = sale.lines;
    Totals runningTotals = sale.totals;

    int64_t nextEpoch = numeric_limits<int64_t>::max();
    for (size_t idx = sale.adjustments.size(); idx > 0; --idx) {
        const BillAdjustment& adj = sale.adjustments[idx - 1];
        if (adj.revision != idx || !validTimestamp(adj.at) ||
            adj.at.epochSeconds < sale.paidAt->epochSeconds ||
            adj.at.epochSeconds > nextEpoch || adj.lines.empty()) {
            throw runtime_error("Invalid adjustment metadata on bill " + sale.ids.billId + ".");
        }
        nextEpoch = adj.at.epochSeconds;

        unordered_set<string> adjustedProducts;
        for (const LineAdjustment& la : adj.lines) {
            const auto posIt = lineIndex.find(la.productId);
            if (posIt == lineIndex.end() || !adjustedProducts.insert(la.productId).second) {
                throw runtime_error("Invalid adjusted product on bill " + sale.ids.billId + ".");
            }
            SaleLine& targetLine = runningLines[posIt->second];
            if (la.previousQuantity < 0 || la.quantity < 0 || la.restockQuantity < 0 ||
                la.previousQuantity == la.quantity ||
                (targetLine.unit == Unit::Count &&
                 (la.previousQuantity % 1000 != 0 || la.quantity % 1000 != 0 ||
                  la.restockQuantity % 1000 != 0)) ||
                la.restockQuantity > max<int64_t>(0, la.previousQuantity - la.quantity) ||
                targetLine.quantity != la.quantity) {
                throw runtime_error("Invalid line adjustment on bill " + sale.ids.billId + ".");
            }
            targetLine.quantity = la.previousQuantity;
        }

        const PricingResult prevPrice = computePricing(runningLines, sale.terms);
        if (runningTotals.payable - prevPrice.totals.payable != adj.amountDelta ||
            runningTotals.productDiscount - prevPrice.totals.productDiscount != adj.productDiscountDelta ||
            runningTotals.seasonalDiscount - prevPrice.totals.seasonalDiscount != adj.seasonalDiscountDelta ||
            runningTotals.specialDiscount - prevPrice.totals.specialDiscount != adj.specialDiscountDelta) {
            throw runtime_error("Adjustment deltas do not match repriced totals on bill " + sale.ids.billId + ".");
        }
        runningTotals = prevPrice.totals;
    }

    const int64_t initialPayable = runningTotals.payable;
    if (sale.payment.mode == PaymentMode::Cash) {
        if (sale.payment.amountReceived < initialPayable ||
            sale.payment.changeReturned != sale.payment.amountReceived - initialPayable) {
            throw runtime_error("Initial cash tender and change mismatch on bill " + sale.ids.billId + ".");
        }
    } else {
        if (sale.payment.amountReceived != initialPayable || sale.payment.changeReturned != 0) {
            throw runtime_error("Initial card/wallet tender mismatch on bill " + sale.ids.billId + ".");
        }
    }

    (void)computeTotalAmountReceived(sale);

    const bool allZero = all_of(sale.lines.begin(), sale.lines.end(),
                                [](const SaleLine& l) { return l.quantity == 0; });
    if (sale.revision == 0) {
        if (sale.billChange != BillChange::Unchanged) {
            throw runtime_error("Unrevised bill " + sale.ids.billId + " must have unchanged billChange.");
        }
    } else if (allZero) {
        if (sale.billChange != BillChange::CompleteRefund || sale.totals.payable != 0) {
            throw runtime_error("Zeroed bill " + sale.ids.billId + " must have complete-refund state and zero payable.");
        }
    } else {
        if (sale.billChange != BillChange::Modified) {
            throw runtime_error("Edited bill " + sale.ids.billId + " must have modified billChange.");
        }
    }
}

void validateSnapshot(const StoreData& data)
{
    unordered_set<string> categoryIds;
    unordered_set<string> categoryNames;
    unordered_set<string> categoryAcronyms;
    for (const Category& cat : data.categories) {
        const string cleanName = trim(cat.name);
        const string cleanAcronym = trim(cat.acronym);
        if (!validCategoryId(cat.id) || cleanName.empty() || cleanAcronym.empty() ||
            hasLineBreak(cat.name) || hasLineBreak(cat.acronym)) {
            throw runtime_error("Invalid category fields for category " + cat.id + ".");
        }
        if (!categoryIds.insert(cat.id).second ||
            !categoryNames.insert(toLower(cleanName)).second ||
            !categoryAcronyms.insert(toLower(cleanAcronym)).second) {
            throw runtime_error("Duplicate category ID, name, or acronym.");
        }
    }

    unordered_map<string, const Product*> products;
    unordered_set<string> barcodes;
    for (const Product& product : data.products) {
        if (!isNumeric(product.id) || !validBarcode(product.barcode) ||
            !validCategoryId(product.categoryId) ||
            categoryIds.find(product.categoryId) == categoryIds.end() ||
            product.barcode.compare(0, 2, product.categoryId) != 0 ||
            trim(product.acronym).empty() || hasLineBreak(product.acronym) ||
            trim(product.name).empty() || hasLineBreak(product.name) ||
            (product.unit != Unit::Count && product.unit != Unit::Weight) ||
            product.unitPrice < 0 || product.taxRate < 0 || product.taxRate > kMaxBasisPoints ||
            product.discountPerUnit < 0 || product.discountPerUnit > product.unitPrice ||
            product.onHand < 0 ||
            (product.unit == Unit::Count && product.onHand % 1000 != 0) ||
            !validRelativeSubpath(product.barcodeImagePath, "barcodes")) {
            throw runtime_error("Invalid fields for product " + product.id + ".");
        }
        if (!products.emplace(product.id, &product).second) {
            throw runtime_error("Duplicate product ID: " + product.id + ".");
        }
        if (!barcodes.insert(product.barcode).second) {
            throw runtime_error("Duplicate product barcode: " + product.barcode + ".");
        }
    }

    unordered_set<string> seasonKeys;
    int activeSeasons = 0;
    for (const Season& season : data.seasons) {
        if (!validDate(season.startDate) || !validDate(season.endDate) ||
            season.startDate > season.endDate ||
            season.rate < 0 || season.rate > kMaxBasisPoints) {
            throw runtime_error("Invalid seasonal promotion dates or rate.");
        }
        const string key = season.startDate + "|" + season.endDate;
        if (!seasonKeys.insert(key).second) {
            throw runtime_error("Duplicate seasonal promotion date range.");
        }
        if (season.active) {
            ++activeSeasons;
            if (activeSeasons > 1) {
                throw runtime_error("At most one seasonal promotion may be active.");
            }
        }
    }

    if (data.specialDiscount.rate < 0 || data.specialDiscount.rate > kMaxSpecialBasisPoints) {
        throw runtime_error("Special discount rate exceeds supported bounds.");
    }

    unordered_set<string> issuedSet;
    for (const string& code : data.issuedBarcodes) {
        if (!validBarcode(code)) {
            throw runtime_error("Invalid barcode in issued barcode set.");
        }
        if (!issuedSet.insert(code).second) {
            throw runtime_error("Duplicate barcode in issued barcode set: " + code + ".");
        }
    }

    unordered_set<string> orderIds;
    unordered_set<string> billIds;
    unordered_map<string, int64_t> reservedByProduct;

    for (const SaleRecord& sale : data.sales) {
        if (!validTimestamp(sale.createdAt) ||
            sale.ids.orderId.size() != 8 || !isNumeric(sale.ids.orderId) ||
            sale.ids.orderId.substr(6) != sale.createdAt.date.substr(8) ||
            sale.ids.billId.size() != 9 || !isNumeric(sale.ids.billId)) {
            throw runtime_error("Invalid date, time, or IDs for order " + sale.ids.orderId + ".");
        }
        if (!orderIds.insert(sale.ids.orderId).second || !billIds.insert(sale.ids.billId).second) {
            throw runtime_error("Duplicate order or bill ID.");
        }
        if (trim(sale.customer.name).empty() || hasLineBreak(sale.customer.name) ||
            hasLineBreak(sale.customer.contact)) {
            throw runtime_error("Invalid customer fields for order " + sale.ids.orderId + ".");
        }

        verifySalePricingAndRevisions(sale);

        if (!sale.archived && sale.status == OrderStatus::OnHold) {
            for (const SaleLine& line : sale.lines) {
                const auto prodIt = products.find(line.productId);
                if (prodIt == products.end() || prodIt->second->unit != line.unit) {
                    throw runtime_error("Active held order " + sale.ids.orderId +
                                        " does not match live product ID or unit.");
                }
                int64_t& reserved = reservedByProduct[line.productId];
                if (line.quantity > kMaxMoney - reserved) {
                    throw runtime_error("Reserved quantity overflows for product " + line.productId + ".");
                }
                reserved += line.quantity;
                if (reserved > prodIt->second->onHand) {
                    throw runtime_error("Held reservations exceed physical stock for product " +
                                        line.productId + ".");
                }
            }
        }
    }
}

// =========================================================================
// Increment 3 serialization
// =========================================================================

string serializeCategory(const Category& cat)
{
    ostringstream out;
    writeQuoted(out, cat.id);
    writeQuoted(out, cat.name);
    writeQuoted(out, cat.acronym);
    out << '#';
    return out.str();
}

string serializeProduct(const Product& product)
{
    ostringstream out;
    writeQuoted(out, product.id);
    writeQuoted(out, product.barcode);
    writeQuoted(out, product.categoryId);
    writeQuoted(out, product.acronym);
    writeQuoted(out, product.name);
    out << unitString(product.unit) << ' '
        << moneyString(product.unitPrice) << ' '
        << basisPointsString(product.taxRate) << ' '
        << moneyString(product.discountPerUnit) << ' '
        << quantityString(product.unit, product.onHand) << ' ';
    writeQuoted(out, product.barcodeImagePath);
    out << '#';
    return out.str();
}

string serializeOrder(const SaleRecord& sale)
{
    ostringstream out;
    writeQuoted(out, sale.ids.orderId);
    writeQuoted(out, sale.createdAt.date);
    writeQuoted(out, sale.createdAt.time);
    out << sale.createdAt.epochSeconds << ' ';
    writeQuoted(out, sale.customer.name);
    writeQuoted(out, sale.customer.contact);
    out << orderStatusString(sale.status) << ' '
        << (sale.archived ? "true" : "false") << ' ';
    if (sale.payment.recorded) {
        out << paymentModeString(sale.payment.mode) << ' ';
    } else {
        out << "- ";
    }
    writeQuoted(out, sale.payment.merchant);
    out << moneyString(sale.payment.amountReceived) << ' '
        << moneyString(sale.payment.changeReturned) << " #";
    return out.str();
}

string serializeOrderDetail(const string& orderId, const SaleLine& line)
{
    ostringstream out;
    writeQuoted(out, orderId);
    writeQuoted(out, line.productId);
    writeQuoted(out, line.barcode);
    out << quantityString(line.unit, line.quantity);
    return out.str();
}

string serializeBill(const SaleRecord& sale)
{
    const Timestamp& billTime =
        (sale.status == OrderStatus::Paid && sale.paidAt.has_value())
            ? *sale.paidAt
            : sale.createdAt;
    const int64_t totalReceived = computeTotalAmountReceived(sale);

    ostringstream out;
    writeQuoted(out, sale.ids.billId);
    writeQuoted(out, sale.customer.name);
    writeQuoted(out, sale.ids.orderId);
    out << moneyString(sale.totals.subtotal) << ' '
        << moneyString(sale.totals.tax) << ' '
        << moneyString(sale.totals.productDiscount) << ' '
        << moneyString(sale.totals.payable) << ' '
        << moneyString(totalReceived) << ' '
        << moneyString(sale.payment.changeReturned) << ' '
        << billStatusString(sale) << ' '
        << moneyString(sale.totals.seasonalDiscount) << ' '
        << moneyString(sale.totals.specialDiscount) << ' ';
    writeQuoted(out, billTime.date);
    out << quoted(billTime.time);
    return out.str();
}

string serializeBillDetail(const string& billId, const SaleLine& line)
{
    ostringstream out;
    writeQuoted(out, billId);
    writeQuoted(out, line.acronym);
    out << moneyString(line.unitPrice) << ' '
        << quantityString(line.unit, line.quantity) << ' '
        << moneyString(line.lineProductDiscount);
    return out.str();
}

string serializeSeason(const Season& season)
{
    ostringstream out;
    writeQuoted(out, season.startDate);
    writeQuoted(out, season.endDate);
    out << basisPointsString(season.rate) << ' '
        << (season.active ? "true" : "false") << " #";
    return out.str();
}

array<vector<string>, 8> serializeStore(const StoreData& data)
{
    array<vector<string>, 8> rows;

    for (const Category& cat : data.categories) {
        rows[0].push_back(serializeCategory(cat));
    }
    for (const Product& prod : data.products) {
        rows[1].push_back(serializeProduct(prod));
    }
    for (const SaleRecord& sale : data.sales) {
        rows[2].push_back(serializeOrder(sale));
        for (const SaleLine& line : sale.lines) {
            rows[3].push_back(serializeOrderDetail(sale.ids.orderId, line));
            rows[5].push_back(serializeBillDetail(sale.ids.billId, line));
        }
        rows[4].push_back(serializeBill(sale));
    }
    for (const Season& season : data.seasons) {
        rows[6].push_back(serializeSeason(season));
    }

    // PosMeta.txt
    vector<string>& meta = rows[7];
    meta.push_back("format 3 #");
    {
        ostringstream out;
        out << "special " << basisPointsString(data.specialDiscount.rate) << ' '
            << (data.specialDiscount.active ? "true" : "false") << " #";
        meta.push_back(out.str());
    }
    for (const string& code : data.issuedBarcodes) {
        ostringstream out;
        out << "issued ";
        writeQuoted(out, code);
        out << '#';
        meta.push_back(out.str());
    }
    for (const SaleRecord& sale : data.sales) {
        {
            ostringstream out;
            out << "bill ";
            writeQuoted(out, sale.ids.billId);
            if (sale.paidAt.has_value()) {
                out << sale.paidAt->epochSeconds << ' ';
            } else {
                out << "- ";
            }
            out << basisPointsString(sale.terms.seasonalRate) << ' '
                << basisPointsString(sale.terms.specialRate) << ' '
                << signedMoneyString(sale.totals.roundingAdjustment) << ' '
                << billChangeString(sale.billChange) << ' '
                << sale.revision << ' ';
            if (sale.pdfPath.empty()) {
                out << "- ";
            } else {
                writeQuoted(out, sale.pdfPath);
            }
            out << '#';
            meta.push_back(out.str());
        }

        unordered_map<string, Unit> unitByProduct;
        for (size_t i = 0; i < sale.lines.size(); ++i) {
            const SaleLine& line = sale.lines[i];
            unitByProduct[line.productId] = line.unit;
            ostringstream out;
            out << "line ";
            writeQuoted(out, sale.ids.billId);
            out << (i + 1) << ' ';
            writeQuoted(out, line.productId);
            writeQuoted(out, line.name);
            out << unitString(line.unit) << ' '
                << basisPointsString(line.taxRate) << ' '
                << moneyString(line.discountPerUnit) << ' '
                << moneyString(line.lineTax) << " #";
            meta.push_back(out.str());
        }

        for (const BillAdjustment& adj : sale.adjustments) {
            {
                ostringstream out;
                out << "adjust ";
                writeQuoted(out, sale.ids.billId);
                out << adj.revision << ' ';
                writeQuoted(out, adj.at.date);
                writeQuoted(out, adj.at.time);
                out << adj.at.epochSeconds << ' '
                    << signedMoneyString(adj.amountDelta) << ' '
                    << signedMoneyString(adj.productDiscountDelta) << ' '
                    << signedMoneyString(adj.seasonalDiscountDelta) << ' '
                    << signedMoneyString(adj.specialDiscountDelta) << " #";
                meta.push_back(out.str());
            }
            for (const LineAdjustment& la : adj.lines) {
                const Unit lineUnit = unitByProduct.at(la.productId);
                ostringstream out;
                out << "adjust-line ";
                writeQuoted(out, sale.ids.billId);
                out << adj.revision << ' ';
                writeQuoted(out, la.productId);
                out << quantityString(lineUnit, la.previousQuantity) << ' '
                    << quantityString(lineUnit, la.quantity) << ' '
                    << quantityString(lineUnit, la.restockQuantity) << " #";
                meta.push_back(out.str());
            }
        }
    }

    return rows;
}

// =========================================================================
// Increment 3 deserialization & join
// =========================================================================

struct RawOrderDetail {
    string productId;
    string barcode;
    int64_t quantity = 0;
};

struct RawBill {
    string billId;
    string customerName;
    string orderId;
    Totals totals;
    int64_t amountReceived = 0;
    int64_t balanceReturned = 0;
    string billStatus;
    string billDate;
    string billTime;
};

struct RawBillDetail {
    string acronym;
    int64_t unitPrice = 0;
    int64_t quantity = 0;
    int64_t lineProductDiscount = 0;
};

struct RawMetaBill {
    optional<int64_t> originalPaymentEpoch;
    PricingTerms terms;
    int64_t roundingAdjustment = 0;
    BillChange changeState = BillChange::Unchanged;
    uint32_t revision = 0;
    string pdfPath;
};

struct RawMetaLine {
    uint32_t ordinal = 0;
    string productId;
    string name;
    Unit unit = Unit::Count;
    int32_t taxRate = 0;
    int64_t discountPerUnit = 0;
    int64_t lineTax = 0;
};

struct RawMetaAdjust {
    uint32_t revision = 0;
    Timestamp at;
    int64_t amountDelta = 0;
    int64_t productDiscountDelta = 0;
    int64_t seasonalDiscountDelta = 0;
    int64_t specialDiscountDelta = 0;
};

struct RawMetaAdjustLine {
    string productId;
    int64_t previousQuantity = 0;
    int64_t quantity = 0;
    int64_t restockQuantity = 0;
};

Category deserializeCategory(const vector<Field>& fields)
{
    if (fields.size() != 3) {
        throw runtime_error("Incorrect number of fields.");
    }
    Category cat;
    cat.id = removeQuotes(fields[0], "Category ID");
    cat.name = removeQuotes(fields[1], "Category name");
    cat.acronym = removeQuotes(fields[2], "Category acronym");
    if (!validCategoryId(cat.id) || trim(cat.name).empty() || trim(cat.acronym).empty()) {
        throw runtime_error("Invalid category fields.");
    }
    return cat;
}

Product deserializeProductInc3(const vector<Field>& fields)
{
    if (fields.size() != 11) {
        throw runtime_error("Incorrect number of fields.");
    }
    Product p;
    p.id = removeQuotes(fields[0], "Product ID");
    p.barcode = removeQuotes(fields[1], "Barcode");
    p.categoryId = removeQuotes(fields[2], "Category ID");
    p.acronym = removeQuotes(fields[3], "Acronym");
    p.name = removeQuotes(fields[4], "Product name");
    p.unit = parseUnit(fields[5]);
    p.unitPrice = parseMoney(fields[6], "Unit price");
    p.taxRate = parseBasisPoints(fields[7], "Tax rate", kMaxBasisPoints);
    p.discountPerUnit = parseMoney(fields[8], "Discount per unit");
    p.onHand = parseQuantity(fields[9], true);
    p.barcodeImagePath = removeQuotes(fields[10], "Barcode image path");

    if (!isNumeric(p.id) || !validBarcode(p.barcode) || !validCategoryId(p.categoryId) ||
        trim(p.acronym).empty() || trim(p.name).empty() ||
        p.discountPerUnit > p.unitPrice ||
        (p.unit == Unit::Count && p.onHand % 1000 != 0) ||
        !validRelativeSubpath(p.barcodeImagePath, "barcodes")) {
        throw runtime_error("Invalid product field values.");
    }
    return p;
}

SaleRecord deserializeOrderInc3(const vector<Field>& fields)
{
    if (fields.size() != 12) {
        throw runtime_error("Incorrect number of fields.");
    }
    SaleRecord sale;
    sale.ids.orderId = removeQuotes(fields[0], "Order ID");
    sale.createdAt.date = removeQuotes(fields[1], "Creation date");
    sale.createdAt.time = removeQuotes(fields[2], "Creation time");
    if (!parseInt64(removeUnquoted(fields[3], "Creation epoch seconds"), sale.createdAt.epochSeconds) ||
        !validTimestamp(sale.createdAt)) {
        throw runtime_error("Invalid creation timestamp.");
    }
    sale.customer.name = removeQuotes(fields[4], "Customer name");
    sale.customer.contact = removeQuotes(fields[5], "Customer contact");

    const string statusStr = removeUnquoted(fields[6], "Order status");
    if (statusStr == "on-hold") {
        sale.status = OrderStatus::OnHold;
    } else if (statusStr == "paid") {
        sale.status = OrderStatus::Paid;
    } else {
        throw runtime_error("Invalid order status (expected on-hold or paid).");
    }

    sale.archived = parseBool(fields[7], "Archived");

    const string modeStr = removeUnquoted(fields[8], "Payment mode");
    if (modeStr == "cash") {
        sale.payment.mode = PaymentMode::Cash;
        sale.payment.recorded = true;
    } else if (modeStr == "card") {
        sale.payment.mode = PaymentMode::Card;
        sale.payment.recorded = true;
    } else if (modeStr == "wallet") {
        sale.payment.mode = PaymentMode::Wallet;
        sale.payment.recorded = true;
    } else if (modeStr == "-") {
        sale.payment.recorded = false;
        sale.payment.mode = PaymentMode::Cash;
    } else {
        throw runtime_error("Invalid payment mode.");
    }

    sale.payment.merchant = removeQuotes(fields[9], "Merchant");
    sale.payment.amountReceived = parseMoney(fields[10], "First amount received");
    sale.payment.changeReturned = parseMoney(fields[11], "First change returned");

    if (sale.ids.orderId.size() != 8 || !isNumeric(sale.ids.orderId) ||
        trim(sale.customer.name).empty()) {
        throw runtime_error("Invalid order ID or customer name.");
    }
    return sale;
}

pair<string, RawOrderDetail> deserializeOrderDetailInc3(const vector<Field>& fields)
{
    if (fields.size() != 4) {
        throw runtime_error("Incorrect number of fields.");
    }
    const string orderId = removeQuotes(fields[0], "Order ID");
    RawOrderDetail d;
    d.productId = removeQuotes(fields[1], "Product ID");
    d.barcode = removeQuotes(fields[2], "Barcode");
    d.quantity = parseQuantity(fields[3], true);

    if (orderId.size() != 8 || !isNumeric(orderId) ||
        !isNumeric(d.productId) || !validBarcode(d.barcode)) {
        throw runtime_error("Invalid order detail identifiers.");
    }
    return {orderId, std::move(d)};
}

RawBill deserializeBillInc3(const vector<Field>& fields)
{
    if (fields.size() != 14) {
        throw runtime_error("Incorrect number of fields.");
    }
    RawBill b;
    b.billId = removeQuotes(fields[0], "Bill ID");
    b.customerName = removeQuotes(fields[1], "Customer name");
    b.orderId = removeQuotes(fields[2], "Order ID");
    b.totals.subtotal = parseMoney(fields[3], "Total without tax");
    b.totals.tax = parseMoney(fields[4], "Total tax");
    b.totals.productDiscount = parseMoney(fields[5], "Total product discount");
    b.totals.payable = parseMoney(fields[6], "Total payable");
    b.amountReceived = parseMoney(fields[7], "Amount received");
    b.balanceReturned = parseMoney(fields[8], "Balance returned");
    b.billStatus = removeUnquoted(fields[9], "Bill status");
    b.totals.seasonalDiscount = parseMoney(fields[10], "Seasonal discount");
    b.totals.specialDiscount = parseMoney(fields[11], "Special discount");
    b.billDate = removeQuotes(fields[12], "Bill date");
    b.billTime = removeQuotes(fields[13], "Bill time");

    if (b.billId.size() != 9 || !isNumeric(b.billId) ||
        b.orderId.size() != 8 || !isNumeric(b.orderId) ||
        trim(b.customerName).empty() ||
        !validDate(b.billDate) || !validTime(b.billTime)) {
        throw runtime_error("Invalid bill summary fields.");
    }
    if (b.billStatus != "paid" && b.billStatus != "on-hold" &&
        b.billStatus != "archived" && b.billStatus != "refunded") {
        throw runtime_error("Invalid bill status.");
    }
    return b;
}

pair<string, RawBillDetail> deserializeBillDetailInc3(const vector<Field>& fields)
{
    if (fields.size() != 5) {
        throw runtime_error("Incorrect number of fields.");
    }
    const string billId = removeQuotes(fields[0], "Bill ID");
    RawBillDetail bd;
    bd.acronym = removeQuotes(fields[1], "Product acronym");
    bd.unitPrice = parseMoney(fields[2], "Unit price without tax");
    bd.quantity = parseQuantity(fields[3], true);
    bd.lineProductDiscount = parseMoney(fields[4], "Total line product discount");

    if (billId.size() != 9 || !isNumeric(billId) || trim(bd.acronym).empty()) {
        throw runtime_error("Invalid bill detail fields.");
    }
    return {billId, std::move(bd)};
}

Season deserializeSeasonInc3(const vector<Field>& fields)
{
    if (fields.size() != 4) {
        throw runtime_error("Incorrect number of fields.");
    }
    Season s;
    s.startDate = removeQuotes(fields[0], "Start date");
    s.endDate = removeQuotes(fields[1], "End date");
    s.rate = parseBasisPoints(fields[2], "Season percentage", kMaxBasisPoints);
    s.active = parseBool(fields[3], "Season active flag");
    if (!validDate(s.startDate) || !validDate(s.endDate) || s.startDate > s.endDate) {
        throw runtime_error("Invalid season date range.");
    }
    return s;
}

template <typename Fn>
void readTextFileLines(const filesystem::path& filePath, const string& displayName, Fn&& handleLine)
{
    ifstream stream(filePath);
    if (!stream.is_open()) {
        throw runtime_error("Unable to open " + displayName + ".");
    }
    string line;
    int lineNumber = 0;
    while (getline(stream, line)) {
        ++lineNumber;
        line = trim(line);
        if (line.empty()) {
            continue;
        }
        try {
            handleLine(line);
        } catch (const exception& e) {
            throw runtime_error(displayName + ": Line " + to_string(lineNumber) + ": " + e.what());
        }
    }
    if (stream.bad() || !stream.eof()) {
        throw runtime_error("Error reading " + displayName + ".");
    }
}

StoreData parseAndValidateInc3Set(const filesystem::path& dir)
{
    StoreData data;

    // 1. PCategory.txt
    readTextFileLines(dir / "PCategory.txt", "PCategory.txt", [&](const string& line) {
        data.categories.push_back(deserializeCategory(removeRecordMarker(line)));
    });

    // 2. Product.txt
    readTextFileLines(dir / "Product.txt", "Product.txt", [&](const string& line) {
        data.products.push_back(deserializeProductInc3(removeRecordMarker(line)));
    });

    // 3. Order.txt
    unordered_map<string, size_t> orderIndexById;
    readTextFileLines(dir / "Order.txt", "Order.txt", [&](const string& line) {
        SaleRecord sale = deserializeOrderInc3(removeRecordMarker(line));
        if (!orderIndexById.emplace(sale.ids.orderId, data.sales.size()).second) {
            throw runtime_error("Duplicate order ID.");
        }
        data.sales.push_back(std::move(sale));
    });

    // 4. OrderDetail.txt
    unordered_map<string, vector<RawOrderDetail>> orderDetailsByOrder;
    readTextFileLines(dir / "OrderDetail.txt", "OrderDetail.txt", [&](const string& line) {
        auto [orderId, detail] = deserializeOrderDetailInc3(slices(line));
        if (orderIndexById.find(orderId) == orderIndexById.end()) {
            throw runtime_error("Order ID " + orderId + " does not exist in Order.txt.");
        }
        orderDetailsByOrder[orderId].push_back(std::move(detail));
    });

    // 5. Bill.txt
    unordered_map<string, RawBill> billsByOrderId;
    unordered_map<string, string> orderIdByBillId;
    readTextFileLines(dir / "Bill.txt", "Bill.txt", [&](const string& line) {
        RawBill bill = deserializeBillInc3(slices(line));
        if (!orderIdByBillId.emplace(bill.billId, bill.orderId).second) {
            throw runtime_error("Duplicate bill ID.");
        }
        if (orderIndexById.find(bill.orderId) == orderIndexById.end()) {
            throw runtime_error("Order ID " + bill.orderId + " does not exist in Order.txt.");
        }
        if (!billsByOrderId.emplace(bill.orderId, std::move(bill)).second) {
            throw runtime_error("Order already has a bill.");
        }
    });

    // 6. BillDetail.txt
    unordered_map<string, vector<RawBillDetail>> billDetailsByBillId;
    readTextFileLines(dir / "BillDetail.txt", "BillDetail.txt", [&](const string& line) {
        auto [billId, bd] = deserializeBillDetailInc3(slices(line));
        if (orderIdByBillId.find(billId) == orderIdByBillId.end()) {
            throw runtime_error("Bill ID " + billId + " does not exist in Bill.txt.");
        }
        billDetailsByBillId[billId].push_back(std::move(bd));
    });

    // 7. Seasons.txt
    readTextFileLines(dir / "Seasons.txt", "Seasons.txt", [&](const string& line) {
        data.seasons.push_back(deserializeSeasonInc3(removeRecordMarker(line)));
    });

    // 8. PosMeta.txt
    bool seenFormat = false;
    bool seenSpecial = false;
    unordered_map<string, RawMetaBill> metaBillsById;
    unordered_map<string, map<uint32_t, RawMetaLine>> metaLinesByBillId;
    unordered_map<string, map<uint32_t, RawMetaAdjust>> metaAdjustsByBillId;
    map<pair<string, uint32_t>, vector<RawMetaAdjustLine>> metaAdjustLines;

    readTextFileLines(dir / "PosMeta.txt", "PosMeta.txt", [&](const string& line) {
        const vector<Field> fields = removeRecordMarker(line);
        if (fields.empty()) {
            throw runtime_error("Missing metadata tag.");
        }
        const string tag = removeUnquoted(fields[0], "Metadata tag");

        if (tag == "format") {
            if (fields.size() != 2) {
                throw runtime_error("Incorrect field count for format.");
            }
            if (seenFormat) {
                throw runtime_error("Duplicate format row.");
            }
            uint32_t version = 0;
            if (!parseUint32(removeUnquoted(fields[1], "Schema version"), version) || version != 3) {
                throw runtime_error("Unsupported schema version (expected 3).");
            }
            seenFormat = true;
        } else if (tag == "special") {
            if (fields.size() != 3) {
                throw runtime_error("Incorrect field count for special.");
            }
            if (seenSpecial) {
                throw runtime_error("Duplicate special row.");
            }
            data.specialDiscount.rate =
                parseBasisPoints(fields[1], "Special discount percentage", kMaxSpecialBasisPoints);
            data.specialDiscount.active = parseBool(fields[2], "Special discount active flag");
            seenSpecial = true;
        } else if (tag == "issued") {
            if (fields.size() != 2) {
                throw runtime_error("Incorrect field count for issued.");
            }
            const string code = removeQuotes(fields[1], "Issued barcode");
            if (!validBarcode(code)) {
                throw runtime_error("Invalid issued barcode.");
            }
            data.issuedBarcodes.push_back(code);
        } else if (tag == "bill") {
            if (fields.size() != 9) {
                throw runtime_error("Incorrect field count for bill metadata.");
            }
            const string billId = removeQuotes(fields[1], "Bill ID");
            if (orderIdByBillId.find(billId) == orderIdByBillId.end()) {
                throw runtime_error("Bill ID " + billId + " does not exist in Bill.txt.");
            }
            RawMetaBill mb;
            const string epochTok = removeUnquoted(fields[2], "Original payment epoch");
            if (epochTok != "-") {
                int64_t epoch = 0;
                if (!parseInt64(epochTok, epoch) || epoch <= 0) {
                    throw runtime_error("Invalid original payment epoch seconds.");
                }
                mb.originalPaymentEpoch = epoch;
            }
            mb.terms.seasonalRate =
                parseBasisPoints(fields[3], "Applied seasonal percentage", kMaxBasisPoints);
            mb.terms.specialRate =
                parseBasisPoints(fields[4], "Applied special percentage", kMaxSpecialBasisPoints);
            mb.roundingAdjustment = parseSignedMoney(fields[5], "Rounding adjustment");

            const string changeStr = removeUnquoted(fields[6], "Change state");
            if (changeStr == "unchanged") {
                mb.changeState = BillChange::Unchanged;
            } else if (changeStr == "modified") {
                mb.changeState = BillChange::Modified;
            } else if (changeStr == "complete-refund") {
                mb.changeState = BillChange::CompleteRefund;
            } else {
                throw runtime_error("Invalid bill change state.");
            }

            if (!parseUint32(removeUnquoted(fields[7], "Revision"), mb.revision)) {
                throw runtime_error("Invalid revision number.");
            }

            if (fields[8].quoted) {
                mb.pdfPath = fields[8].value;
                if (!validRelativeSubpath(mb.pdfPath, "bills")) {
                    throw runtime_error("Invalid managed PDF path.");
                }
            } else if (fields[8].value != "-") {
                throw runtime_error("PDF path must be quoted or unquoted '-'.");
            }

            if (!metaBillsById.emplace(billId, std::move(mb)).second) {
                throw runtime_error("Duplicate metadata bill row for " + billId + ".");
            }
        } else if (tag == "line") {
            if (fields.size() != 9) {
                throw runtime_error("Incorrect field count for line metadata.");
            }
            const string billId = removeQuotes(fields[1], "Bill ID");
            if (orderIdByBillId.find(billId) == orderIdByBillId.end()) {
                throw runtime_error("Bill ID " + billId + " does not exist in Bill.txt.");
            }
            RawMetaLine ml;
            if (!parseUint32(removeUnquoted(fields[2], "Line ordinal"), ml.ordinal) || ml.ordinal == 0) {
                throw runtime_error("Invalid 1-based line ordinal.");
            }
            ml.productId = removeQuotes(fields[3], "Product ID");
            ml.name = removeQuotes(fields[4], "Historical product name");
            ml.unit = parseUnit(fields[5]);
            ml.taxRate = parseBasisPoints(fields[6], "Tax rate", kMaxBasisPoints);
            ml.discountPerUnit = parseMoney(fields[7], "Product discount per unit");
            ml.lineTax = parseMoney(fields[8], "Line tax");

            if (!isNumeric(ml.productId) || trim(ml.name).empty()) {
                throw runtime_error("Invalid line metadata fields.");
            }
            if (!metaLinesByBillId[billId].emplace(ml.ordinal, std::move(ml)).second) {
                throw runtime_error("Duplicate line ordinal for bill " + billId + ".");
            }
        } else if (tag == "adjust") {
            if (fields.size() != 10) {
                throw runtime_error("Incorrect field count for adjust metadata.");
            }
            const string billId = removeQuotes(fields[1], "Bill ID");
            if (orderIdByBillId.find(billId) == orderIdByBillId.end()) {
                throw runtime_error("Bill ID " + billId + " does not exist in Bill.txt.");
            }
            RawMetaAdjust ma;
            if (!parseUint32(removeUnquoted(fields[2], "Adjustment revision"), ma.revision) ||
                ma.revision == 0) {
                throw runtime_error("Invalid adjustment revision.");
            }
            ma.at.date = removeQuotes(fields[3], "Edit date");
            ma.at.time = removeQuotes(fields[4], "Edit time");
            if (!parseInt64(removeUnquoted(fields[5], "Edit epoch seconds"), ma.at.epochSeconds) ||
                !validTimestamp(ma.at)) {
                throw runtime_error("Invalid adjustment timestamp.");
            }
            ma.amountDelta = parseSignedMoney(fields[6], "Payable difference");
            ma.productDiscountDelta = parseSignedMoney(fields[7], "Product discount difference");
            ma.seasonalDiscountDelta = parseSignedMoney(fields[8], "Seasonal discount difference");
            ma.specialDiscountDelta = parseSignedMoney(fields[9], "Special discount difference");

            if (!metaAdjustsByBillId[billId].emplace(ma.revision, std::move(ma)).second) {
                throw runtime_error("Duplicate adjustment revision for bill " + billId + ".");
            }
        } else if (tag == "adjust-line") {
            if (fields.size() != 7) {
                throw runtime_error("Incorrect field count for adjust-line metadata.");
            }
            const string billId = removeQuotes(fields[1], "Bill ID");
            uint32_t revision = 0;
            if (!parseUint32(removeUnquoted(fields[2], "Adjustment revision"), revision) ||
                revision == 0) {
                throw runtime_error("Invalid adjust-line revision.");
            }
            RawMetaAdjustLine mal;
            mal.productId = removeQuotes(fields[3], "Product ID");
            mal.previousQuantity = parseQuantity(fields[4], true);
            mal.quantity = parseQuantity(fields[5], true);
            mal.restockQuantity = parseQuantity(fields[6], true);
            if (!isNumeric(mal.productId)) {
                throw runtime_error("Invalid product ID in adjust-line.");
            }
            metaAdjustLines[{billId, revision}].push_back(std::move(mal));
        } else {
            throw runtime_error("Unknown metadata tag: " + tag);
        }
    });

    if (!seenFormat) {
        throw runtime_error("PosMeta.txt: Missing format 3 header.");
    }
    if (!seenSpecial) {
        throw runtime_error("PosMeta.txt: Missing special discount entry.");
    }

    // Verify all adjust-line keys correspond to declared adjust rows.
    for (const auto& [key, _] : metaAdjustLines) {
        const auto adjBillIt = metaAdjustsByBillId.find(key.first);
        if (adjBillIt == metaAdjustsByBillId.end() ||
            adjBillIt->second.find(key.second) == adjBillIt->second.end()) {
            throw runtime_error("PosMeta.txt: adjust-line references missing adjustment for bill " +
                                key.first + " revision " + to_string(key.second) + ".");
        }
    }

    // Join Order, OrderDetail, Bill, BillDetail, and PosMeta into SaleRecord objects.
    for (SaleRecord& sale : data.sales) {
        const string& orderId = sale.ids.orderId;
        const auto billIt = billsByOrderId.find(orderId);
        if (billIt == billsByOrderId.end()) {
            throw runtime_error("Order " + orderId + " does not have a matching Bill.txt row.");
        }
        const RawBill& bill = billIt->second;
        sale.ids.billId = bill.billId;

        if (sale.customer.name != bill.customerName) {
            throw runtime_error("Customer name mismatch for order " + orderId + ".");
        }

        const auto metaBillIt = metaBillsById.find(bill.billId);
        if (metaBillIt == metaBillsById.end()) {
            throw runtime_error("PosMeta.txt: Missing bill metadata for bill " + bill.billId + ".");
        }
        const RawMetaBill& mb = metaBillIt->second;

        sale.terms = mb.terms;
        sale.totals = bill.totals;
        sale.totals.roundingAdjustment = mb.roundingAdjustment;
        sale.billChange = mb.changeState;
        sale.revision = mb.revision;
        sale.pdfPath = mb.pdfPath;

        if (billStatusString(sale) != bill.billStatus) {
            throw runtime_error("Status mismatch across Order.txt, Bill.txt, and PosMeta.txt for bill " +
                                bill.billId + ".");
        }

        if (sale.status == OrderStatus::Paid) {
            if (!mb.originalPaymentEpoch.has_value()) {
                throw runtime_error("Paid bill " + bill.billId + " is missing original payment epoch.");
            }
            sale.paidAt = Timestamp{bill.billDate, bill.billTime, *mb.originalPaymentEpoch};
        } else {
            if (mb.originalPaymentEpoch.has_value() ||
                bill.billDate != sale.createdAt.date ||
                bill.billTime != sale.createdAt.time) {
                throw runtime_error("Held bill " + bill.billId + " date/time does not match Order.txt.");
            }
        }

        const auto odIt = orderDetailsByOrder.find(orderId);
        const auto bdIt = billDetailsByBillId.find(bill.billId);
        const auto mlIt = metaLinesByBillId.find(bill.billId);
        if (odIt == orderDetailsByOrder.end() ||
            bdIt == billDetailsByBillId.end() ||
            mlIt == metaLinesByBillId.end()) {
            throw runtime_error("Missing detail or line metadata rows for order " + orderId + ".");
        }

        const vector<RawOrderDetail>& ods = odIt->second;
        const vector<RawBillDetail>& bds = bdIt->second;
        const map<uint32_t, RawMetaLine>& mls = mlIt->second;

        if (ods.size() != bds.size() || ods.size() != mls.size() || ods.empty()) {
            throw runtime_error("Line count mismatch across OrderDetail, BillDetail, and PosMeta for bill " +
                                bill.billId + ".");
        }

        sale.lines.reserve(ods.size());
        for (size_t i = 0; i < ods.size(); ++i) {
            const uint32_t ordinal = static_cast<uint32_t>(i + 1);
            const auto lineMetaIt = mls.find(ordinal);
            if (lineMetaIt == mls.end()) {
                throw runtime_error("Missing line ordinal " + to_string(ordinal) +
                                    " in PosMeta.txt for bill " + bill.billId + ".");
            }
            const RawOrderDetail& od = ods[i];
            const RawBillDetail& bd = bds[i];
            const RawMetaLine& ml = lineMetaIt->second;

            if (od.productId != ml.productId || od.quantity != bd.quantity) {
                throw runtime_error("Joined line mismatch at ordinal " + to_string(ordinal) +
                                    " for bill " + bill.billId + ".");
            }

            SaleLine line;
            line.productId = od.productId;
            line.barcode = od.barcode;
            line.acronym = bd.acronym;
            line.name = ml.name;
            line.unit = ml.unit;
            line.unitPrice = bd.unitPrice;
            line.taxRate = ml.taxRate;
            line.discountPerUnit = ml.discountPerUnit;
            line.quantity = od.quantity;
            line.lineProductDiscount = bd.lineProductDiscount;
            line.lineTax = ml.lineTax;
            sale.lines.push_back(std::move(line));
        }

        const auto adjIt = metaAdjustsByBillId.find(bill.billId);
        const size_t adjCount = (adjIt == metaAdjustsByBillId.end()) ? 0 : adjIt->second.size();
        if (adjCount != sale.revision) {
            throw runtime_error("Adjustment count does not match bill revision for bill " + bill.billId + ".");
        }
        for (uint32_t rev = 1; rev <= sale.revision; ++rev) {
            const auto maIt = adjIt->second.find(rev);
            if (maIt == adjIt->second.end()) {
                throw runtime_error("Missing adjustment revision " + to_string(rev) +
                                    " for bill " + bill.billId + ".");
            }
            const RawMetaAdjust& ma = maIt->second;
            const auto malIt = metaAdjustLines.find({bill.billId, rev});
            if (malIt == metaAdjustLines.end() || malIt->second.empty()) {
                throw runtime_error("Missing adjust-line rows for bill " + bill.billId +
                                    " revision " + to_string(rev) + ".");
            }

            BillAdjustment adj;
            adj.revision = ma.revision;
            adj.at = ma.at;
            adj.amountDelta = ma.amountDelta;
            adj.productDiscountDelta = ma.productDiscountDelta;
            adj.seasonalDiscountDelta = ma.seasonalDiscountDelta;
            adj.specialDiscountDelta = ma.specialDiscountDelta;
            for (const RawMetaAdjustLine& mal : malIt->second) {
                adj.lines.push_back({mal.productId, mal.previousQuantity, mal.quantity, mal.restockQuantity});
            }
            sale.adjustments.push_back(std::move(adj));
        }

        const int64_t expectedReceived = computeTotalAmountReceived(sale);
        if (bill.amountReceived != expectedReceived ||
            bill.balanceReturned != sale.payment.changeReturned) {
            throw runtime_error("Bill.txt tender/change mismatch against Order.txt and adjustments for bill " +
                                bill.billId + ".");
        }
    }

    validateSnapshot(data);
    return data;
}

// =========================================================================
// Staging & atomic directory installation helpers
// =========================================================================

void writeFileChecked(const filesystem::path& path, const vector<string>& rows)
{
    ofstream out(path, ios::binary | ios::trunc);
    if (!out.is_open()) {
        throw runtime_error("Unable to create file: " + path.u8string());
    }
    for (const string& row : rows) {
        out << row << '\n';
    }
    out.flush();
    out.close();
    if (out.fail()) {
        throw runtime_error("Error writing file: " + path.u8string());
    }
}

void writeMarkerChecked(const filesystem::path& path)
{
    ofstream out(path, ios::binary | ios::trunc);
    if (!out.is_open()) {
        throw runtime_error("Unable to create marker: " + path.u8string());
    }
    out << "1\n";
    out.flush();
    out.close();
    if (out.fail()) {
        throw runtime_error("Error writing marker: " + path.u8string());
    }
}

void copyFileChecked(const filesystem::path& src, const filesystem::path& dst)
{
    ifstream in(src, ios::binary);
    if (!in.is_open()) {
        throw runtime_error("Unable to read source file: " + src.u8string());
    }
    ofstream out(dst, ios::binary | ios::trunc);
    if (!out.is_open()) {
        throw runtime_error("Unable to write staging file: " + dst.u8string());
    }
    array<char, 8192> buffer{};
    while (in.read(buffer.data(), static_cast<streamsize>(buffer.size())) || in.gcount() > 0) {
        out.write(buffer.data(), in.gcount());
        if (!out) {
            break;
        }
    }
    if (in.bad() || (in.fail() && !in.eof())) {
        throw runtime_error("Error reading source file: " + src.u8string());
    }
    out.flush();
    out.close();
    if (out.fail()) {
        throw runtime_error("Error writing staging file: " + dst.u8string());
    }
}

void installSetFromDir(const filesystem::path& sourceDir,
                       const filesystem::path& stagingRoot,
                       const filesystem::path& destRoot,
                       const string& tempPrefix)
{
    for (const char* name : kInc3Files) {
        const filesystem::path stagedTmp = stagingRoot / (tempPrefix + name);
        copyFileChecked(sourceDir / name, stagedTmp);
        filesystem::rename(stagedTmp, destRoot / name);
    }
}

bool removeAllLiveFiles(const filesystem::path& destRoot)
{
    bool ok = true;
    for (const char* name : kInc3Files) {
        error_code ec;
        const filesystem::path p = destRoot / name;
        if (filesystem::exists(p, ec)) {
            if (ec) {
                ok = false;
                continue;
            }
            filesystem::remove(p, ec);
            if (ec) {
                ok = false;
            }
        } else if (ec) {
            ok = false;
        }
    }
    return ok;
}

bool anyLiveFileExists(const filesystem::path& dir)
{
    for (const char* name : kInc3Files) {
        error_code ec;
        if (filesystem::exists(dir / name, ec) || ec) {
            return true;
        }
    }
    return false;
}

bool allLiveFilesExist(const filesystem::path& dir)
{
    for (const char* name : kInc3Files) {
        error_code ec;
        const auto st = filesystem::symlink_status(dir / name, ec);
        if (ec || !filesystem::exists(st) || !filesystem::is_regular_file(st)) {
            return false;
        }
    }
    return true;
}

constexpr string_view kStagingDirName = ".writing";
constexpr string_view kRetiredPrefix = ".writing.retired";

bool isRetiredStagingName(string_view name)
{
    return name == kRetiredPrefix ||
           (name.size() > kRetiredPrefix.size() + 1 &&
            name.compare(0, kRetiredPrefix.size(), kRetiredPrefix) == 0 &&
            name[kRetiredPrefix.size()] == '.');
}

void cleanupRetiredStagingDirs(const filesystem::path& dataRoot) noexcept
{
    try {
        error_code ec;
        vector<filesystem::path> targets;
        const filesystem::directory_iterator end;
        for (filesystem::directory_iterator it(dataRoot, ec); !ec && it != end; it.increment(ec)) {
            const string name = it->path().filename().u8string();
            if (isRetiredStagingName(name)) {
                targets.push_back(it->path());
            }
        }
        for (const filesystem::path& target : targets) {
            error_code rmEc;
            filesystem::remove_all(target, rmEc);
        }
    } catch (...) {
    }
}

enum class StagingCleanupOutcome {
    Clean,
    RetiredButDeleteFailed,
    RetireFailed
};

// Retire .writing atomically before recursive removal so cleanup interruption
// or partial deletion never corrupts markers or the authoritative staging set.
StagingCleanupOutcome retireAndRemoveStagingDir(const filesystem::path& dataRoot,
                                                  const filesystem::path& stagingRoot,
                                                  error_code& outError) noexcept
{
    outError.clear();
    try {
        cleanupRetiredStagingDirs(dataRoot);

        error_code statusEc;
        const auto st = filesystem::symlink_status(stagingRoot, statusEc);
        if (statusEc == errc::no_such_file_or_directory || (!statusEc && !filesystem::exists(st))) {
            return StagingCleanupOutcome::Clean;
        }
        if (statusEc) {
            outError = statusEc;
            return StagingCleanupOutcome::RetireFailed;
        }

        filesystem::path retiredPath;
        for (unsigned int attempt = 0; attempt < 10000; ++attempt) {
            const string candidateName = (attempt == 0)
                ? string(kRetiredPrefix)
                : string(kRetiredPrefix) + "." + to_string(attempt);
            const filesystem::path candidate = dataRoot / candidateName;
            error_code candEc;
            const auto candSt = filesystem::symlink_status(candidate, candEc);
            if (candEc == errc::no_such_file_or_directory ||
                (!candEc && !filesystem::exists(candSt))) {
                retiredPath = candidate;
                break;
            }
            if (candEc) {
                outError = candEc;
                return StagingCleanupOutcome::RetireFailed;
            }
        }

        if (retiredPath.empty()) {
            outError = make_error_code(errc::file_exists);
            return StagingCleanupOutcome::RetireFailed;
        }

        error_code renameEc;
        filesystem::rename(stagingRoot, retiredPath, renameEc);
        if (renameEc) {
            outError = renameEc;
            return StagingCleanupOutcome::RetireFailed;
        }

        error_code removeEc;
        filesystem::remove_all(retiredPath, removeEc);
        if (removeEc) {
            outError = removeEc;
            return StagingCleanupOutcome::RetiredButDeleteFailed;
        }

        return StagingCleanupOutcome::Clean;
    } catch (...) {
        if (!outError) {
            outError = make_error_code(errc::io_error);
        }
        error_code checkEc;
        const auto checkSt = filesystem::symlink_status(stagingRoot, checkEc);
        if (checkEc == errc::no_such_file_or_directory ||
            (!checkEc && !filesystem::exists(checkSt))) {
            return StagingCleanupOutcome::RetiredButDeleteFailed;
        }
        return StagingCleanupOutcome::RetireFailed;
    }
}

// =========================================================================
// Legacy Increment 2 parsing, validation & PNG verification (for import)
// =========================================================================

struct LegacyProduct {
    int sourceLine = 0;
    string id;
    string barcode;
    string acronym;
    string name;
    Unit unit = Unit::Count;
    int64_t unitPrice = 0;
    int64_t taxPerUnit = 0;
    int64_t discountPerUnit = 0;
    int64_t available = 0;
    string barcodeImagePath;
};

struct LegacyOrder {
    string orderId;
    string date;
    string time;
    string customerName;
    string customerContact;
    string status; // on-hold, paid, archived
    int64_t paymentAmount = 0;
    bool paymentRecorded = false;
    PaymentMode paymentMode = PaymentMode::Cash;
    string merchant;
};

struct LegacyDetail {
    string productId;
    string barcode;
    int64_t quantity = 0;
};

struct LegacyBillItem {
    optional<int64_t> taxPerUnit;
    string acronym;
    int64_t unitPrice = 0;
    int64_t discountPerUnit = 0;
    int64_t quantity = 0;
};

struct LegacyBill {
    string billId;
    string customerName;
    string orderId;
    int64_t subtotal = 0;
    int64_t tax = 0;
    int64_t discount = 0;
    int64_t payable = 0;
    string status;
    vector<LegacyBillItem> items;
};

LegacyProduct deserializeLegacyProduct(const vector<Field>& fields)
{
    if (fields.size() != 10) {
        throw runtime_error("Incorrect number of fields.");
    }
    LegacyProduct p;
    p.id = removeQuotes(fields[0], "Product ID");
    p.barcode = removeQuotes(fields[1], "Barcode");
    p.acronym = removeQuotes(fields[2], "Acronym");
    p.name = removeQuotes(fields[3], "Product name");
    p.unit = parseUnit(fields[4]);
    p.unitPrice = parseMoney(fields[5], "Unit price");
    p.taxPerUnit = parseMoney(fields[6], "Tax");
    p.discountPerUnit = parseMoney(fields[7], "Discount");
    p.available = parseQuantity(fields[8], true);
    p.barcodeImagePath = removeQuotes(fields[9], "Barcode image path");

    if (!isNumeric(p.id) || !validBarcode(p.barcode) ||
        trim(p.acronym).empty() || p.acronym.size() > 8 ||
        trim(p.name).empty() ||
        p.discountPerUnit > p.unitPrice ||
        (p.unit == Unit::Count && p.available % 1000 != 0) ||
        !validRelativeSubpath(p.barcodeImagePath, nullopt)) {
        throw runtime_error("Invalid legacy product fields.");
    }
    return p;
}

LegacyOrder deserializeLegacyOrder(const vector<Field>& fields)
{
    if (fields.size() != 9) {
        throw runtime_error("Incorrect number of fields.");
    }
    LegacyOrder o;
    o.orderId = removeQuotes(fields[0], "Order ID");
    o.date = removeQuotes(fields[1], "Date");
    o.time = removeQuotes(fields[2], "Time");
    o.customerName = removeQuotes(fields[3], "Customer name");
    o.customerContact = removeQuotes(fields[4], "Customer contact");
    o.status = removeUnquoted(fields[5], "Order status");
    o.paymentAmount = parseMoney(fields[6], "Payment amount");
    const string modeStr = removeUnquoted(fields[7], "Payment mode");
    o.merchant = removeQuotes(fields[8], "Merchant");

    if (o.orderId.size() != 8 || !isNumeric(o.orderId) ||
        !validDate(o.date) || !validTime(o.time) ||
        o.orderId.substr(6) != o.date.substr(8) ||
        trim(o.customerName).empty()) {
        throw runtime_error("Invalid legacy order metadata.");
    }
    if (o.status != "on-hold" && o.status != "paid" && o.status != "archived") {
        throw runtime_error("Invalid legacy order status.");
    }
    if (modeStr == "cash") {
        o.paymentMode = PaymentMode::Cash;
        o.paymentRecorded = true;
    } else if (modeStr == "card") {
        o.paymentMode = PaymentMode::Card;
        o.paymentRecorded = true;
    } else if (modeStr == "wallet") {
        o.paymentMode = PaymentMode::Wallet;
        o.paymentRecorded = true;
    } else if (modeStr == "-") {
        o.paymentRecorded = false;
    } else {
        throw runtime_error("Invalid legacy payment mode.");
    }
    if (!o.paymentRecorded && (o.paymentAmount != 0 || !o.merchant.empty())) {
        throw runtime_error("Unrecorded legacy payment must have zero amount and empty merchant.");
    }
    return o;
}

pair<string, LegacyDetail> deserializeLegacyDetail(const vector<Field>& fields)
{
    if (fields.size() != 4) {
        throw runtime_error("Incorrect number of fields.");
    }
    const string orderId = removeQuotes(fields[0], "Order ID");
    LegacyDetail d;
    d.productId = removeQuotes(fields[1], "Product ID");
    d.barcode = removeQuotes(fields[2], "Barcode");
    d.quantity = parseQuantity(fields[3], false);
    if (orderId.size() != 8 || !isNumeric(orderId) ||
        !isNumeric(d.productId) || !validBarcode(d.barcode)) {
        throw runtime_error("Invalid legacy order detail.");
    }
    return {orderId, std::move(d)};
}

LegacyBill decodeLegacyBill(const vector<Field>& fields)
{
    const int fieldCount = static_cast<int>(fields.size());
    const bool rates = fieldCount > 3 && !fields[3].quoted && fields[3].value == "rates";
    const int width = rates ? 5 : 4;
    const int overhead = rates ? 9 : 8;
    if (fieldCount < overhead + width || (fieldCount - overhead) % width != 0) {
        throw runtime_error("Invalid number of fields.");
    }
    const int itemCount = (fieldCount - overhead) / width;

    LegacyBill b;
    b.billId = removeQuotes(fields[0], "Bill ID");
    b.customerName = removeQuotes(fields[1], "Customer name");
    b.orderId = removeQuotes(fields[2], "Order ID");
    if (b.billId.size() != 9 || !isNumeric(b.billId) ||
        b.orderId.size() != 8 || !isNumeric(b.orderId) ||
        trim(b.customerName).empty()) {
        throw runtime_error("Invalid legacy bill header.");
    }

    int index = rates ? 4 : 3;
    for (int i = 0; i < itemCount; ++i) {
        LegacyBillItem item;
        item.acronym = removeQuotes(fields[index], "Acronym");
        if (trim(item.acronym).empty() || item.acronym.size() > 8) {
            throw runtime_error("Invalid legacy bill item acronym.");
        }
        item.unitPrice = parseMoney(fields[index + 1], "Unit price");
        item.discountPerUnit = parseMoney(fields[index + 2], "Discount per unit");
        item.quantity = parseQuantity(fields[index + 3], false);
        if (item.discountPerUnit > item.unitPrice) {
            throw runtime_error("Legacy bill item discount exceeds unit price.");
        }
        if (rates && removeUnquoted(fields[index + 4], "Tax per unit") != "-") {
            item.taxPerUnit = parseMoney(fields[index + 4], "Tax per unit");
        }
        b.items.push_back(std::move(item));
        index += width;
    }

    b.subtotal = parseMoney(fields[index], "Subtotal");
    b.tax = parseMoney(fields[index + 1], "Tax");
    b.discount = parseMoney(fields[index + 2], "Discount");
    b.payable = parseMoney(fields[index + 3], "Payable");
    b.status = removeUnquoted(fields[index + 4], "Order status");
    if (b.status != "on-hold" && b.status != "paid" && b.status != "archived") {
        throw runtime_error("Invalid legacy bill status.");
    }
    if (b.discount > b.subtotal) {
        throw runtime_error("Legacy bill discount exceeds subtotal.");
    }
    const int64_t net = b.subtotal - b.discount;
    if (b.tax > kMaxMoney - net || b.payable != net + b.tax) {
        throw runtime_error("Inconsistent legacy bill payable total.");
    }
    return b;
}

void verifyPngFile(const filesystem::path& path, const string& productId)
{
    error_code ec;
    const auto st = filesystem::symlink_status(path, ec);
    if (ec || !filesystem::exists(st) || !filesystem::is_regular_file(st)) {
        throw runtime_error("Missing or invalid barcode PNG for product " + productId +
                            ": " + path.u8string());
    }
    ifstream in(path, ios::binary);
    if (!in.is_open()) {
        throw runtime_error("Cannot open barcode PNG for product " + productId +
                            ": " + path.u8string());
    }
    constexpr array<unsigned char, 8> kPngSig = {
        0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A
    };
    array<char, 16> header{};
    in.read(header.data(), static_cast<streamsize>(header.size()));
    if (in.gcount() < 16) {
        throw runtime_error("Truncated barcode PNG for product " + productId +
                            ": " + path.u8string());
    }
    for (size_t i = 0; i < 8; ++i) {
        if (static_cast<unsigned char>(header[i]) != kPngSig[i]) {
            throw runtime_error("Invalid PNG signature for product " + productId +
                                ": " + path.u8string());
        }
    }
    if (header[12] != 'I' || header[13] != 'H' || header[14] != 'D' || header[15] != 'R') {
        throw runtime_error("Corrupt PNG header chunk for product " + productId +
                            ": " + path.u8string());
    }
}

bool filesHaveIdenticalBytes(const filesystem::path& a, const filesystem::path& b)
{
    ifstream fa(a, ios::binary);
    ifstream fb(b, ios::binary);
    if (!fa.is_open() || !fb.is_open()) {
        return false;
    }
    istreambuf_iterator<char> itA(fa), endA;
    istreambuf_iterator<char> itB(fb), endB;
    return equal(itA, endA, itB, endB);
}

} // namespace

// =========================================================================
// PosStorage public methods
// =========================================================================

PosStorage::PosStorage(filesystem::path dataRoot)
    : dataRoot_(std::move(dataRoot)), loaded_(false)
{
}

Status PosStorage::load()
{
    loaded_ = false;
    const string rootStr = dataRoot_.u8string();

    try {
        error_code ec;
        if (filesystem::exists(dataRoot_, ec)) {
            if (ec) {
                throw runtime_error("Cannot inspect data root \"" + rootStr + "\": " + ec.message());
            }
            if (!filesystem::is_directory(dataRoot_, ec) || ec) {
                throw runtime_error("Data path is not a directory: \"" + rootStr + "\".");
            }
        } else if (ec) {
            throw runtime_error("Cannot inspect data root \"" + rootStr + "\": " + ec.message());
        } else {
            filesystem::create_directories(dataRoot_);
        }

        // 1. Recover any interrupted staging directory before inspecting live files.
        const filesystem::path stagingRoot = dataRoot_ / string(kStagingDirName);
        const auto stageStatus = filesystem::symlink_status(stagingRoot, ec);
        if (ec && ec != errc::no_such_file_or_directory) {
            throw runtime_error("Cannot inspect staging directory under \"" + rootStr + "\": " + ec.message());
        }
        if (filesystem::exists(stageStatus)) {
            try {
                if (!filesystem::is_directory(stageStatus)) {
                    throw runtime_error("Corrupt staging entry; writes blocked until resolved.");
                }

                const filesystem::path beforeDir = stagingRoot / "before";
                const filesystem::path afterDir = stagingRoot / "after";
                const filesystem::path preparedMarker = stagingRoot / "prepared";
                const filesystem::path committedMarker = stagingRoot / "committed";

                const bool hasPrepared = filesystem::exists(preparedMarker);
                const bool hasCommitted = filesystem::exists(committedMarker);

                if (hasCommitted && hasPrepared) {
                    if (!filesystem::is_directory(afterDir) || !allLiveFilesExist(afterDir)) {
                        throw runtime_error("Committed staging set missing; leaving business data untouched and blocking writes.");
                    }
                    (void)parseAndValidateInc3Set(afterDir);
                    installSetFromDir(afterDir, stagingRoot, dataRoot_, "rec_");
                    (void)parseAndValidateInc3Set(dataRoot_);
                    error_code retireEc;
                    if (retireAndRemoveStagingDir(dataRoot_, stagingRoot, retireEc) ==
                        StagingCleanupOutcome::RetireFailed) {
                        throw runtime_error("Failed to remove recovered staging directory; blocking writes.");
                    }
                } else if (hasPrepared && !hasCommitted) {
                    if (!filesystem::is_directory(beforeDir)) {
                        throw runtime_error("Prepared backup set missing; leaving business data untouched and blocking writes.");
                    }
                    const filesystem::path absentMarker = beforeDir / ".absent";
                    if (filesystem::exists(absentMarker)) {
                        if (anyLiveFileExists(beforeDir)) {
                            throw runtime_error("Ambiguous prepared backup; blocking writes.");
                        }
                        if (!removeAllLiveFiles(dataRoot_) || anyLiveFileExists(dataRoot_)) {
                            throw runtime_error("Failed to roll back partial first-create files; blocking writes.");
                        }
                    } else {
                        if (!allLiveFilesExist(beforeDir)) {
                            throw runtime_error("Incomplete prepared backup; leaving business data untouched and blocking writes.");
                        }
                        (void)parseAndValidateInc3Set(beforeDir);
                        installSetFromDir(beforeDir, stagingRoot, dataRoot_, "rec_");
                        (void)parseAndValidateInc3Set(dataRoot_);
                    }
                    error_code retireEc;
                    if (retireAndRemoveStagingDir(dataRoot_, stagingRoot, retireEc) ==
                        StagingCleanupOutcome::RetireFailed) {
                        throw runtime_error("Failed to remove rolled-back staging directory; blocking writes.");
                    }
                } else if (!hasPrepared && !hasCommitted) {
                    const bool liveEmpty = !anyLiveFileExists(dataRoot_);
                    const bool beforeEmpty = !filesystem::exists(beforeDir) || !anyLiveFileExists(beforeDir);
                    if (liveEmpty && beforeEmpty) {
                        error_code retireEc;
                        if (retireAndRemoveStagingDir(dataRoot_, stagingRoot, retireEc) ==
                            StagingCleanupOutcome::RetireFailed) {
                            throw runtime_error("Failed to discard unprepared staging directory; blocking writes.");
                        }
                    } else if (allLiveFilesExist(dataRoot_)) {
                        (void)parseAndValidateInc3Set(dataRoot_);
                        error_code retireEc;
                        if (retireAndRemoveStagingDir(dataRoot_, stagingRoot, retireEc) ==
                            StagingCleanupOutcome::RetireFailed) {
                            throw runtime_error("Failed to discard unprepared staging directory; blocking writes.");
                        }
                    } else {
                        throw runtime_error("Unprepared staging directory cannot be discarded because live files are invalid or incomplete; blocking writes.");
                    }
                } else {
                    throw runtime_error("Inconsistent staging markers; leaving business data untouched and blocking writes.");
                }
            } catch (const exception& recErr) {
                throw runtime_error("Recovery failed at data root \"" + rootStr + "\": " + recErr.what());
            }
        }
        cleanupRetiredStagingDirs(dataRoot_);

        // 2. Inspect live files in dataRoot_.
        int existingInc3All = 0;
        int existingInc2 = 0;
        int existingInc3Only = 0;

        for (const char* name : kInc3Files) {
            const filesystem::path p = dataRoot_ / name;
            const auto st = filesystem::symlink_status(p, ec);
            if (ec && ec != errc::no_such_file_or_directory) {
                throw runtime_error("Cannot inspect \"" + p.u8string() + "\": " + ec.message());
            }
            if (filesystem::exists(st)) {
                if (!filesystem::is_regular_file(st)) {
                    throw runtime_error("\"" + p.u8string() + "\" is not a regular file.");
                }
                ++existingInc3All;
            }
        }

        if (existingInc3All == 0) {
            data_ = StoreData{};
            loaded_ = true;
            return {true, "New empty store loaded successfully."};
        }

        for (const char* name : kInc2Files) {
            if (filesystem::exists(dataRoot_ / name)) {
                ++existingInc2;
            }
        }
        for (const char* name : kInc3OnlyFiles) {
            if (filesystem::exists(dataRoot_ / name)) {
                ++existingInc3Only;
            }
        }

        if (existingInc3Only == 0 && existingInc2 == static_cast<int>(kInc2Files.size())) {
            throw runtime_error(
                "Legacy Increment 2 store detected at \"" + rootStr +
                "\". Back up that directory before initializing a fresh Increment 3 data root.");
        }

        if (existingInc3All != static_cast<int>(kInc3Files.size())) {
            string message = "Incomplete or mixed Increment 3 data files at \"" + rootStr + "\". Missing:";
            for (const char* name : kInc3Files) {
                if (!filesystem::exists(dataRoot_ / name)) {
                    message += " ";
                    message += name;
                }
            }
            throw runtime_error(message);
        }

        StoreData loadedData = parseAndValidateInc3Set(dataRoot_);
        data_ = std::move(loadedData);
        loaded_ = true;
        return {true, "All eight POS data files loaded successfully."};
    } catch (const exception& e) {
        data_ = StoreData{};
        loaded_ = false;
        return {false, e.what()};
    }
}

Result<StoreData> PosStorage::read() const
{
    if (!loaded_) {
        return {false, "Storage has not been loaded successfully at \"" + dataRoot_.u8string() + "\".", {}};
    }
    return {true, {}, data_};
}

Status PosStorage::save(const StoreData& data)
{
    const string rootStr = dataRoot_.u8string();
    if (!loaded_) {
        return {false, "Storage is blocked or not loaded at \"" + rootStr + "\"."};
    }

    const filesystem::path stagingRoot = dataRoot_ / string(kStagingDirName);
    {
        error_code existEc;
        const auto stageSt = filesystem::symlink_status(stagingRoot, existEc);
        if ((existEc && existEc != errc::no_such_file_or_directory) || filesystem::exists(stageSt)) {
            loaded_ = false;
            return {false, "Unresolved staging directory exists at \"" + rootStr +
                           "\"; reload and recover storage before saving."};
        }
    }

    const filesystem::path beforeDir = stagingRoot / "before";
    const filesystem::path afterDir = stagingRoot / "after";
    const filesystem::path preparedMarker = stagingRoot / "prepared";
    const filesystem::path committedMarker = stagingRoot / "committed";

    bool prepared = false;
    bool committed = false;
    bool hadLiveFiles = false;

    try {
        validateSnapshot(data);
        StoreData nextData = data;
        const array<vector<string>, 8> serialized = serializeStore(nextData);

        filesystem::create_directories(dataRoot_);

        int existingCount = 0;
        for (const char* name : kInc3Files) {
            const filesystem::path p = dataRoot_ / name;
            error_code fileEc;
            const auto st = filesystem::symlink_status(p, fileEc);
            if (fileEc && fileEc != errc::no_such_file_or_directory) {
                throw runtime_error("Cannot inspect live file \"" + p.u8string() + "\": " + fileEc.message());
            }
            if (filesystem::exists(st)) {
                if (!filesystem::is_regular_file(st)) {
                    throw runtime_error("Non-regular file blocks save of \"" + p.u8string() + "\".");
                }
                ++existingCount;
            }
        }
        if (existingCount != 0 && existingCount != static_cast<int>(kInc3Files.size())) {
            throw runtime_error("Incomplete live file set at \"" + rootStr + "\" blocks save.");
        }
        hadLiveFiles = (existingCount == static_cast<int>(kInc3Files.size()));

        filesystem::create_directories(beforeDir);
        filesystem::create_directories(afterDir);

        if (!hadLiveFiles) {
            writeMarkerChecked(beforeDir / ".absent");
        } else {
            for (const char* name : kInc3Files) {
                copyFileChecked(dataRoot_ / name, beforeDir / name);
            }
            (void)parseAndValidateInc3Set(beforeDir);
        }

        for (size_t i = 0; i < kInc3Files.size(); ++i) {
            writeFileChecked(afterDir / kInc3Files[i], serialized[i]);
        }
        (void)parseAndValidateInc3Set(afterDir);

        writeMarkerChecked(preparedMarker);
        prepared = true;

        installSetFromDir(afterDir, stagingRoot, dataRoot_, "tmp_");
        (void)parseAndValidateInc3Set(dataRoot_);

        writeMarkerChecked(committedMarker);
        committed = true;

        data_ = std::move(nextData);

        error_code cleanupEc;
        const StagingCleanupOutcome cleanupOutcome =
            retireAndRemoveStagingDir(dataRoot_, stagingRoot, cleanupEc);
        if (cleanupOutcome == StagingCleanupOutcome::RetireFailed) {
            loaded_ = false;
            return {true, "Data committed at \"" + rootStr +
                          "\", but staging cleanup failed; reload storage before further writes."};
        }
        if (cleanupOutcome == StagingCleanupOutcome::RetiredButDeleteFailed) {
            return {true, "All eight POS data files saved successfully, but retired staging cleanup is pending at \"" +
                          rootStr + "\"."};
        }

        return {true, "All eight POS data files saved successfully."};
    } catch (const exception& e) {
        if (!prepared) {
            error_code cleanEc;
            if (retireAndRemoveStagingDir(dataRoot_, stagingRoot, cleanEc) ==
                StagingCleanupOutcome::RetireFailed) {
                loaded_ = false;
                return {false, string(e.what()) + " Staging cleanup failed under \"" +
                               rootStr + "\"; writes blocked until reload."};
            }
            return {false, e.what()};
        }

        if (committed) {
            loaded_ = false;
            return {true, string(e.what()) + " Data committed at \"" + rootStr +
                          "\", but staging cleanup failed; reload storage before further writes."};
        }

        bool rolledBack = true;
        try {
            if (!hadLiveFiles) {
                if (!removeAllLiveFiles(dataRoot_) || anyLiveFileExists(dataRoot_)) {
                    rolledBack = false;
                }
            } else {
                installSetFromDir(beforeDir, stagingRoot, dataRoot_, "rb_");
                (void)parseAndValidateInc3Set(dataRoot_);
            }
        } catch (...) {
            rolledBack = false;
        }

        if (!rolledBack) {
            loaded_ = false;
            return {false, string(e.what()) + " Rollback failed under \"" + rootStr +
                           "\"; staging preserved and writes blocked."};
        }

        error_code cleanEc;
        if (retireAndRemoveStagingDir(dataRoot_, stagingRoot, cleanEc) ==
            StagingCleanupOutcome::RetireFailed) {
            loaded_ = false;
            return {false, string(e.what()) + " Rolled back, but staging cleanup failed under \"" +
                           rootStr + "\"; writes blocked until reload."};
        }
        return {false, e.what()};
    }
}

Status PosStorage::importIncrement2Catalogue(const filesystem::path& sourceRoot)
{
    const string destStr = dataRoot_.u8string();
    const string srcStr = sourceRoot.u8string();

    if (!loaded_) {
        return {false, "Destination store at \"" + destStr + "\" must be loaded before import."};
    }
    if (!data_.categories.empty() || !data_.products.empty() || !data_.seasons.empty() ||
        !data_.issuedBarcodes.empty() || !data_.sales.empty() ||
        data_.specialDiscount.active || data_.specialDiscount.rate != 0) {
        return {false, "Destination store at \"" + destStr + "\" is already populated; import requires an empty store."};
    }

    error_code ec;
    if (!filesystem::exists(sourceRoot, ec) || ec || !filesystem::is_directory(sourceRoot, ec) || ec) {
        return {false, "Legacy Increment 2 source directory does not exist: \"" + srcStr + "\"."};
    }
    const filesystem::path canSrc = filesystem::weakly_canonical(filesystem::absolute(sourceRoot, ec), ec);
    error_code ecDest;
    const filesystem::path canDst = filesystem::weakly_canonical(filesystem::absolute(dataRoot_, ecDest), ecDest);
    if (!ec && !ecDest && canSrc == canDst) {
        return {false, "Import source root cannot be the destination data directory \"" + destStr + "\"."};
    }
    if (filesystem::equivalent(sourceRoot, dataRoot_, ec) && !ec) {
        return {false, "Import source root cannot be the destination data directory \"" + destStr + "\"."};
    }

    vector<filesystem::path> copiedAssets;
    auto cleanupCopiedAssets = [&]() {
        for (const filesystem::path& asset : copiedAssets) {
            error_code rmEc;
            filesystem::remove(asset, rmEc);
        }
    };

    try {
        for (const char* inc3File : kInc3OnlyFiles) {
            if (filesystem::exists(sourceRoot / inc3File, ec) || ec) {
                throw runtime_error("Mixed Increment 2/Increment 3 file set at \"" + srcStr +
                                    "\" (unexpected " + inc3File + ").");
            }
        }

        for (const char* inc2File : kInc2Files) {
            const filesystem::path p = sourceRoot / inc2File;
            const auto st = filesystem::symlink_status(p, ec);
            if (ec || !filesystem::exists(st) || !filesystem::is_regular_file(st)) {
                throw runtime_error("Missing or invalid Increment 2 file in \"" + srcStr +
                                    "\": " + inc2File);
            }
        }

        vector<LegacyProduct> legacyProducts;
        unordered_set<string> legacyProductIds;
        unordered_set<string> legacyBarcodes;

        {
            ifstream prodStream(sourceRoot / "Product.txt");
            if (!prodStream.is_open()) {
                throw runtime_error("Unable to open Product.txt.");
            }
            string rawLine;
            int lineNumber = 0;
            while (getline(prodStream, rawLine)) {
                ++lineNumber;
                rawLine = trim(rawLine);
                if (rawLine.empty()) {
                    continue;
                }
                try {
                    LegacyProduct p = deserializeLegacyProduct(removeRecordMarker(rawLine));
                    p.sourceLine = lineNumber;
                    if (!legacyProductIds.insert(p.id).second) {
                        throw runtime_error("Duplicate product ID.");
                    }
                    if (!legacyBarcodes.insert(p.barcode).second) {
                        throw runtime_error("Duplicate barcode.");
                    }
                    legacyProducts.push_back(std::move(p));
                } catch (const exception& lineErr) {
                    throw runtime_error("Product.txt: Line " + to_string(lineNumber) + ": " + lineErr.what());
                }
            }
            if (prodStream.bad() || !prodStream.eof()) {
                throw runtime_error("Error reading Product.txt.");
            }
        }

        unordered_map<string, const LegacyProduct*> legacyProductById;
        for (const LegacyProduct& p : legacyProducts) {
            legacyProductById[p.id] = &p;
        }

        vector<LegacyOrder> legacyOrders;
        unordered_map<string, size_t> legacyOrderIdx;
        readTextFileLines(sourceRoot / "Order.txt", "Order.txt", [&](const string& line) {
            LegacyOrder o = deserializeLegacyOrder(removeRecordMarker(line));
            if (!legacyOrderIdx.emplace(o.orderId, legacyOrders.size()).second) {
                throw runtime_error("Duplicate order ID.");
            }
            legacyOrders.push_back(std::move(o));
        });

        unordered_map<string, vector<LegacyDetail>> legacyDetails;
        vector<string> retiredAndOrderBarcodes;
        readTextFileLines(sourceRoot / "OrderDetail.txt", "OrderDetail.txt", [&](const string& line) {
            auto [orderId, detail] = deserializeLegacyDetail(slices(line));
            if (legacyOrderIdx.find(orderId) == legacyOrderIdx.end()) {
                throw runtime_error("Order ID " + orderId + " does not exist in Order.txt.");
            }
            retiredAndOrderBarcodes.push_back(detail.barcode);
            legacyDetails[orderId].push_back(std::move(detail));
        });

        unordered_map<string, LegacyBill> legacyBillsByOrder;
        unordered_set<string> legacyBillIds;
        readTextFileLines(sourceRoot / "Bill.txt", "Bill.txt", [&](const string& line) {
            LegacyBill b = decodeLegacyBill(removeRecordMarker(line));
            if (!legacyBillIds.insert(b.billId).second) {
                throw runtime_error("Duplicate bill ID.");
            }
            if (legacyOrderIdx.find(b.orderId) == legacyOrderIdx.end()) {
                throw runtime_error("Order ID " + b.orderId + " does not exist in Order.txt.");
            }
            if (!legacyBillsByOrder.emplace(b.orderId, std::move(b)).second) {
                throw runtime_error("Order " + b.orderId + " already has a bill.");
            }
        });

        // Validate Increment 2 cross-file joins and hold consistency.
        for (const LegacyOrder& order : legacyOrders) {
            const auto billIt = legacyBillsByOrder.find(order.orderId);
            if (billIt == legacyBillsByOrder.end()) {
                throw runtime_error("Order " + order.orderId + " does not have a bill.");
            }
            const LegacyBill& bill = billIt->second;
            if (order.customerName != bill.customerName || order.status != bill.status) {
                throw runtime_error("Order and bill mismatch for order " + order.orderId + ".");
            }
            if ((order.status == "paid" && !order.paymentRecorded) ||
                (order.status == "on-hold" && order.paymentRecorded) ||
                (order.paymentRecorded && order.paymentAmount != bill.payable)) {
                throw runtime_error("Payment mismatch for order " + order.orderId + ".");
            }
            const auto detIt = legacyDetails.find(order.orderId);
            if (detIt == legacyDetails.end() || detIt->second.size() != bill.items.size()) {
                throw runtime_error("Detail count mismatch for order " + order.orderId + ".");
            }
            unordered_set<string> seenProdInOrder;
            for (size_t i = 0; i < bill.items.size(); ++i) {
                const LegacyDetail& det = detIt->second[i];
                const LegacyBillItem& bi = bill.items[i];
                if (!seenProdInOrder.insert(det.productId).second || det.quantity != bi.quantity) {
                    throw runtime_error("Line mismatch in order " + order.orderId + ".");
                }
                if (order.status == "on-hold") {
                    const auto prodIt = legacyProductById.find(det.productId);
                    if (prodIt == legacyProductById.end() ||
                        prodIt->second->barcode != det.barcode ||
                        (prodIt->second->unit == Unit::Count && det.quantity % 1000 != 0)) {
                        throw runtime_error("Held order " + order.orderId +
                                            " does not match legacy catalogue.");
                    }
                }
            }
        }

        // Convert legacy products, assign placeholder categories, and validate PNGs.
        StoreData imported;
        map<string, Category> placeholderCategories;
        unordered_set<string> issuedSeen;
        unordered_set<string> destAssetSeen;
        vector<pair<filesystem::path, filesystem::path>> pendingPngCopies;

        for (size_t i = 0; i < legacyProducts.size(); ++i) {
            const LegacyProduct& lp = legacyProducts[i];
            int32_t convertedRate = 0;
            if (!roundedPreDiscountTaxRate(lp.taxPerUnit, lp.unitPrice, convertedRate)) {
                throw runtime_error("Product.txt: Line " + to_string(lp.sourceLine) +
                                    ": Product " + lp.id + " has an invalid or out-of-range tax conversion.");
            }

            const filesystem::path srcImg = sourceRoot / lp.barcodeImagePath;
            verifyPngFile(srcImg, lp.id);

            string destRel = lp.barcodeImagePath;
            if (!validRelativeSubpath(destRel, "barcodes")) {
                const string fname = filesystem::path(lp.barcodeImagePath).filename().u8string();
                destRel = "barcodes/" + fname;
            }
            if (!validRelativeSubpath(destRel, "barcodes")) {
                throw runtime_error("Product.txt: Invalid target PNG path for product " + lp.id + ".");
            }
            if (!destAssetSeen.insert(destRel).second) {
                throw runtime_error("Duplicate destination barcode PNG path: " + destRel);
            }

            const filesystem::path dstImg = dataRoot_ / destRel;
            const auto dstStatus = filesystem::symlink_status(dstImg, ec);
            if (ec && ec != errc::no_such_file_or_directory) {
                throw runtime_error("Cannot inspect destination PNG path: " + dstImg.u8string());
            }
            if (filesystem::exists(dstStatus)) {
                if (!filesystem::is_regular_file(dstStatus) || !filesHaveIdenticalBytes(srcImg, dstImg)) {
                    throw runtime_error("Conflicting PNG already exists at destination: " + dstImg.u8string());
                }
            } else {
                pendingPngCopies.emplace_back(srcImg, dstImg);
            }

            const string prefix = lp.barcode.substr(0, 2);
            if (placeholderCategories.find(prefix) == placeholderCategories.end()) {
                placeholderCategories.emplace(
                    prefix, Category{prefix, "Category " + prefix, "CAT" + prefix});
            }

            Product p;
            p.id = lp.id;
            p.barcode = lp.barcode;
            p.categoryId = prefix;
            p.acronym = lp.acronym;
            p.name = lp.name;
            p.unit = lp.unit;
            p.unitPrice = lp.unitPrice;
            p.taxRate = convertedRate;
            p.discountPerUnit = lp.discountPerUnit;
            p.onHand = lp.available;
            p.barcodeImagePath = destRel;
            imported.products.push_back(std::move(p));

            if (issuedSeen.insert(lp.barcode).second) {
                imported.issuedBarcodes.push_back(lp.barcode);
            }
        }

        for (const auto& [_, cat] : placeholderCategories) {
            imported.categories.push_back(cat);
        }
        for (const string& code : retiredAndOrderBarcodes) {
            if (issuedSeen.insert(code).second) {
                imported.issuedBarcodes.push_back(code);
            }
        }

        validateSnapshot(imported);

        // Copy newly imported PNG assets.
        for (const auto& [srcImg, dstImg] : pendingPngCopies) {
            filesystem::create_directories(dstImg.parent_path());
            copyFileChecked(srcImg, dstImg);
            copiedAssets.push_back(dstImg);
        }

        const Status saveStatus = save(imported);
        if (!saveStatus.success) {
            cleanupCopiedAssets();
            return saveStatus;
        }

        if (!loaded_) {
            return {true, saveStatus.message};
        }

        return {true, "Increment 2 catalogue imported successfully."};
    } catch (const exception& e) {
        cleanupCopiedAssets();
        return {false, e.what()};
    }
}
