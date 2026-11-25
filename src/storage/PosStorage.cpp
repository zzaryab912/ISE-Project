#include "PosStorage.h"
#include "PosTypes.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iterator>
#include <limits>
#include <map>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

using namespace std;
namespace fs = std::filesystem;

namespace
{
    namespace storage_helpers
    {
        struct Field
        {
            string value;
            bool quoted = false;
        };

        vector<Field> slices(const string &line)
        {
            vector<Field> fields;
            istringstream in(line);
            while (in >> ws)
            {
                if (in.eof())
                    break;
                Field field;
                if (in.peek() == '"')
                {
                    field.quoted = true;
                    in >> quoted(field.value);
                    if (in.fail())
                        throw runtime_error("Invalid quoted string.");
                    if (field.value.find_first_of("\r\n") != string::npos)
                        throw runtime_error("String fields cannot contain line breaks.");
                    int next = in.peek();
                    if (next != char_traits<char>::eof() && !isspace(static_cast<unsigned char>(next)))
                        throw runtime_error("Missing whitespace after quoted string.");
                }
                else
                {
                    if (!(in >> field.value))
                        throw runtime_error("Invalid field.");
                }
                fields.push_back(move(field));
            }
            return fields;
        }

        string trim(const string &s)
        {
            size_t first = s.find_first_not_of(" \t\r\n");
            if (first == string::npos)
                return {};
            size_t last = s.find_last_not_of(" \t\r\n");
            return s.substr(first, last - first + 1);
        }

        string quotedField(const Field &f, const string &name)
        {
            if (!f.quoted)
                throw runtime_error(name + " must be quoted.");
            return f.value;
        }

        string token(const Field &f, const string &name)
        {
            if (f.quoted)
                throw runtime_error(name + " must not be quoted.");
            return f.value;
        }

        bool digits(const string &s)
        {
            return !s.empty() && all_of(s.begin(), s.end(), [](unsigned char c)
                                        { return isdigit(c); });
        }

        int64_t parseInteger(const Field &f, const string &name)
        {
            string s = token(f, name);
            if (s.empty() || s == "-" || s.front() == '+')
                throw runtime_error("Invalid " + name + ".");
            size_t used = 0;
            try
            {
                int64_t value = stoll(s, &used);
                if (used != s.size())
                    throw runtime_error("Invalid " + name + ".");
                return value;
            }
            catch (const out_of_range &)
            {
                throw runtime_error(name + " is out of range.");
            }
        }

        int64_t parseUnsignedInteger(const Field &f, const string &name)
        {
            string s = token(f, name);
            if (!digits(s))
                throw runtime_error("Invalid " + name + ".");
            try
            {
                return stoll(s);
            }
            catch (const out_of_range &)
            {
                throw runtime_error(name + " is out of range.");
            }
        }

        int64_t parseMoney(const Field &f, const string &name, bool signedValue = false)
        {
            string s = token(f, name);
            bool negative = !s.empty() && s.front() == '-';
            if (negative)
            {
                if (!signedValue)
                    throw runtime_error(name + " cannot be negative.");
                s.erase(s.begin());
            }
            size_t dot = s.find('.');
            if (dot == string::npos || dot != s.rfind('.') || s.size() - dot - 1 != 2)
                throw runtime_error(name + " must have exactly two decimal places.");
            string whole = s.substr(0, dot), fraction = s.substr(dot + 1);
            if (!digits(whole) || !digits(fraction))
                throw runtime_error("Invalid " + name + ".");
            try
            {
                int64_t rupees = stoll(whole), paisa = stoll(fraction);
                if (rupees > (numeric_limits<int64_t>::max() - paisa) / 100)
                    throw out_of_range("money");
                int64_t value = rupees * 100 + paisa;
                return negative ? -value : value;
            }
            catch (const out_of_range &)
            {
                throw runtime_error(name + " is out of range.");
            }
        }

        int32_t parsePercent(const Field &f, const string &name)
        {
            int64_t value = parseMoney(f, name);
            if (value > 10000)
                throw runtime_error(name + " exceeds 100.00%.");
            return static_cast<int32_t>(value);
        }

        int64_t parseQuantity(const Field &f, bool allowZero = false)
        {
            string s = token(f, "Quantity");
            size_t dot = s.find('.');
            if (dot != string::npos && dot != s.rfind('.'))
                throw runtime_error("Invalid quantity.");
            string whole = dot == string::npos ? s : s.substr(0, dot);
            string fraction = dot == string::npos ? "" : s.substr(dot + 1);
            if (!digits(whole) || (dot != string::npos && (fraction.empty() || !digits(fraction) || fraction.size() > 3)))
                throw runtime_error("Invalid quantity.");
            while (fraction.size() < 3)
                fraction.push_back('0');
            try
            {
                int64_t a = stoll(whole), b = fraction.empty() ? 0 : stoll(fraction);
                if (a > (numeric_limits<int64_t>::max() - b) / 1000)
                    throw out_of_range("quantity");
                int64_t value = a * 1000 + b;
                if (allowZero ? value < 0 : value <= 0)
                    throw runtime_error("Invalid quantity.");
                return value;
            }
            catch (const out_of_range &)
            {
                throw runtime_error("Quantity is out of range.");
            }
        }

        bool parseBool(const Field &f, const string &name)
        {
            string s = token(f, name);
            if (s == "true")
                return true;
            if (s == "false")
                return false;
            throw runtime_error(name + " must be true or false.");
        }

        vector<Field> record(const string &line)
        {
            vector<Field> fields = slices(line);
            if (fields.empty() || fields.back().quoted || fields.back().value != "#")
                throw runtime_error("Missing # record marker.");
            fields.pop_back();
            return fields;
        }

        void writeQuoted(ostream &out, const string &value)
        {
            if (value.find_first_of("\r\n") != string::npos)
                throw runtime_error("String fields cannot contain line breaks.");
            out << quoted(value) << ' ';
        }

        string moneyString(int64_t value)
        {
            bool negative = value < 0;
            uint64_t magnitude = negative ? static_cast<uint64_t>(-(value + 1)) + 1 : static_cast<uint64_t>(value);
            ostringstream out;
            if (negative)
                out << '-';
            out << magnitude / 100 << '.' << setw(2) << setfill('0') << magnitude % 100;
            return out.str();
        }

        string percentString(int32_t value) { return moneyString(value); }

        string quantityString(int64_t value, Unit unit)
        {
            if (value < 0)
                throw runtime_error("Quantity cannot be negative.");
            if (unit == Unit::Count)
            {
                if (value % 1000 != 0)
                    throw runtime_error("Count quantity must be a whole number.");
                return to_string(value / 1000);
            }
            if (unit != Unit::Weight)
                throw runtime_error("Invalid quantity unit.");
            ostringstream out;
            out << value / 1000 << '.' << setw(3) << setfill('0') << value % 1000;
            return out.str();
        }

        string unitString(Unit unit)
        {
            if (unit == Unit::Count)
                return "count";
            if (unit == Unit::Weight)
                return "weight";
            throw runtime_error("Invalid unit.");
        }

        Unit parseUnit(const Field &f)
        {
            string s = token(f, "Unit");
            if (s == "count")
                return Unit::Count;
            if (s == "weight")
                return Unit::Weight;
            throw runtime_error("Unit must be count or weight.");
        }

        string paymentModeString(PaymentMode mode)
        {
            switch (mode)
            {
            case PaymentMode::Cash:
                return "cash";
            case PaymentMode::Card:
                return "card";
            case PaymentMode::Wallet:
                return "wallet";
            }
            throw runtime_error("Invalid payment mode.");
        }

        PaymentMode parsePaymentMode(const Field &f)
        {
            string s = token(f, "Payment mode");
            if (s == "cash")
                return PaymentMode::Cash;
            if (s == "card")
                return PaymentMode::Card;
            if (s == "wallet")
                return PaymentMode::Wallet;
            throw runtime_error("Invalid payment mode.");
        }

        string changeString(BillChange change)
        {
            switch (change)
            {
            case BillChange::Unchanged:
                return "unchanged";
            case BillChange::Modified:
                return "modified";
            case BillChange::CompleteRefund:
                return "complete-refund";
            }
            throw runtime_error("Invalid bill change state.");
        }

        BillChange parseChange(const Field &f)
        {
            string s = token(f, "Change state");
            if (s == "unchanged")
                return BillChange::Unchanged;
            if (s == "modified")
                return BillChange::Modified;
            if (s == "complete-refund")
                return BillChange::CompleteRefund;
            throw runtime_error("Invalid change state.");
        }

        string billStatus(const SaleRecord &sale)
        {
            if (sale.archived)
                return "archived";
            if (sale.billChange == BillChange::CompleteRefund)
                return "refunded";
            return sale.status == OrderStatus::OnHold ? "on-hold" : "paid";
        }

        bool validDate(const string &date)
        {
            if (date.size() != 10 || date[4] != '-' || date[7] != '-')
                return false;
            string d = date.substr(0, 4) + date.substr(5, 2) + date.substr(8, 2);
            if (!digits(d))
                return false;
            int y = stoi(date.substr(0, 4)), m = stoi(date.substr(5, 2)), day = stoi(date.substr(8, 2));
            if (y < 1 || m < 1 || m > 12 || day < 1)
                return false;
            const int days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
            bool leap = y % 4 == 0 && (y % 100 != 0 || y % 400 == 0);
            return day <= days[m - 1] + (m == 2 && leap ? 1 : 0);
        }

