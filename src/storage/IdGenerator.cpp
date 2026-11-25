#include "IdGenerator.h"

#include <charconv>
#include <iomanip>
#include <limits>
#include <sstream>
#include <string>

using namespace std;

namespace
{

    // Check whether a string contains only ASCII decimal digits.
    bool isNumeric(const string &value)
    {
        if (value.empty())
            return false;

        for (char ch : value)
        {
            if (ch < '0' || ch > '9')
                return false;
        }

        return true;
    }

    // Convert a numeric string to unsigned long long.
    bool toNumber(
        const string &value,
        unsigned long long &number)
    {
        if (!isNumeric(value))
            return false;

        const auto end = value.data() + value.size();
        const auto parsed = from_chars(value.data(), end, number);
        return parsed.ec == errc{} && parsed.ptr == end;
    }

    // Validate YYYY-MM-DD and extract the two-digit day.
    bool getDay(
        const string &date,
        string &day)
    {
        if (date.size() != 10 ||
            date[4] != '-' ||
            date[7] != '-')
            return false;

        const string yearStr = date.substr(0, 4);
        const string monthStr = date.substr(5, 2);
        const string dayStr = date.substr(8, 2);

        unsigned long long year = 0;
        unsigned long long month = 0;
        unsigned long long dateDay = 0;

        if (!toNumber(yearStr, year) ||
            !toNumber(monthStr, month) ||
            !toNumber(dayStr, dateDay))
            return false;

        if (year == 0 || month < 1 || month > 12)
            return false;

        unsigned long long daysInMonth[] = {
            31, 28, 31, 30, 31, 30,
            31, 31, 30, 31, 30, 31};

        const bool leapYear =
            (year % 400 == 0) ||
            (year % 4 == 0 && year % 100 != 0);

        if (leapYear)
            daysInMonth[1] = 29;

        if (dateDay < 1 || dateDay > daysInMonth[month - 1])
            return false;

        day = dayStr;
        return true;
    }

    // Format a number using a fixed zero-padded width.
    string formatId(
        unsigned long long number,
        int width)
    {
        ostringstream output;

        output << setw(width)
               << setfill('0')
               << number;

        return output.str();
    }

} // namespace

// =============================================================================
// Generate Product ID
// =============================================================================

Result<string> IdGenerator::productId(const StoreData &data) const
{
    unsigned long long maximumId = 0;

    // Scan live catalogue product IDs.
    for (const Product &product : data.products)
    {
        unsigned long long id = 0;
        if (!toNumber(product.id, id))
        {
            return {false, "Invalid numeric product ID found in stored products.", {}};
        }

        if (id > maximumId)
            maximumId = id;
    }

    // Scan historical product IDs retained in saved and adjusted sales (including archived).
    for (const SaleRecord &sale : data.sales)
    {
        for (const SaleLine &line : sale.lines)
        {
            unsigned long long id = 0;
            if (!toNumber(line.productId, id))
            {
                return {false, "Invalid numeric product ID found in historical sales.", {}};
            }

            if (id > maximumId)
                maximumId = id;
        }

        for (const BillAdjustment &adjustment : sale.adjustments)
        {
            for (const LineAdjustment &lineAdjustment : adjustment.lines)
            {
                unsigned long long id = 0;
                if (!toNumber(lineAdjustment.productId, id))
                {
                    return {false, "Invalid numeric product ID found in historical bill adjustments.", {}};
                }

                if (id > maximumId)
                    maximumId = id;
            }
        }
    }

    if (maximumId == numeric_limits<unsigned long long>::max())
        return {false, "No product IDs remain in the supported range.", {}};

    return {true, {}, to_string(maximumId + 1)};
}

// =============================================================================
// Generate Order ID and Bill ID
// =============================================================================

Result<SaleIds> IdGenerator::saleIds(
    const StoreData &data,
    const Timestamp &timestamp) const
{
    string day;
    if (!getDay(timestamp.date, day))
    {
        return {false, "Invalid date supplied for ID generation.", {}};
    }

    unsigned long long maximumOrderSequence = 0;
    unsigned long long maximumBillId = 0;

    // Scan all saved sales, including archived records.
    for (const SaleRecord &sale : data.sales)
    {
        const string &orderId = sale.ids.orderId;
        if (orderId.size() != 8 || !isNumeric(orderId))
        {
            return {false, "Invalid order ID found in stored sales.", {}};
        }

        unsigned long long orderSequence = 0;
        if (!toNumber(orderId.substr(0, 6), orderSequence))
        {
            return {false, "Invalid order ID sequence found in stored sales.", {}};
        }

        if (orderSequence > maximumOrderSequence)
            maximumOrderSequence = orderSequence;

        const string &billId = sale.ids.billId;
        if (billId.size() != 9)
        {
            return {false, "Invalid bill ID length found in stored sales.", {}};
        }

        unsigned long long billNumber = 0;
        if (!toNumber(billId, billNumber))
        {
            return {false, "Invalid bill ID found in stored sales.", {}};
        }

        if (billNumber > maximumBillId)
            maximumBillId = billNumber;
    }

    if (maximumOrderSequence >= 999999 || maximumBillId >= 999999999)
        return {false, "The order or bill ID sequence is exhausted.", {}};

    SaleIds ids;
    ids.orderId = formatId(maximumOrderSequence + 1, 6) + day;
    ids.billId = formatId(maximumBillId + 1, 9);

    return {true, {}, ids};
}
