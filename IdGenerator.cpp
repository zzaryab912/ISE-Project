
#include "IdGenerator.h"

#include <string>
#include <sstream>
#include <iomanip>
#include <cctype>

namespace id_helpers
{

    // -------------------------------------------------------------------------
    // Check whether a string contains only digits.

    bool isNumeric(const std::string &value)
    {
        if (value.empty())
            return false;

        for (char ch : value)
        {
            if (!std::isdigit(static_cast<unsigned char>(ch)))
                return false;
        }

        return true;
    }

    // -------------------------------------------------------------------------
    // Convert a numeric string to unsigned long long.

    bool toNumber(
        const std::string &value,
        unsigned long long &number)
    {
        if (!isNumeric(value))
            return false;

        number = 0;

        for (char ch : value)
        {
            number = number * 10 + (ch - '0');
        }

        return true;
    }

    // -------------------------------------------------------------------------
    // Validate YYYY-MM-DD and extract the day.

    bool getDay(
        const std::string &date,
        std::string &day)
    {
        if (date.size() != 10 ||
            date[4] != '-' ||
            date[7] != '-')
            return false;

        std::string yearStr = date.substr(0, 4);
        std::string monthStr = date.substr(5, 2);
        std::string dayStr = date.substr(8, 2);

        unsigned long long year, month, dateDay;

        if (!toNumber(yearStr, year) ||
            !toNumber(monthStr, month) ||
            !toNumber(dayStr, dateDay))
            return false;

        if (year == 0 || month < 1 || month > 12)
            return false;

        int daysInMonth[] = {
            31, 28, 31, 30, 31, 30,
            31, 31, 30, 31, 30, 31};

        bool leapYear =
            (year % 400 == 0) ||
            (year % 4 == 0 && year % 100 != 0);

        if (leapYear)
            daysInMonth[1] = 29;

        if (dateDay < 1 || dateDay > daysInMonth[month - 1])
            return false;

        day = dayStr;

        return true;
    }

    // -------------------------------------------------------------------------
    // Format a number using a fixed number of digits.

    std::string formatId(
        unsigned long long number,
        int width)
    {
        std::ostringstream output;

        output << std::setw(width)
               << std::setfill('0')
               << number;

        return output.str();
    }

}

// =============================================================================
// Generate Product ID
// =============================================================================

Result<std::string> IdGenerator::productId(
    const StoreData &data) const
{

    unsigned long long maximumId = 0;

    // Check current product IDs.

    for (const Product &product : data.products)
    {

        unsigned long long id;

        if (!id_helpers::toNumber(product.id, id))
        {
            return {
                false,
                "Invalid numeric product ID found in stored products.",
                {}};
        }

        if (id > maximumId)
            maximumId = id;
    }

    // Check product IDs retained in historical sale records.

    for (const SaleRecord &sale : data.sales)
    {

        for (const SaleLine &line : sale.lines)
        {

            unsigned long long id;

            if (!id_helpers::toNumber(line.productId, id))
            {
                return {
                    false,
                    "Invalid numeric product ID found in historical sales.",
                    {}};
            }

            if (id > maximumId)
                maximumId = id;
        }
    }

    return {
        true,
        "",
        std::to_string(maximumId + 1)};
}

// =============================================================================
// Generate Order ID and Bill ID
// =============================================================================

Result<SaleIds> IdGenerator::saleIds(
    const StoreData &data,
    const Timestamp &timestamp) const
{

    std::string day;

    if (!id_helpers::getDay(timestamp.date, day))
    {
        return {
            false,
            "Invalid date supplied for ID generation.",
            {}};
    }

    unsigned long long maximumOrderSequence = 0;
    unsigned long long maximumBillId = 0;

    // Check all saved sales, including archived sales.

    for (const SaleRecord &sale : data.sales)
    {

        // -------------------------------------------------------------
        // Check Order ID: 6-digit sequence + 2-digit day.

        const std::string &orderId = sale.ids.orderId;

        if (orderId.size() != 8 || !id_helpers::isNumeric(orderId))
        {
            return {
                false,
                "Invalid order ID found in stored sales.",
                {}};
        }

        unsigned long long orderSequence;

        id_helpers::toNumber(orderId.substr(0, 6), orderSequence);

        if (orderSequence > maximumOrderSequence)
            maximumOrderSequence = orderSequence;

        // -------------------------------------------------------------
        // Check Bill ID: exactly 9 digits.

        const std::string &billId = sale.ids.billId;

        if (billId.size() != 9)
        {
            return {
                false,
                "Invalid bill ID length found in stored sales.",
                {}};
        }

        unsigned long long billNumber;

        if (!id_helpers::toNumber(billId, billNumber))
        {
            return {
                false,
                "Invalid bill ID found in stored sales.",
                {}};
        }

        if (billNumber > maximumBillId)
            maximumBillId = billNumber;
    }

    // Generate the next IDs.

    SaleIds ids;

    ids.orderId =
        id_helpers::formatId(maximumOrderSequence + 1, 6) + day;

    ids.billId =
        id_helpers::formatId(maximumBillId + 1, 9);

    return {
        true,
        "",
        ids};
}