        bool validTime(const string &time)
        {
            if (time.size() != 8 || time[2] != ':' || time[5] != ':')
                return false;
            string d = time.substr(0, 2) + time.substr(3, 2) + time.substr(6, 2);
            if (!digits(d))
                return false;
            return stoi(time.substr(0, 2)) < 24 && stoi(time.substr(3, 2)) < 60 && stoi(time.substr(6, 2)) < 60;
        }

        bool validTimestamp(const Timestamp &t)
        {
            return validDate(t.date) && validTime(t.time) && t.epochSeconds > 0;
        }

        bool validBarcode(const string &s)
        {
            return s.size() == 6 && digits(s) && s[0] != '0';
        }

        bool safeRelativePath(const string &value, const string &expectedFolder)
        {
            if (value.empty() || value.find('\\') != string::npos || value.find(':') != string::npos)
                return false;
            fs::path p(value);
            if (p.is_absolute() || p.has_root_path() || p.begin() == p.end())
                return false;
            for (const auto &part : p)
                if (part.empty() || part == "." || part == "..")
                    return false;
            return *p.begin() == expectedFolder && p.filename() != expectedFolder;
        }

        void readRows(const fs::path &path, const string &fileName,
                      const function<void(const string &, int)> &consume)
        {
            ifstream in(path, ios::binary);
            if (!in.is_open())
                throw runtime_error("Unable to open " + fileName + ".");
            string row;
            int number = 0;
            while (getline(in, row))
            {
                ++number;
                row = trim(row);
                if (row.empty())
                    continue;
                try
                {
                    consume(row, number);
                }
                catch (const exception &e)
                {
                    throw runtime_error(fileName + ": Line " + to_string(number) + ": " + e.what());
                }
            }
            if (in.bad() || !in.eof())
                throw runtime_error("Error reading " + fileName + ".");
        }

        Unit unitForLine(const SaleRecord &sale, const string &productId)
        {
            auto it = find_if(sale.lines.begin(), sale.lines.end(), [&](const SaleLine &line)
                              { return line.productId == productId; });
            if (it == sale.lines.end())
                throw runtime_error("Adjustment refers to an unknown sale line.");
            return it->unit;
        }

        string serializeCategory(const Category &c)
        {
            ostringstream o;
            writeQuoted(o, c.id);
            writeQuoted(o, c.name);
            writeQuoted(o, c.acronym);
            o << '#';
            return o.str();
        }

        string serializeProduct(const Product &p)
        {
            ostringstream o;
            writeQuoted(o, p.id);
            writeQuoted(o, p.barcode);
            writeQuoted(o, p.categoryId);
            writeQuoted(o, p.acronym);
            writeQuoted(o, p.name);
            o << unitString(p.unit) << ' ' << moneyString(p.unitPrice) << ' '
              << percentString(p.taxRate) << ' ' << moneyString(p.discountPerUnit) << ' '
              << quantityString(p.onHand, p.unit) << ' ';
            writeQuoted(o, p.barcodeImagePath);
            o << '#';
            return o.str();
        }

        string serializeOrder(const SaleRecord &s)
        {
            ostringstream o;
            writeQuoted(o, s.ids.orderId);
            writeQuoted(o, s.createdAt.date);
            writeQuoted(o, s.createdAt.time);
            o << s.createdAt.epochSeconds << ' ';
            writeQuoted(o, s.customer.name);
            writeQuoted(o, s.customer.contact);
            o << (s.status == OrderStatus::OnHold ? "on-hold" : "paid") << ' '
              << (s.archived ? "true" : "false") << ' ';
            if (s.payment.recorded)
                o << paymentModeString(s.payment.mode) << ' ';
            else
                o << "- ";
            writeQuoted(o, s.payment.merchant);
            o << moneyString(s.payment.amountReceived) << ' ' << moneyString(s.payment.changeReturned) << " #";
            return o.str();
        }

        string serializeOrderDetail(const string &orderId, const SaleLine &line)
        {
            ostringstream o;
            writeQuoted(o, orderId);
            writeQuoted(o, line.productId);
            writeQuoted(o, line.barcode);
            o << quantityString(line.quantity, line.unit);
            return o.str();
        }

        int64_t collectedAmount(const SaleRecord &s)
        {
            int64_t amount = s.payment.amountReceived;
            for (const BillAdjustment &a : s.adjustments)
                if (a.amountDelta > 0)
                {
                    if (amount > numeric_limits<int64_t>::max() - a.amountDelta)
                        throw runtime_error("Collected amount overflow.");
                    amount += a.amountDelta;
                }
            return amount;
        }

        string serializeBill(const SaleRecord &s)
        {
            Timestamp billAt = s.paidAt ? *s.paidAt : s.createdAt;
            ostringstream o;
            writeQuoted(o, s.ids.billId);
            writeQuoted(o, s.customer.name);
            writeQuoted(o, s.ids.orderId);
            o << moneyString(s.totals.subtotal) << ' ' << moneyString(s.totals.tax) << ' '
              << moneyString(s.totals.productDiscount) << ' ' << moneyString(s.totals.payable) << ' '
              << moneyString(collectedAmount(s)) << ' ' << moneyString(s.payment.changeReturned) << ' '
              << billStatus(s) << ' ' << moneyString(s.totals.seasonalDiscount) << ' '
              << moneyString(s.totals.specialDiscount) << ' ';
            writeQuoted(o, billAt.date);
            writeQuoted(o, billAt.time);
            return o.str();
        }

        string serializeBillDetail(const string &billId, const SaleLine &line)
        {
            ostringstream o;
            writeQuoted(o, billId);
            writeQuoted(o, line.acronym);
            o << moneyString(line.unitPrice) << ' ' << quantityString(line.quantity, line.unit) << ' '
              << moneyString(line.lineProductDiscount);
            return o.str();
        }

        string serializeSeason(const Season &s)
        {
            ostringstream o;
            writeQuoted(o, s.startDate);
            writeQuoted(o, s.endDate);
            o << percentString(s.rate) << ' ' << (s.active ? "true" : "false") << " #";
            return o.str();
        }

        struct TempOrderDetail
        {
            string productId, barcode;
            int64_t quantity = 0;
        };
        struct TempBillDetail
        {
            string acronym;
            int64_t unitPrice = 0, quantity = 0, lineDiscount = 0;
        };
        struct TempBill
        {
            string billId, customerName, orderId, status, date, time;
            Totals totals{};
            int64_t amountReceived = 0, changeReturned = 0;
        };
        struct TempBillMeta
        {
            optional<int64_t> paidEpoch;
            PricingTerms terms{};
            int64_t rounding = 0;
            BillChange change = BillChange::Unchanged;
            uint32_t revision = 0;
            string pdfPath;
        };
        struct TempLineMeta
        {
            size_t ordinal = 0;
            string productId, name;
            Unit unit = Unit::Count;
            int32_t taxRate = 0;
            int64_t discountPerUnit = 0, lineTax = 0;
        };
        struct TempAdjustmentLine
        {
            string billId;
            uint32_t revision = 0;
            LineAdjustment line{};
        };

        int64_t multiplyDivideRound(int64_t a, int64_t b, int64_t divisor)
        {
            if (a < 0 || b < 0 || divisor <= 0)
                throw runtime_error("Invalid scaled-money calculation.");
            int64_t aq = a / divisor, ar = a % divisor;
            int64_t bq = b / divisor, br = b % divisor;
            const int64_t limit = numeric_limits<int64_t>::max();
            if (aq != 0 && b > limit / aq)
                throw runtime_error("Scaled-money calculation overflow.");
            int64_t result = aq * b;
            if (ar != 0 && bq > (limit - result) / ar)
                throw runtime_error("Scaled-money calculation overflow.");
            result += ar * bq;
            int64_t tailProduct = ar * br; // Both remainders are smaller than divisor.
            int64_t tail = (tailProduct + divisor / 2) / divisor;
            if (result > limit - tail)
                throw runtime_error("Scaled-money calculation overflow.");
            return result + tail;
        }

        void validateSnapshot(const StoreData &data)
        {
            unordered_set<string> categoryIds;
            for (const Category &c : data.categories)
            {
                if (c.id.size() != 2 || !digits(c.id) || stoi(c.id) < 10 || stoi(c.id) > 99 ||
                    c.name.empty() || c.acronym.empty() || !categoryIds.insert(c.id).second)
                    throw runtime_error("Invalid or duplicate category.");
            }

            unordered_map<string, const Product *> products;
            unordered_set<string> barcodes;
            for (const Product &p : data.products)
            {
                if (!digits(p.id) || !validBarcode(p.barcode) || p.categoryId.size() != 2 ||
                    p.barcode.substr(0, 2) != p.categoryId || categoryIds.find(p.categoryId) == categoryIds.end() ||
                    p.acronym.empty() || p.acronym.size() > 8 || p.name.empty() ||
                    !safeRelativePath(p.barcodeImagePath, "barcodes") ||
                    p.unitPrice < 0 || p.taxRate < 0 || p.taxRate > 10000 ||
                    p.discountPerUnit < 0 || p.discountPerUnit > p.unitPrice || p.onHand < 0 ||
                    (p.unit != Unit::Count && p.unit != Unit::Weight) ||
                    (p.unit == Unit::Count && p.onHand % 1000 != 0))
                    throw runtime_error("Invalid product " + p.id + ".");
                if (!products.emplace(p.id, &p).second || !barcodes.insert(p.barcode).second)
                    throw runtime_error("Duplicate product ID or barcode.");
            }

            unordered_set<string> issued;
            for (const string &barcode : data.issuedBarcodes)
                if (!validBarcode(barcode) || !issued.insert(barcode).second)
                    throw runtime_error("Invalid or duplicate issued barcode.");
            for (const string &barcode : barcodes)
                if (issued.find(barcode) == issued.end())
                    throw runtime_error("A live barcode is missing from issuedBarcodes.");

            if (data.specialDiscount.rate < 0 || data.specialDiscount.rate > 800)
                throw runtime_error("Special discount must be between 0.00% and 8.00%.");
            for (const Season &s : data.seasons)
                if (!validDate(s.startDate) || !validDate(s.endDate) || s.startDate > s.endDate ||
                    s.rate < 0 || s.rate > 10000)
                    throw runtime_error("Invalid season.");
            if (count_if(data.seasons.begin(), data.seasons.end(), [](const Season &s)
                         { return s.active; }) > 1)
                throw runtime_error("More than one season is enabled.");

            unordered_set<string> orders, bills;
            unordered_map<string, int64_t> reservedByProduct;
            for (const SaleRecord &s : data.sales)
            {
                if (s.ids.orderId.size() != 8 || !digits(s.ids.orderId) ||
                    s.ids.orderId.substr(6) != s.createdAt.date.substr(8) ||
                    s.ids.billId.size() != 9 || !digits(s.ids.billId) || !validTimestamp(s.createdAt) ||
                    !orders.insert(s.ids.orderId).second || !bills.insert(s.ids.billId).second ||
                    s.customer.name.empty() || s.lines.empty() ||
                    (s.status != OrderStatus::OnHold && s.status != OrderStatus::Paid))
                    throw runtime_error("Invalid sale identity, timestamp, customer or status.");
                if (s.status == OrderStatus::OnHold && (s.payment.recorded || s.paidAt))
                    throw runtime_error("Held order cannot have a recorded payment or paid timestamp.");
                if (s.status == OrderStatus::Paid && (!s.payment.recorded || !s.paidAt || !validTimestamp(*s.paidAt)))
                    throw runtime_error("Paid order must have its original payment details.");
                if (s.payment.amountReceived < 0 || s.payment.changeReturned < 0 ||
                    s.payment.changeReturned > s.payment.amountReceived ||
                    (!s.payment.recorded && (s.payment.amountReceived != 0 || s.payment.changeReturned != 0 || !s.payment.merchant.empty())))
                    throw runtime_error("Invalid first payment values.");
                if (s.payment.recorded && s.payment.mode != PaymentMode::Cash &&
                    s.payment.mode != PaymentMode::Card && s.payment.mode != PaymentMode::Wallet)
                    throw runtime_error("Invalid payment mode.");

                const Totals &t = s.totals;
                if (t.subtotal < 0 || t.productDiscount < 0 || t.seasonalDiscount < 0 ||
                    t.specialDiscount < 0 || t.tax < 0 || t.productDiscount > t.subtotal)
                    throw runtime_error("Invalid sale totals.");
                int64_t expected = t.subtotal - t.productDiscount;
                if (t.seasonalDiscount > expected)
                    throw runtime_error("Invalid seasonal discount total.");
                expected -= t.seasonalDiscount;
                if (t.specialDiscount > expected)
                    throw runtime_error("Invalid special discount total.");
                expected -= t.specialDiscount;
                if (expected > numeric_limits<int64_t>::max() - t.tax)
                    throw runtime_error("Sale total overflow.");
                expected += t.tax;
                if ((t.roundingAdjustment > 0 && expected > numeric_limits<int64_t>::max() - t.roundingAdjustment) ||
                    (t.roundingAdjustment < 0 && expected < numeric_limits<int64_t>::min() - t.roundingAdjustment))
                    throw runtime_error("Rounding adjustment overflow.");
                expected += t.roundingAdjustment;
                if (expected < 0 || expected != t.payable)
                    throw runtime_error("Inconsistent payable total for order " + s.ids.orderId + ".");
                if (s.terms.seasonalRate < 0 || s.terms.seasonalRate > 10000 ||
                    s.terms.specialRate < 0 || s.terms.specialRate > 800)
                    throw runtime_error("Invalid saved pricing terms.");
                if ((s.revision == 0 && s.billChange != BillChange::Unchanged) ||
                    (s.revision > 0 && s.billChange == BillChange::Unchanged))
                    throw runtime_error("Bill revision and change state disagree.");
                if (!s.pdfPath.empty() && !safeRelativePath(s.pdfPath, "bills"))
                    throw runtime_error("Invalid PDF path.");

                unordered_set<string> lineProducts;
                int64_t lineDiscountTotal = 0;
                int64_t lineTaxTotal = 0;
                int64_t lineGrossTotal = 0;
                for (const SaleLine &line : s.lines)
                {
                    if (!digits(line.productId) || !validBarcode(line.barcode) || line.acronym.empty() ||
                        line.acronym.size() > 8 || line.name.empty() || line.quantity < 0 ||
                        line.unitPrice < 0 || line.taxRate < 0 || line.taxRate > 10000 ||
                        line.discountPerUnit < 0 || line.discountPerUnit > line.unitPrice ||
                        line.lineProductDiscount < 0 || line.lineTax < 0 ||
                        (line.unit != Unit::Count && line.unit != Unit::Weight) ||
                        (line.unit == Unit::Count && line.quantity % 1000 != 0) ||
                        !lineProducts.insert(line.productId).second)
                        throw runtime_error("Invalid or duplicate sale line in order " + s.ids.orderId + ".");
                    int64_t expectedGross = multiplyDivideRound(line.unitPrice, line.quantity, 1000);
                    int64_t expectedLineDiscount = multiplyDivideRound(line.discountPerUnit, line.quantity, 1000);
                    if (expectedLineDiscount != line.lineProductDiscount)
                        throw runtime_error("Line product discount does not match its unit discount and quantity.");
                    if (lineGrossTotal > numeric_limits<int64_t>::max() - expectedGross ||
                        lineDiscountTotal > numeric_limits<int64_t>::max() - line.lineProductDiscount)
                        throw runtime_error("Line amount overflow.");
                    lineGrossTotal += expectedGross;
                    lineDiscountTotal += line.lineProductDiscount;
                    if (lineTaxTotal > numeric_limits<int64_t>::max() - line.lineTax)
                        throw runtime_error("Line tax overflow.");
                    lineTaxTotal += line.lineTax;
                    if (s.status == OrderStatus::OnHold && !s.archived)
                    {
                        auto it = products.find(line.productId);
                        if (line.quantity <= 0 || it == products.end() || it->second->barcode != line.barcode || it->second->unit != line.unit)
                            throw runtime_error("Held order does not match the live catalogue.");
                        int64_t &reserved = reservedByProduct[line.productId];
                        if (reserved > numeric_limits<int64_t>::max() - line.quantity)
                            throw runtime_error("Reservation quantity overflow.");
                        reserved += line.quantity;
                    }
                }
                if (lineGrossTotal != t.subtotal)
                    throw runtime_error("Gross line subtotals do not match the bill subtotal.");
                if (lineDiscountTotal != t.productDiscount)
                    throw runtime_error("Line product discounts do not match the bill total.");
                if (lineTaxTotal != t.tax)
                    throw runtime_error("Line taxes do not match the bill tax total.");

                uint32_t expectedRevision = 1;
                int64_t adjustmentTotal = 0;
                int64_t positiveAdjustments = 0;
                for (const BillAdjustment &a : s.adjustments)
                {
                    if (a.revision != expectedRevision++ || !validTimestamp(a.at) || a.lines.empty() ||
                        s.status != OrderStatus::Paid || !s.paidAt || a.at.epochSeconds < s.paidAt->epochSeconds)
                        throw runtime_error("Invalid or non-sequential bill adjustment revisions.");
                    if ((a.amountDelta > 0 && adjustmentTotal > numeric_limits<int64_t>::max() - a.amountDelta) ||
                        (a.amountDelta < 0 && adjustmentTotal < numeric_limits<int64_t>::min() - a.amountDelta))
                        throw runtime_error("Adjustment amount overflow.");
                    adjustmentTotal += a.amountDelta;
                    if (a.amountDelta > 0)
                    {
                        if (positiveAdjustments > numeric_limits<int64_t>::max() - a.amountDelta)
                            throw runtime_error("Positive adjustment overflow.");
                        positiveAdjustments += a.amountDelta;
                    }
                    unordered_set<string> changed;
                    for (const LineAdjustment &l : a.lines)
                    {
                        if (!lineProducts.count(l.productId) || l.previousQuantity < 0 || l.quantity < 0 ||
                            l.restockQuantity < 0 || l.restockQuantity > max<int64_t>(0, l.previousQuantity - l.quantity) ||
                            l.previousQuantity == l.quantity || !changed.insert(l.productId).second)
                            throw runtime_error("Invalid adjustment line.");
                        Unit lineUnit = unitForLine(s, l.productId);
                        if (lineUnit == Unit::Count &&
                            (l.previousQuantity % 1000 != 0 || l.quantity % 1000 != 0 || l.restockQuantity % 1000 != 0))
                            throw runtime_error("Count adjustment quantities must be whole items.");
                    }
                }
                for (size_t i = 1; i < s.adjustments.size(); ++i)
                    if (s.adjustments[i].at.epochSeconds < s.adjustments[i - 1].at.epochSeconds)
                        throw runtime_error("Bill adjustment timestamps are not chronological.");
                // Walk edits newest-to-oldest so each updated quantity must match the
                // state produced by the next revision, ending at the current saved line.
                unordered_map<string, int64_t> quantitiesAfterEdits;
                for (const SaleLine &line : s.lines)
                    quantitiesAfterEdits[line.productId] = line.quantity;
                for (auto adjustment = s.adjustments.rbegin(); adjustment != s.adjustments.rend(); ++adjustment)
                {
                    for (const LineAdjustment &line : adjustment->lines)
                    {
                        auto quantity = quantitiesAfterEdits.find(line.productId);
                        if (quantity == quantitiesAfterEdits.end() || quantity->second != line.quantity)
                            throw runtime_error("Adjustment quantity history is discontinuous.");
                        quantity->second = line.previousQuantity;
                    }
                }
                if (s.adjustments.size() != s.revision)
                    throw runtime_error("Bill revision count does not match its adjustments.");
                if (s.status == OrderStatus::Paid)
                {
                    if (s.payment.amountReceived - s.payment.changeReturned < 0)
                        throw runtime_error("Original payment net cannot be negative.");
                    int64_t originalNet = s.payment.amountReceived - s.payment.changeReturned;
                    if (s.payment.mode != PaymentMode::Cash && s.payment.changeReturned != 0)
                        throw runtime_error("Card and Wallet payments cannot return cash change.");
                    if ((adjustmentTotal > 0 && originalNet > numeric_limits<int64_t>::max() - adjustmentTotal) ||
                        (adjustmentTotal < 0 && originalNet < numeric_limits<int64_t>::min() - adjustmentTotal) ||
                        originalNet + adjustmentTotal != t.payable)
                        throw runtime_error("Current payable does not reconcile with original payment and adjustments.");
                    if (positiveAdjustments > numeric_limits<int64_t>::max() - s.payment.amountReceived)
                        throw runtime_error("Collected amount overflow.");
                }
                if (s.billChange == BillChange::CompleteRefund &&
                    (s.status != OrderStatus::Paid || t.payable != 0 ||
                     any_of(s.lines.begin(), s.lines.end(), [](const SaleLine &l)
                            { return l.quantity != 0; })))
                    throw runtime_error("Complete refund must be paid, have zero payable and zero remaining quantities.");
            }
            for (const auto &item : reservedByProduct)
            {
                auto product = products.find(item.first);
                if (product == products.end() || item.second > product->second->onHand)
                    throw runtime_error("Active held reservations exceed physical stock for product " + item.first + ".");
            }
        }

        const vector<string> fileNames = {"PCategory.txt", "Product.txt", "Order.txt", "OrderDetail.txt",
                                          "Bill.txt", "BillDetail.txt", "Seasons.txt", "PosMeta.txt"};

        void writeRows(const fs::path &path, const vector<string> &rows)
        {
            ofstream out(path, ios::binary | ios::trunc);
            if (!out.is_open())
                throw runtime_error("Unable to create " + path.filename().string() + ".");
            for (const string &row : rows)
                out << row << '\n';
            out.close();
            if (!out)
                throw runtime_error("Error writing " + path.filename().string() + ".");
        }

        void installSet(const fs::path &root, const fs::path &setDir, const fs::path &absentDir)
        {
            for (const string &name : fileNames)
            {
                fs::path target = root / name;
                if (fs::exists(absentDir / name))
                {
                    error_code ec;
                    fs::remove(target, ec);
                    if (ec)
                        throw runtime_error("Unable to remove " + name + " during recovery.");
                }
                else
                {
                    fs::copy_file(setDir / name, target, fs::copy_options::overwrite_existing);
                }
            }
        }

        bool sameFile(const fs::path &a, const fs::path &b)
        {
            if (!fs::exists(a) || !fs::exists(b) || fs::file_size(a) != fs::file_size(b))
                return false;
            ifstream x(a, ios::binary), y(b, ios::binary);
            return equal(istreambuf_iterator<char>(x), istreambuf_iterator<char>(),
                         istreambuf_iterator<char>(y), istreambuf_iterator<char>());
        }

        void validateStagedSet(const fs::path &root, const fs::path &setDir,
                               const fs::path &absentDir, const string &label)
        {
            fs::path verifyRoot = root / ".writing" / ("verify_" + label);
            error_code ec;
            fs::remove_all(verifyRoot, ec);
            if (ec)
                throw runtime_error("Unable to prepare " + label + " snapshot verification.");
            fs::create_directories(verifyRoot);
            try
            {
                for (const string &name : fileNames)
                {
                    bool hasFile = fs::is_regular_file(setDir / name);
                    bool markedAbsent = fs::is_regular_file(absentDir / name);
                    if (hasFile == markedAbsent)
                        throw runtime_error("Invalid " + label + " snapshot entry for " + name + ".");
                    if (hasFile)
                        fs::copy_file(setDir / name, verifyRoot / name);
                }
                PosStorage verifier(verifyRoot);
                Status result = verifier.load();
                if (!result.success)
                    throw runtime_error("Invalid " + label + " snapshot: " + result.message);
                fs::remove_all(verifyRoot);
            }
            catch (...)
            {
                error_code cleanupError;
                fs::remove_all(verifyRoot, cleanupError);
                throw;
            }
        }

        void verifyInstalledSet(const fs::path &root, const fs::path &setDir,
                                const fs::path &absentDir)
        {
            for (const string &name : fileNames)
            {
                const fs::path staged = setDir / name;
                const fs::path absent = absentDir / name;
                const fs::path live = root / name;
                if (fs::is_regular_file(staged))
                {
                    if (!sameFile(staged, live))
                        throw runtime_error("Installed file verification failed for " + name + ".");
                }
                else if (fs::is_regular_file(absent))
                {
                    if (fs::exists(live))
                        throw runtime_error("Unexpected live file after recovery: " + name + ".");
                }
                else
                    throw runtime_error("Recovery snapshot is missing an entry for " + name + ".");
            }
        }

        void writeMarker(const fs::path &directory, const string &markerName)
        {
            fs::path temporary = directory / (markerName + ".tmp");
            fs::path markerPath = directory / markerName;
            ofstream out(temporary, ios::binary | ios::trunc);
            if (!out.is_open())
                throw runtime_error("Unable to create " + markerName + " marker.");
            out << markerName << '\n';
            out.close();
            if (!out)
                throw runtime_error("Unable to write " + markerName + " marker.");
            fs::rename(temporary, markerPath);
        }

        uint32_t pngCrc32(const unsigned char *data, size_t size)
        {
            uint32_t crc = 0xFFFFFFFFu;
            for (size_t i = 0; i < size; ++i)
            {
                crc ^= data[i];
                for (int bit = 0; bit < 8; ++bit)
                    crc = (crc & 1u) ? (crc >> 1) ^ 0xEDB88320u : (crc >> 1);
            }
            return crc ^ 0xFFFFFFFFu;
        }

        uint32_t readBigEndian32(const unsigned char *data)
        {
            return (uint32_t(data[0]) << 24) | (uint32_t(data[1]) << 16) |
                   (uint32_t(data[2]) << 8) | uint32_t(data[3]);
        }

        bool validPng(const fs::path &path)
        {
            if (!fs::is_regular_file(path))
                return false;
            ifstream in(path, ios::binary);
            if (!in)
                return false;
            vector<unsigned char> bytes((istreambuf_iterator<char>(in)), istreambuf_iterator<char>());
            if (bytes.size() < 8)
                return false;
            const unsigned char signature[8] = {137, 80, 78, 71, 13, 10, 26, 10};
            if (!equal(begin(signature), end(signature), bytes.begin()))
                return false;

            size_t offset = 8;
            bool firstChunk = true, sawIdat = false, sawIend = false;
            while (offset + 12 <= bytes.size())
            {
                uint32_t length = readBigEndian32(bytes.data() + offset);
                if (static_cast<uint64_t>(length) > bytes.size() - offset - 12)
                    return false;
                const unsigned char *type = bytes.data() + offset + 4;
                const unsigned char *chunkData = bytes.data() + offset + 8;
                const unsigned char *crcBytes = chunkData + length;
                uint32_t storedCrc = readBigEndian32(crcBytes);
                if (pngCrc32(type, static_cast<size_t>(length) + 4) != storedCrc)
                    return false;

                string chunkType(reinterpret_cast<const char *>(type), 4);
                if (firstChunk)
                {
                    if (chunkType != "IHDR" || length != 13)
                        return false;
                    if (readBigEndian32(chunkData) == 0 || readBigEndian32(chunkData + 4) == 0)
                        return false;
                    firstChunk = false;
                }
                else if (chunkType == "IHDR")
                    return false;

                if (chunkType == "IDAT")
                    sawIdat = true;
                if (chunkType == "IEND")
                {
                    if (length != 0 || !sawIdat)
                        return false;
                    sawIend = true;
                    offset += static_cast<size_t>(length) + 12;
                    break;
                }
                offset += static_cast<size_t>(length) + 12;
            }
            return sawIend && offset == bytes.size();
        }
    }
    using namespace storage_helpers;
}

PosStorage::PosStorage(filesystem::path dataRoot)
    : dataRoot_(move(dataRoot)), loaded_(false)
{
}

Status PosStorage::load()
{
    loaded_ = false;
    try
    {
        if (fs::exists(dataRoot_) && !fs::is_directory(dataRoot_))
            throw runtime_error("Data path is not a directory: " + dataRoot_.string());
        const fs::path writing = dataRoot_ / ".writing";
        if (!fs::exists(writing) && fs::is_directory(dataRoot_) &&
            fs::exists(dataRoot_ / "Product.txt") &&
            !fs::exists(dataRoot_ / "PCategory.txt") &&
            !fs::exists(dataRoot_ / "PosMeta.txt"))
            throw runtime_error("Increment 2 data detected at " + dataRoot_.string() +
                                ". Preserve this directory as the legacy source, initialize a separate Increment 3 data root, and import its catalogue.");
        fs::create_directories(dataRoot_);

        // Verify a complete staged snapshot before installing it. Keep .writing
        // evidence until the installed set has also been checked.
        bool discardUnpreparedStage = false;
        if (fs::exists(writing))
        {
            if (fs::exists(writing / "committed"))
            {
                validateStagedSet(dataRoot_, writing / "after", writing / "after_absent", "committed_after");
                installSet(dataRoot_, writing / "after", writing / "after_absent");
                verifyInstalledSet(dataRoot_, writing / "after", writing / "after_absent");
                fs::remove_all(writing);
            }
            else if (fs::exists(writing / "prepared"))
            {
                validateStagedSet(dataRoot_, writing / "before", writing / "before_absent", "prepared_before");
                installSet(dataRoot_, writing / "before", writing / "before_absent");
                verifyInstalledSet(dataRoot_, writing / "before", writing / "before_absent");
                fs::remove_all(writing);
            }
            else
            {
                // No live file is touched before prepared. Keep the stage until the
                // current live set has been completely parsed and validated below.
                discardUnpreparedStage = true;
            }
        }
        if (fs::exists(dataRoot_ / "Product.txt") &&
            !fs::exists(dataRoot_ / "PCategory.txt") &&
            !fs::exists(dataRoot_ / "PosMeta.txt"))
            throw runtime_error("Increment 2 data detected at " + dataRoot_.string() +
                                ". Preserve this directory as the legacy source, initialize a separate Increment 3 data root, and import its catalogue.");

        int present = 0;
        for (const string &name : fileNames)
        {
            fs::path p = dataRoot_ / name;
            if (fs::exists(p))
            {
                if (!fs::is_regular_file(p))
                    throw runtime_error(name + " is not a regular file.");
                ++present;
            }
        }
        if (present == 0)
        {
            if (discardUnpreparedStage)
                fs::remove_all(writing);
            data_ = StoreData{};
            loaded_ = true;
            return {true, "New empty Increment 3 store loaded successfully."};
        }
        if (present != static_cast<int>(fileNames.size()))
        {
            string missing = "Incomplete Increment 3 data set. Missing:";
            for (const string &name : fileNames)
                if (!fs::exists(dataRoot_ / name))
                    missing += " " + name;
            throw runtime_error(missing);
        }

        StoreData loadedData;
        unordered_map<string, size_t> orderIndex, billIndex;
        unordered_map<string, vector<TempOrderDetail>> orderDetails;
        unordered_map<string, vector<TempBillDetail>> billDetails;
        unordered_map<string, TempBill> billsByOrder;
        unordered_map<string, TempBillMeta> billMeta;
        map<string, BillAdjustment> pendingAdjustments;
        unordered_map<string, vector<TempLineMeta>> lineMeta;
        vector<TempAdjustmentLine> adjustmentLines;
        unordered_set<string> productIds, productBarcodes, categoryIds, billIds;
        bool sawFormat = false, sawSpecial = false;
        string row;

        // 1. Categories
        readRows(dataRoot_ / "PCategory.txt", "PCategory.txt", [&](const string &text, int)
                 {
            auto f = record(text);
            if (f.size() != 3) throw runtime_error("Incorrect number of fields.");
            Category c{quotedField(f[0], "Category ID"), quotedField(f[1], "Category name"), quotedField(f[2], "Category acronym")};
            if (c.id.empty() || c.name.empty() || c.acronym.empty() || !categoryIds.insert(c.id).second)
                throw runtime_error("Invalid or duplicate category.");
            loadedData.categories.push_back(move(c)); });

        // 2. Products
        readRows(dataRoot_ / "Product.txt", "Product.txt", [&](const string &text, int)
                 {
            auto f = record(text);
            if (f.size() != 11) throw runtime_error("Incorrect number of fields.");
            Product p;
            p.id = quotedField(f[0], "Product ID"); p.barcode = quotedField(f[1], "Barcode");
            p.categoryId = quotedField(f[2], "Category ID"); p.acronym = quotedField(f[3], "Acronym");
            p.name = quotedField(f[4], "Product name"); p.unit = parseUnit(f[5]);
            p.unitPrice = parseMoney(f[6], "Unit price"); p.taxRate = parsePercent(f[7], "Tax rate");
            p.discountPerUnit = parseMoney(f[8], "Discount per unit"); p.onHand = parseQuantity(f[9], true);
            p.barcodeImagePath = quotedField(f[10], "Barcode image path");
            if (!productIds.insert(p.id).second || !productBarcodes.insert(p.barcode).second)
                throw runtime_error("Duplicate product ID or barcode.");
            loadedData.products.push_back(move(p)); });

        // 3. Seasons
        readRows(dataRoot_ / "Seasons.txt", "Seasons.txt", [&](const string &text, int)
                 {
            auto f = record(text);
            if (f.size() != 4) throw runtime_error("Incorrect number of fields.");
            Season s;
            s.startDate = quotedField(f[0], "Season start date"); s.endDate = quotedField(f[1], "Season end date");
            s.rate = parsePercent(f[2], "Season percentage"); s.active = parseBool(f[3], "Season active flag");
            loadedData.seasons.push_back(move(s)); });

        // 4. Orders
        readRows(dataRoot_ / "Order.txt", "Order.txt", [&](const string &text, int)
                 {
            auto f = record(text);
            if (f.size() != 12) throw runtime_error("Incorrect number of fields.");
            SaleRecord s;
            s.ids.orderId = quotedField(f[0], "Order ID");
            s.createdAt.date = quotedField(f[1], "Creation date"); s.createdAt.time = quotedField(f[2], "Creation time");
            s.createdAt.epochSeconds = parseInteger(f[3], "Creation epoch");
            s.customer.name = quotedField(f[4], "Customer name"); s.customer.contact = quotedField(f[5], "Customer contact");
            string status = token(f[6], "Order status");
            if (status == "on-hold") s.status = OrderStatus::OnHold;
            else if (status == "paid") s.status = OrderStatus::Paid;
            else throw runtime_error("Order status must be on-hold or paid.");
            s.archived = parseBool(f[7], "Archived flag");
            string mode = token(f[8], "Payment mode");
            if (mode == "-") s.payment.recorded = false;
            else { s.payment.recorded = true; s.payment.mode = parsePaymentMode(f[8]); }
            s.payment.merchant = quotedField(f[9], "Merchant");
            s.payment.amountReceived = parseMoney(f[10], "First amount received");
            s.payment.changeReturned = parseMoney(f[11], "First change returned");
            if (!orderIndex.emplace(s.ids.orderId, loadedData.sales.size()).second)
                throw runtime_error("Duplicate order ID.");
            loadedData.sales.push_back(move(s)); });

        // 5. Order details (no # terminator in this file).
        readRows(dataRoot_ / "OrderDetail.txt", "OrderDetail.txt", [&](const string &text, int)
                 {
            auto f = slices(text);
            if (f.size() != 4) throw runtime_error("Incorrect number of fields.");
            string orderId = quotedField(f[0], "Order ID");
            TempOrderDetail d{quotedField(f[1], "Product ID"), quotedField(f[2], "Barcode"), parseQuantity(f[3], true)};
            if (!orderIndex.count(orderId) || d.productId.empty() || !validBarcode(d.barcode))
                throw runtime_error("Invalid or unknown order detail reference.");
            orderDetails[orderId].push_back(move(d)); });

        // 6. Bill summaries (no # terminator in this file).
        readRows(dataRoot_ / "Bill.txt", "Bill.txt", [&](const string &text, int)
                 {
            auto f = slices(text);
            if (f.size() != 14) throw runtime_error("Incorrect number of fields.");
            TempBill b;
            b.billId = quotedField(f[0], "Bill ID"); b.customerName = quotedField(f[1], "Customer name");
            b.orderId = quotedField(f[2], "Order ID");
            b.totals.subtotal = parseMoney(f[3], "Gross subtotal"); b.totals.tax = parseMoney(f[4], "Bill tax");
            b.totals.productDiscount = parseMoney(f[5], "Product discount"); b.totals.payable = parseMoney(f[6], "Payable");
            b.amountReceived = parseMoney(f[7], "Amount received"); b.changeReturned = parseMoney(f[8], "Balance returned");
            b.status = token(f[9], "Bill status"); b.totals.seasonalDiscount = parseMoney(f[10], "Seasonal discount");
            b.totals.specialDiscount = parseMoney(f[11], "Special discount");
            b.date = quotedField(f[12], "Bill date"); b.time = quotedField(f[13], "Bill time");
            if (!billIds.insert(b.billId).second || !orderIndex.count(b.orderId) || billsByOrder.count(b.orderId))
                throw runtime_error("Duplicate bill or invalid order-to-bill relationship.");
            billsByOrder.emplace(b.orderId, move(b)); });

        // 7. Bill details (no # terminator in this file).
        readRows(dataRoot_ / "BillDetail.txt", "BillDetail.txt", [&](const string &text, int)
                 {
            auto f = slices(text);
            if (f.size() != 5) throw runtime_error("Incorrect number of fields.");
            string billId = quotedField(f[0], "Bill ID");
            TempBillDetail d{quotedField(f[1], "Product acronym"), parseMoney(f[2], "Unit price"),
                             parseQuantity(f[3], true), parseMoney(f[4], "Line product discount")};
            if (!billIds.count(billId)) throw runtime_error("Unknown bill ID in BillDetail.txt.");
            billDetails[billId].push_back(move(d)); });

        // 8. Metadata. The format row must be first; the other records are tagged.
        readRows(dataRoot_ / "PosMeta.txt", "PosMeta.txt", [&](const string &text, int)
                 {
            auto f = record(text);
            if (f.empty()) throw runtime_error("Empty metadata record.");
            string tag = token(f[0], "Metadata tag");
            if (!sawFormat && tag != "format") throw runtime_error("The first metadata record must be format.");
            if (tag == "format")
            {
                if (sawFormat || f.size() != 2 || parseUnsignedInteger(f[1], "Schema version") != 3)
                    throw runtime_error("Invalid or unsupported metadata format record.");
                sawFormat = true;
            }
            else if (tag == "special")
            {
                if (sawSpecial || f.size() != 3) throw runtime_error("Invalid or duplicate special record.");
                loadedData.specialDiscount.rate = parsePercent(f[1], "Special discount");
                loadedData.specialDiscount.active = parseBool(f[2], "Special active flag");
                if (loadedData.specialDiscount.rate > 800) throw runtime_error("Special discount exceeds 8.00%.");
                sawSpecial = true;
            }
            else if (tag == "issued")
            {
                if (f.size() != 2) throw runtime_error("Invalid issued record.");
                loadedData.issuedBarcodes.push_back(quotedField(f[1], "Issued barcode"));
            }
            else if (tag == "bill")
            {
                if (f.size() != 9) throw runtime_error("Invalid bill metadata record.");
                string id = quotedField(f[1], "Bill ID");
                TempBillMeta m;
                string epoch = token(f[2], "Original payment epoch");
                if (epoch != "-") m.paidEpoch = parseInteger(f[2], "Original payment epoch");
                m.terms.seasonalRate = parsePercent(f[3], "Applied seasonal percentage");
                m.terms.specialRate = parsePercent(f[4], "Applied special percentage");
                m.rounding = parseMoney(f[5], "Rounding adjustment", true);
                m.change = parseChange(f[6]);
                int64_t revision = parseUnsignedInteger(f[7], "Bill revision");
                if (revision > numeric_limits<uint32_t>::max()) throw runtime_error("Bill revision out of range.");
                m.revision = static_cast<uint32_t>(revision);
                if (f[8].quoted) m.pdfPath = quotedField(f[8], "PDF path");
                else if (token(f[8], "PDF path") != "-") throw runtime_error("PDF path must be quoted or -.");
                if (!billMeta.emplace(id, move(m)).second) throw runtime_error("Duplicate bill metadata.");
            }
            else if (tag == "line")
            {
                if (f.size() != 9) throw runtime_error("Invalid line metadata record.");
                string billId = quotedField(f[1], "Bill ID");
                int64_t ordinal = parseUnsignedInteger(f[2], "Line ordinal");
                if (ordinal < 1 || static_cast<uint64_t>(ordinal) > numeric_limits<size_t>::max()) throw runtime_error("Invalid line ordinal.");
                TempLineMeta m;
                m.ordinal = static_cast<size_t>(ordinal); m.productId = quotedField(f[3], "Product ID");
                m.name = quotedField(f[4], "Historical product name"); m.unit = parseUnit(f[5]);
                m.taxRate = parsePercent(f[6], "Tax rate"); m.discountPerUnit = parseMoney(f[7], "Discount per unit");
                m.lineTax = parseMoney(f[8], "Line tax");
                lineMeta[billId].push_back(move(m));
            }
            else if (tag == "adjust")
            {
                if (f.size() != 10) throw runtime_error("Invalid adjustment record.");
                string billId = quotedField(f[1], "Bill ID");
                int64_t rev = parseUnsignedInteger(f[2], "Adjustment revision");
                if (rev < 1 || rev > numeric_limits<uint32_t>::max()) throw runtime_error("Invalid adjustment revision.");
                BillAdjustment a;
                a.revision = static_cast<uint32_t>(rev);
                a.at.date = quotedField(f[3], "Edit date"); a.at.time = quotedField(f[4], "Edit time");
                a.at.epochSeconds = parseInteger(f[5], "Edit epoch");
                a.amountDelta = parseMoney(f[6], "Payable difference", true);
                a.productDiscountDelta = parseMoney(f[7], "Product discount difference", true);
                a.seasonalDiscountDelta = parseMoney(f[8], "Seasonal discount difference", true);
                a.specialDiscountDelta = parseMoney(f[9], "Special discount difference", true);
                string key = billId + "_" + to_string(rev);
                // Keep adjustments in a temporary lookup until bill IDs are joined to orders.
                if (!pendingAdjustments.emplace(key, move(a)).second) throw runtime_error("Duplicate adjustment revision.");
            }
            else if (tag == "adjust-line")
            {
                if (f.size() != 7) throw runtime_error("Invalid adjustment-line record.");
                TempAdjustmentLine a;
                a.billId = quotedField(f[1], "Bill ID");
                int64_t rev = parseUnsignedInteger(f[2], "Adjustment revision");
                if (rev < 1 || rev > numeric_limits<uint32_t>::max()) throw runtime_error("Invalid adjustment revision.");
                a.revision = static_cast<uint32_t>(rev);
                a.line.productId = quotedField(f[3], "Product ID");
                a.line.previousQuantity = parseQuantity(f[4], true); a.line.quantity = parseQuantity(f[5], true);
                a.line.restockQuantity = parseQuantity(f[6], true);
                adjustmentLines.push_back(move(a));
            }
            else throw runtime_error("Unknown metadata tag: " + tag + "."); });

        if (!sawFormat || !sawSpecial)
            throw runtime_error("PosMeta.txt must contain one format and one special record.");
        if (billsByOrder.size() != loadedData.sales.size())
            throw runtime_error("Every order must have exactly one bill.");
        if (orderDetails.size() != loadedData.sales.size() || billDetails.size() != billsByOrder.size() || lineMeta.size() != billsByOrder.size())
            throw runtime_error("Unexpected or missing order/bill detail groups.");
        if (billMeta.size() != billsByOrder.size())
            throw runtime_error("Every bill must have exactly one bill metadata record.");

        // Build bill lookup and attach current bill snapshots to the matching order.
        for (const auto &pair : billsByOrder)
        {
            const string &orderId = pair.first;
            const TempBill &b = pair.second;
            SaleRecord &s = loadedData.sales.at(orderIndex.at(orderId));
            if (s.customer.name != b.customerName)
                throw runtime_error("Customer name mismatch for order " + orderId + ".");
            s.ids.billId = b.billId;
            if (!billIndex.emplace(b.billId, orderIndex.at(orderId)).second)
                throw runtime_error("Duplicate bill ID.");
            s.totals = b.totals;
            if (b.status == "archived")
            {
                if (!s.archived)
                    throw runtime_error("Bill archived flag does not match Order.txt.");
            }
            else if (b.status == "on-hold")
            {
                if (s.status != OrderStatus::OnHold || s.archived)
                    throw runtime_error("Bill status does not match order.");
            }
            else if (b.status == "paid" || b.status == "refunded")
            {
                if (s.status != OrderStatus::Paid || s.archived)
                    throw runtime_error("Bill status does not match order.");
            }
            else
                throw runtime_error("Invalid bill status.");
            if (b.status == "refunded" && !s.archived)
            { /* checked against metadata below */
            }
            if (!validDate(b.date) || !validTime(b.time))
                throw runtime_error("Invalid bill date or time.");

            auto metaIt = billMeta.find(b.billId);
            if (metaIt == billMeta.end())
                throw runtime_error("Missing bill metadata for " + b.billId + ".");
            const TempBillMeta &m = metaIt->second;
            s.terms = m.terms;
            s.totals.roundingAdjustment = m.rounding;
            s.billChange = m.change;
            s.revision = m.revision;
            s.pdfPath = m.pdfPath;
            if (m.paidEpoch)
            {
                if (s.status != OrderStatus::Paid)
                    throw runtime_error("Held order has a payment timestamp.");
                s.paidAt = Timestamp{b.date, b.time, *m.paidEpoch};
            }
            else
            {
                if (s.status == OrderStatus::Paid)
                    throw runtime_error("Paid order is missing its original payment timestamp.");
                if (b.date != s.createdAt.date || b.time != s.createdAt.time)
                    throw runtime_error("Held bill date/time must match order creation time.");
            }
            if (b.status == "refunded" && s.billChange != BillChange::CompleteRefund)
                throw runtime_error("Refunded bill is missing complete-refund metadata.");
            if (b.status == "paid" && s.billChange == BillChange::CompleteRefund)
                throw runtime_error("Complete refund must use refunded bill status.");

            int64_t expectedReceived = s.payment.amountReceived;
            auto adjustmentIt = pendingAdjustments.lower_bound(b.billId + "_");
            for (; adjustmentIt != pendingAdjustments.end() && adjustmentIt->first.rfind(b.billId + "_", 0) == 0; ++adjustmentIt)
                if (adjustmentIt->second.amountDelta > 0)
                {
                    if (expectedReceived > numeric_limits<int64_t>::max() - adjustmentIt->second.amountDelta)
                        throw runtime_error("Received amount overflow.");
                    expectedReceived += adjustmentIt->second.amountDelta;
                }
            if (expectedReceived != b.amountReceived || s.payment.changeReturned != b.changeReturned)
                throw runtime_error("Bill payment summary does not match first tender and adjustments.");

            auto od = orderDetails.find(orderId);
            auto bd = billDetails.find(b.billId);
            auto lm = lineMeta.find(b.billId);
            if (od == orderDetails.end() || bd == billDetails.end() || lm == lineMeta.end())
                throw runtime_error("Missing order, bill or metadata line details for bill " + b.billId + ".");
            if (od->second.size() != bd->second.size() || od->second.size() != lm->second.size())
                throw runtime_error("Line count mismatch for bill " + b.billId + ".");
            auto &metadataLines = lm->second;
            sort(metadataLines.begin(), metadataLines.end(), [](const TempLineMeta &a, const TempLineMeta &b)
                 { return a.ordinal < b.ordinal; });
            for (size_t i = 0; i < od->second.size(); ++i)
            {
                const auto &d = od->second[i];
                const auto &item = bd->second[i];
                const auto &meta = metadataLines[i];
                if (meta.ordinal != i + 1 || d.quantity != item.quantity || d.productId != meta.productId)
                    throw runtime_error("OrderDetail, BillDetail and line metadata do not match at line " + to_string(i + 1) + ".");
                SaleLine line;
                line.productId = d.productId;
                line.barcode = d.barcode;
                line.acronym = item.acronym;
                line.name = meta.name;
                line.unit = meta.unit;
                line.unitPrice = item.unitPrice;
                line.taxRate = meta.taxRate;
                line.discountPerUnit = meta.discountPerUnit;
                line.quantity = d.quantity;
                line.lineProductDiscount = item.lineDiscount;
                line.lineTax = meta.lineTax;
                s.lines.push_back(move(line));
            }
        }

        // Attach adjustment rows after all bill and sale objects exist.
        for (auto &entry : pendingAdjustments)
        {
            size_t split = entry.first.rfind('_');
            string billId = entry.first.substr(0, split);
            auto it = billIndex.find(billId);
            if (it == billIndex.end())
                throw runtime_error("Adjustment refers to unknown bill " + billId + ".");
            loadedData.sales[it->second].adjustments.push_back(move(entry.second));
        }
        for (const TempAdjustmentLine &temp : adjustmentLines)
        {
            auto it = billIndex.find(temp.billId);
            if (it == billIndex.end())
                throw runtime_error("Adjustment line refers to unknown bill.");
            SaleRecord &s = loadedData.sales[it->second];
            auto a = find_if(s.adjustments.begin(), s.adjustments.end(), [&](const BillAdjustment &x)
                             { return x.revision == temp.revision; });
            if (a == s.adjustments.end())
                throw runtime_error("Adjustment line has no matching adjustment revision.");
            a->lines.push_back(temp.line);
        }
        for (SaleRecord &s : loadedData.sales)
            sort(s.adjustments.begin(), s.adjustments.end(), [](const BillAdjustment &a, const BillAdjustment &b)
                 { return a.revision < b.revision; });

        validateSnapshot(loadedData);
        if (discardUnpreparedStage)
            fs::remove_all(writing);
        data_ = move(loadedData);
        loaded_ = true;
        return {true, "All eight Increment 3 files loaded successfully."};
    }
    catch (const exception &e)
    {
        loaded_ = false;
        string message = e.what();
        if (fs::exists(dataRoot_ / ".writing"))
            message += " Recovery evidence is retained at " + (dataRoot_ / ".writing").string() +
                       "; data root: " + dataRoot_.string() + ".";
        return {false, message};
    }
}

Result<StoreData> PosStorage::read() const
{
    if (!loaded_)
        return {false, "Storage has not been loaded successfully.", {}};
    return {true, "", data_};
}

Status PosStorage::save(const StoreData &data)
{
    if (!loaded_)
        return {false, "Storage has not been loaded successfully."};
    const fs::path writing = dataRoot_ / ".writing";
    StoreData nextData{};
    bool haveNextData = false;
    try
    {
        validateSnapshot(data);
        nextData = data;
        haveNextData = true;
        if (fs::exists(writing))
            throw runtime_error("A .writing recovery directory already exists at " + dataRoot_.string() + ".");
        fs::create_directories(writing / "before");
        fs::create_directories(writing / "before_absent");
        fs::create_directories(writing / "after");
        fs::create_directories(writing / "after_absent");

        // Capture the complete previous file set, including explicit absences.
        for (const string &name : fileNames)
        {
            fs::path live = dataRoot_ / name;
            if (fs::is_regular_file(live))
                fs::copy_file(live, writing / "before" / name);
            else if (!fs::exists(live))
                ofstream(writing / "before_absent" / name).close();
            else
                throw runtime_error("Existing data path is not a regular file: " + live.string());
        }

        vector<string> categoryRows, productRows, orderRows, orderDetailRows, billRows, billDetailRows, seasonRows, metaRows;
        for (const Category &c : data.categories)
            categoryRows.push_back(serializeCategory(c));
        for (const Product &p : data.products)
            productRows.push_back(serializeProduct(p));
        for (const Season &season : data.seasons)
            seasonRows.push_back(serializeSeason(season));

        metaRows.push_back("format 3 #");
        metaRows.push_back("special " + percentString(data.specialDiscount.rate) + " " + (data.specialDiscount.active ? "true" : "false") + " #");
        for (const string &barcode : data.issuedBarcodes)
        {
            ostringstream o;
            o << "issued ";
            writeQuoted(o, barcode);
            o << '#';
            metaRows.push_back(o.str());
        }

        for (const SaleRecord &sale : data.sales)
        {
            orderRows.push_back(serializeOrder(sale));
            for (const SaleLine &line : sale.lines)
            {
                orderDetailRows.push_back(serializeOrderDetail(sale.ids.orderId, line));
                billDetailRows.push_back(serializeBillDetail(sale.ids.billId, line));
            }
            billRows.push_back(serializeBill(sale));

            ostringstream billMeta;
            billMeta << "bill ";
            writeQuoted(billMeta, sale.ids.billId);
            if (sale.paidAt)
                billMeta << sale.paidAt->epochSeconds << ' ';
            else
                billMeta << "- ";
            billMeta << percentString(sale.terms.seasonalRate) << ' ' << percentString(sale.terms.specialRate) << ' '
                     << moneyString(sale.totals.roundingAdjustment) << ' ' << changeString(sale.billChange) << ' '
                     << sale.revision << ' ';
            if (sale.pdfPath.empty())
                billMeta << "- ";
            else
                writeQuoted(billMeta, sale.pdfPath);
            billMeta << '#';
            metaRows.push_back(billMeta.str());

            for (size_t i = 0; i < sale.lines.size(); ++i)
            {
                const SaleLine &line = sale.lines[i];
                ostringstream m;
                m << "line ";
                writeQuoted(m, sale.ids.billId);
                m << i + 1 << ' ';
                writeQuoted(m, line.productId);
                writeQuoted(m, line.name);
                m << unitString(line.unit) << ' ' << percentString(line.taxRate) << ' '
                  << moneyString(line.discountPerUnit) << ' ' << moneyString(line.lineTax) << " #";
                metaRows.push_back(m.str());
            }
            for (const BillAdjustment &adjustment : sale.adjustments)
            {
                ostringstream m;
                m << "adjust ";
                writeQuoted(m, sale.ids.billId);
                m << adjustment.revision << ' ';
                writeQuoted(m, adjustment.at.date);
                writeQuoted(m, adjustment.at.time);
                m << adjustment.at.epochSeconds << ' ' << moneyString(adjustment.amountDelta) << ' '
                  << moneyString(adjustment.productDiscountDelta) << ' ' << moneyString(adjustment.seasonalDiscountDelta) << ' '
                  << moneyString(adjustment.specialDiscountDelta) << " #";
                metaRows.push_back(m.str());
                for (const LineAdjustment &line : adjustment.lines)
                {
                    Unit unit = unitForLine(sale, line.productId);
                    ostringstream x;
                    x << "adjust-line ";
                    writeQuoted(x, sale.ids.billId);
                    x << adjustment.revision << ' ';
                    writeQuoted(x, line.productId);
                    x << quantityString(line.previousQuantity, unit) << ' '
                      << quantityString(line.quantity, unit) << ' '
                      << quantityString(line.restockQuantity, unit) << " #";
                    metaRows.push_back(x.str());
                }
            }
        }

        vector<vector<string>> rows = {categoryRows, productRows, orderRows, orderDetailRows,
                                       billRows, billDetailRows, seasonRows, metaRows};
        for (size_t i = 0; i < fileNames.size(); ++i)
            writeRows(writing / "after" / fileNames[i], rows[i]);

        // Parse both staged snapshots before changing any live business file.
        validateStagedSet(dataRoot_, writing / "before", writing / "before_absent", "before");
        validateStagedSet(dataRoot_, writing / "after", writing / "after_absent", "after");
        writeMarker(writing, "prepared");

        installSet(dataRoot_, writing / "after", writing / "after_absent");
        verifyInstalledSet(dataRoot_, writing / "after", writing / "after_absent");
        // The after-set was parsed above and every installed byte is verified equal.
        writeMarker(writing, "committed");

        data_ = move(nextData);
        loaded_ = true;
        try
        {
            fs::remove_all(writing);
            return {true, "All eight Increment 3 files saved successfully."};
        }
        catch (const exception &cleanupError)
        {
            // The committed marker is authoritative. Do not report a failed save
            // and invite a caller to repeat a transaction that has already committed.
            return {true, string("Data committed successfully; recovery cleanup is pending: ") + cleanupError.what()};
        }
    }
    catch (const exception &e)
    {
        try
        {
            if (fs::exists(writing / "committed"))
            {
                if (haveNextData)
                    data_ = move(nextData);
                loaded_ = true;
                return {true, string("Data committed successfully; recovery cleanup is pending: ") + e.what()};
            }
            if (fs::exists(writing / "prepared"))
            {
                validateStagedSet(dataRoot_, writing / "before", writing / "before_absent", "rollback_before");
                installSet(dataRoot_, writing / "before", writing / "before_absent");
                verifyInstalledSet(dataRoot_, writing / "before", writing / "before_absent");
                fs::remove_all(writing);
            }
            else if (fs::exists(writing))
            {
                // Live files have not been touched before the prepared marker.
                fs::remove_all(writing);
            }
        }
        catch (const exception &rollbackError)
        {
            loaded_ = false;
            return {false, string(e.what()) + " Recovery/rollback failed at " + dataRoot_.string() +
                               "; .writing evidence was preserved: " + rollbackError.what()};
        }
        return {false, e.what()};
    }
}

// Import only the Increment 2 catalogue. Old transactions are deliberately not imported.
Status PosStorage::importIncrement2Catalogue(const filesystem::path &sourceRoot)
{
    vector<fs::path> copiedImages;
    try
    {
        if (!loaded_)
            throw runtime_error("Initialize the Increment 3 destination before importing.");
        if (sourceRoot.empty() || !fs::is_directory(sourceRoot))
            throw runtime_error("Increment 2 source root is not a directory.");
        if (fs::weakly_canonical(sourceRoot) == fs::weakly_canonical(dataRoot_))
            throw runtime_error("Increment 2 source and Increment 3 destination must be different.");
        if (!data_.categories.empty() || !data_.products.empty() || !data_.sales.empty() ||
            !data_.seasons.empty() || !data_.issuedBarcodes.empty())
            throw runtime_error("Increment 3 destination must be empty before catalogue import.");
        for (const string &name : fileNames)
            if (fs::exists(dataRoot_ / name))
                throw runtime_error("Destination already contains " + name + ".");

        StoreData imported;
        unordered_map<string, size_t> categoryIndex;
        unordered_set<string> ids, barcodes;
        const fs::path oldProductPath = sourceRoot / "Product.txt";
        if (!fs::is_regular_file(oldProductPath))
            throw runtime_error("Increment 2 Product.txt is missing.");

        readRows(oldProductPath, "Increment 2 Product.txt", [&](const string &text, int)
                 {
            auto f = record(text);
            if (f.size() != 10) throw runtime_error("Unexpected Increment 2 product record format.");
            Product p;
            p.id = quotedField(f[0], "Product ID");
            p.barcode = quotedField(f[1], "Barcode");
            p.acronym = quotedField(f[2], "Acronym");
            p.name = quotedField(f[3], "Product name");
            p.unit = parseUnit(f[4]);
            p.unitPrice = parseMoney(f[5], "Unit price");
            int64_t oldTaxPerUnit = parseMoney(f[6], "Legacy tax per unit");
            p.discountPerUnit = parseMoney(f[7], "Discount per unit");
            p.onHand = parseQuantity(f[8], true);
            string oldImage = quotedField(f[9], "Barcode image path");
            if (!digits(p.id) || !validBarcode(p.barcode) || !ids.insert(p.id).second || !barcodes.insert(p.barcode).second)
                throw runtime_error("Invalid or duplicate legacy product ID/barcode.");
            if (oldTaxPerUnit < 0 || (p.unitPrice == 0 && oldTaxPerUnit != 0) || oldTaxPerUnit > p.unitPrice)
                throw runtime_error("Legacy per-unit tax cannot be converted safely for product " + p.id + ".");
            if (p.unitPrice == 0) p.taxRate = 0;
            else p.taxRate = static_cast<int32_t>(floor((static_cast<long double>(oldTaxPerUnit) * 10000.0L / p.unitPrice) + 0.5L));
            if (p.taxRate > 10000) throw runtime_error("Converted tax rate exceeds 100% for product " + p.id + ".");

            p.categoryId = p.barcode.substr(0, 2);
            if (!categoryIndex.count(p.categoryId))
            {
                Category c{p.categoryId, "Imported category " + p.categoryId, p.categoryId};
                categoryIndex[p.categoryId] = imported.categories.size();
                imported.categories.push_back(move(c));
            }
            string normalized = oldImage;
            replace(normalized.begin(), normalized.end(), '\\', '/');
            fs::path oldPath(normalized);
            if (oldPath.is_absolute() || oldPath.has_root_path()) throw runtime_error("Legacy barcode image path must be relative.");
            for (const auto& part : oldPath) if (part == "..") throw runtime_error("Legacy barcode image path escapes the source root.");
            fs::path sourceImage = sourceRoot / oldPath;
            if (!validPng(sourceImage)) throw runtime_error("Missing or invalid PNG barcode image for product " + p.id + ".");
            fs::path relativeImage = fs::path("barcodes") / (p.barcode + ".png");
            fs::path destinationImage = dataRoot_ / relativeImage;
            fs::create_directories(destinationImage.parent_path());
            fs::copy_file(sourceImage, destinationImage, fs::copy_options::overwrite_existing);
            copiedImages.push_back(destinationImage);
            p.barcodeImagePath = relativeImage.generic_string();
            imported.issuedBarcodes.push_back(p.barcode);
            imported.products.push_back(move(p)); });

        // Retired/deleted labels can still occur in old order details; never reissue them.
        const fs::path oldDetails = sourceRoot / "OrderDetail.txt";
        if (!fs::is_regular_file(oldDetails))
            throw runtime_error("Increment 2 OrderDetail.txt is missing.");
        readRows(oldDetails, "Increment 2 OrderDetail.txt", [&](const string &text, int)
                 {
            auto f = slices(text);
            if (f.size() != 4) throw runtime_error("Unexpected Increment 2 order-detail format.");
            string orderId = quotedField(f[0], "Historical order ID");
            string productId = quotedField(f[1], "Historical product ID");
            string barcode = quotedField(f[2], "Historical barcode");
            if (orderId.empty() || !digits(orderId) || productId.empty() || !digits(productId) ||
                !validBarcode(barcode))
                throw runtime_error("Invalid historical order-detail identity or barcode.");
            (void)parseQuantity(f[3], true);
            if (barcodes.insert(barcode).second) imported.issuedBarcodes.push_back(barcode); });

        Status saved = save(imported);
        if (!saved.success)
            throw runtime_error(saved.message);
        return {true, "Increment 2 catalogue imported successfully."};
    }
    catch (const exception &e)
    {
        for (const fs::path &path : copiedImages)
        {
            error_code ec;
            fs::remove(path, ec);
        }
        return {false, e.what()};
    }
}
