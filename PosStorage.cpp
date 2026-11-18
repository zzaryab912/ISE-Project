
#include "PosStorage.h"
#include "PosTypes.h"

#include <fstream>
#include <sstream>
#include <vector>
#include <string>
#include <stdexcept>
#include <cstdint>
#include <algorithm>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <iomanip>
#include <filesystem>
#include <limits>
#include <cctype>

using namespace std;

namespace
{

    // =========================================================================
    // Helper functions
    // =========================================================================

    namespace storage_helpers
    {

        struct Field
        {
            string value;
            bool quoted = false;
        };

        // Split using whitespace outside quoted strings.
        vector<Field> slices(const string &line)
        {
            vector<Field> fields;
            istringstream stream(line);

            while (stream >> ws)
            {
                if (stream.eof())
                    break;

                Field field;

                if (stream.peek() == '"')
                {
                    field.quoted = true;
                    stream >> quoted(field.value);

                    if (stream.fail())
                        throw runtime_error("Invalid quoted string.");
                    if (field.value.find_first_of("\r\n") != string::npos)
                        throw runtime_error("String fields cannot contain line breaks.");

                    if (!stream.eof() &&
                        !isspace(static_cast<unsigned char>(stream.peek())))
                    {
                        throw runtime_error(
                            "Missing whitespace after quoted string.");
                    }
                }
                else
                {
                    field.quoted = false;

                    if (!(stream >> field.value))
                        throw runtime_error("Invalid field.");
                }

                fields.push_back(move(field));
            }

            return fields;
        }

        string removeQuotes(
            const Field &field,
            const string &fieldName)
        {
            if (!field.quoted)
                throw runtime_error(
                    fieldName + " must be enclosed in double quotes.");

            return field.value;
        }

        string removeUnquoted(
            const Field &field,
            const string &fieldName)
        {
            if (field.quoted)
                throw runtime_error(
                    fieldName + " must not be enclosed in double quotes.");

            return field.value;
        }

        bool isNumeric(const string &s)
        {
            if (s.empty())
                return false;

            for (unsigned char c : s)
            {
                if (!isdigit(c))
                    return false;
            }

            return true;
        }

        // Money is stored in paisa.
        // Example: 1250.50 -> 125050.
        int64_t parseMoney(
            const Field &field,
            const string &fieldName)
        {
            string s = removeUnquoted(field, fieldName);

            size_t dot = s.find('.');

            if (dot == string::npos ||
                dot != s.rfind('.') ||
                s.size() - dot - 1 != 2)
            {
                throw runtime_error(
                    fieldName + " must have exactly two decimal places.");
            }

            string whole = s.substr(0, dot);
            string fraction = s.substr(dot + 1);

            if (!isNumeric(whole) || !isNumeric(fraction))
                throw runtime_error(fieldName + " is invalid.");

            try
            {
                int64_t rupees = stoll(whole);
                int64_t paisa = stoll(fraction);

                if (rupees >
                    (numeric_limits<int64_t>::max() - paisa) / 100)
                {
                    throw out_of_range("Money out of range.");
                }

                return rupees * 100 + paisa;
            }
            catch (const out_of_range &)
            {
                throw runtime_error(fieldName + " is out of range.");
            }
        }

        // Quantity is stored in thousandths.
        // Example: 1.500 -> 1500.
        int64_t parseQuantity(const Field &field, bool allowZero = false)
        {
            string s = removeUnquoted(field, "Quantity");

            size_t dot = s.find('.');

            if (dot != string::npos && dot != s.rfind('.'))
                throw runtime_error("Invalid quantity.");

            string whole =
                (dot == string::npos) ? s : s.substr(0, dot);

            string fraction =
                (dot == string::npos) ? "" : s.substr(dot + 1);

            if (!isNumeric(whole) ||
                (dot != string::npos &&
                 (fraction.empty() ||
                  !isNumeric(fraction) ||
                  fraction.size() > 3)))
            {
                throw runtime_error("Invalid quantity.");
            }

            while (fraction.size() < 3)
                fraction.push_back('0');

            try
            {
                int64_t integerPart = stoll(whole);
                int64_t decimalPart =
                    fraction.empty() ? 0 : stoll(fraction);

                if (integerPart >
                    (numeric_limits<int64_t>::max() - decimalPart) / 1000)
                {
                    throw out_of_range("Quantity out of range.");
                }

                int64_t quantity =
                    integerPart * 1000 + decimalPart;

                if (allowZero ? quantity < 0 : quantity <= 0)
                    throw runtime_error("Invalid quantity.");

                return quantity;
            }
            catch (const out_of_range &)
            {
                throw runtime_error("Quantity is out of range.");
            }
        }

        string trim(const string &s)
        {
            size_t start = s.find_first_not_of(" \t\r\n");

            if (start == string::npos)
                return "";

            size_t end = s.find_last_not_of(" \t\r\n");

            return s.substr(start, end - start + 1);
        }

        // ---------------------------------------------------------
        // Formatting helpers
        // ---------------------------------------------------------

        void writeQuoted(ostream &out, const string &value)
        {
            if (value.find('\n') != string::npos ||
                value.find('\r') != string::npos)
            {
                throw runtime_error(
                    "String fields cannot contain line breaks.");
            }

            out << quoted(value) << ' ';
        }

        string moneyString(int64_t value)
        {
            ostringstream out;

            out << value / 100 << '.'
                << setw(2) << setfill('0') << value % 100;

            return out.str();
        }

        string quantityString(int64_t value)
        {
            int64_t whole = value / 1000;
            int64_t fraction = value % 1000;

            ostringstream out;
            out << whole;

            if (fraction != 0)
            {
                out << '.'
                    << setw(3) << setfill('0') << fraction;

                string result = out.str();

                while (result.back() == '0')
                    result.pop_back();

                return result;
            }

            return out.str();
        }

        string unitString(Unit unit)
        {
            switch (unit)
            {
            case Unit::Count:
                return "count";
            case Unit::Weight:
                return "weight";
            }

            throw runtime_error("Invalid unit.");
        }

        string statusString(OrderStatus status)
        {
            switch (status)
            {
            case OrderStatus::OnHold:
                return "on-hold";
            case OrderStatus::Paid:
                return "paid";
            case OrderStatus::Archived:
                return "archived";
            default:
                throw runtime_error("Unsupported order status.");
            }
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

        // ---------------------------------------------------------
        // Serialization functions
        // ---------------------------------------------------------

        string serializeProduct(const Product &product)
        {
            ostringstream out;

            writeQuoted(out, product.id);
            writeQuoted(out, product.barcode);
            writeQuoted(out, product.acronym);
            writeQuoted(out, product.name);

            out << unitString(product.unit) << ' '
                << moneyString(product.unitPrice) << ' '
                << moneyString(product.taxPerUnit) << ' '
                << moneyString(product.discountPerUnit) << ' '
                << quantityString(product.available) << ' ';

            writeQuoted(out, product.barcodeImagePath);
            out << '#';

            return out.str();
        }

        string serializeOrder(const SaleRecord &sale)
        {
            ostringstream out;

            writeQuoted(out, sale.ids.orderId);
            writeQuoted(out, sale.timestamp.date);
            writeQuoted(out, sale.timestamp.time);
            writeQuoted(out, sale.customer.name);
            writeQuoted(out, sale.customer.contact);

            out << statusString(sale.status) << ' '
                << moneyString(sale.payment.amount) << ' ';

            if (sale.payment.recorded)
                out << paymentModeString(sale.payment.mode) << ' ';
            else
                out << "- ";

            writeQuoted(out, sale.payment.merchant);
            out << '#';

            return out.str();
        }

        string serializeOrderDetail(
            const string &orderId,
            const SaleLine &item)
        {
            ostringstream out;

            writeQuoted(out, orderId);
            writeQuoted(out, item.productId);
            writeQuoted(out, item.barcode);

            out << quantityString(item.quantity);

            return out.str();
        }

        string serializeBill(const SaleRecord &sale)
        {
            ostringstream out;

            writeQuoted(out, sale.ids.billId);
            writeQuoted(out, sale.customer.name);
            writeQuoted(out, sale.ids.orderId);

            for (const SaleLine &item : sale.lines)
            {
                writeQuoted(out, item.acronym);

                out << moneyString(item.unitPrice) << ' '
                    << moneyString(item.discountPerUnit) << ' '
                    << quantityString(item.quantity) << ' ';
            }

            out << moneyString(sale.totals.subtotal) << ' '
                << moneyString(sale.totals.tax) << ' '
                << moneyString(sale.totals.discount) << ' '
                << moneyString(sale.totals.payable) << ' '
                << statusString(sale.status) << " #";

            return out.str();
        }

    } // namespace storage_helpers

    using namespace storage_helpers;

    // =========================================================================
    // Enums for Deserialization
    // =========================================================================

    enum class ProductField
    {
        ID,
        BARCODE,
        ACRONYM,
        NAME,
        UNIT,
        UNIT_PRICE,
        TAX,
        DISCOUNT,
        AVAILABLE,
        BARCODE_IMAGE_PATH,
        COUNT
    };

    enum class OrderField
    {
        ORDER_ID,
        DATE,
        TIME,
        CUSTOMER_NAME,
        CUSTOMER_CONTACT,
        STATUS,
        PAYMENT_AMOUNT,
        PAYMENT_MODE,
        MERCHANT,
        COUNT
    };

    // =========================================================================
    // Temporary structures
    // =========================================================================

    struct TempDetail
    {
        string productId;
        string barcode;
        int64_t quantity = 0;
    };

    struct BillItem
    {
        string acronym;
        int64_t unitPrice = 0;
        int64_t discountPerUnit = 0;
        int64_t quantity = 0;
    };

    struct BillLine
    {
        SaleIds ids{};
        string customerName;
        Totals totals{};
        vector<BillItem> items;
        OrderStatus status = OrderStatus::InProgress;
    };

    // =========================================================================
    // Record marker handling & Deserializers
    // =========================================================================

    vector<Field> removeRecordMarker(const string &line)
    {
        vector<Field> fields = slices(line);

        if (fields.empty() || fields.back().quoted || fields.back().value != "#")
        {
            throw runtime_error("Missing # record marker.");
        }

        fields.pop_back();
        return fields;
    }

    Product deserializeProduct(const vector<Field> &fields)
    {
        if (fields.size() != (int)ProductField::COUNT)
            throw runtime_error("Incorrect number of fields.");

        Product product;

        product.id = removeQuotes(fields[(int)ProductField::ID], "Product ID");
        product.barcode = removeQuotes(fields[(int)ProductField::BARCODE], "Barcode");
        product.acronym = removeQuotes(fields[(int)ProductField::ACRONYM], "Acronym");
        product.name = removeQuotes(fields[(int)ProductField::NAME], "Product name");
        product.barcodeImagePath = removeQuotes(fields[(int)ProductField::BARCODE_IMAGE_PATH], "Barcode image path");

        if (product.id.empty() || product.barcode.empty() || product.acronym.empty() ||
            product.name.empty() || product.barcodeImagePath.empty())
            throw runtime_error("Product fields cannot be empty.");

        if (!isNumeric(product.barcode) || product.barcode.size() != 6 || product.barcode[0] == '0')
            throw runtime_error("Barcode must contain exactly 6 digits and cannot start with 0.");

        if (product.acronym.size() > 8)
            throw runtime_error("Acronym cannot exceed 8 characters.");

        string unitStr = removeUnquoted(fields[(int)ProductField::UNIT], "Unit");

        if (unitStr == "count")
            product.unit = Unit::Count;
        else if (unitStr == "weight")
            product.unit = Unit::Weight;
        else
            throw runtime_error("Unit must be count or weight.");

        product.unitPrice = parseMoney(fields[(int)ProductField::UNIT_PRICE], "Unit price");
        product.taxPerUnit = parseMoney(fields[(int)ProductField::TAX], "Tax");
        product.discountPerUnit = parseMoney(fields[(int)ProductField::DISCOUNT], "Discount");
        product.available = parseQuantity(fields[(int)ProductField::AVAILABLE], true);

        return product;
    }

    SaleRecord deserializeOrder(const vector<Field> &fields)
    {
        if (fields.size() != (int)OrderField::COUNT)
            throw runtime_error("Incorrect number of fields.");

        SaleRecord sale;

        sale.ids.orderId = removeQuotes(fields[(int)OrderField::ORDER_ID], "Order ID");
        sale.timestamp.date = removeQuotes(fields[(int)OrderField::DATE], "Date");
        sale.timestamp.time = removeQuotes(fields[(int)OrderField::TIME], "Time");
        sale.customer.name = removeQuotes(fields[(int)OrderField::CUSTOMER_NAME], "Customer name");
        sale.customer.contact = removeQuotes(fields[(int)OrderField::CUSTOMER_CONTACT], "Customer contact");
        sale.payment.merchant = removeQuotes(fields[(int)OrderField::MERCHANT], "Merchant");

        if (sale.ids.orderId.empty() || sale.timestamp.date.empty() ||
            sale.timestamp.time.empty() || sale.customer.name.empty())
            throw runtime_error("Mandatory order fields cannot be empty.");

        string statusStr = removeUnquoted(fields[(int)OrderField::STATUS], "Order status");

        if (statusStr == "on-hold")
            sale.status = OrderStatus::OnHold;
        else if (statusStr == "paid")
            sale.status = OrderStatus::Paid;
        else if (statusStr == "archived")
            sale.status = OrderStatus::Archived;
        else
            throw runtime_error("Invalid order status.");

        sale.payment.amount = parseMoney(fields[(int)OrderField::PAYMENT_AMOUNT], "Payment amount");

        string modeStr = removeUnquoted(fields[(int)OrderField::PAYMENT_MODE], "Payment mode");

        if (modeStr == "cash")
        {
            sale.payment.mode = PaymentMode::Cash;
            sale.payment.recorded = true;
        }
        else if (modeStr == "card")
        {
            sale.payment.mode = PaymentMode::Card;
            sale.payment.recorded = true;
        }
        else if (modeStr == "wallet")
        {
            sale.payment.mode = PaymentMode::Wallet;
            sale.payment.recorded = true;
        }
        else if (modeStr == "-")
        {
            sale.payment.recorded = false;
            sale.payment.mode = PaymentMode::Cash;
        }
        else
        {
            throw runtime_error("Invalid payment mode.");
        }

        if (!sale.payment.recorded &&
            (sale.payment.amount != 0 || !sale.payment.merchant.empty()))
            throw runtime_error("Unrecorded payment must have zero amount and empty merchant.");

        return sale;
    }

    pair<string, TempDetail> deserializeDetail(const vector<Field> &fields)
    {
        if (fields.size() != 4)
            throw runtime_error("Incorrect number of fields.");

        string orderId = removeQuotes(fields[0], "Order ID");
        string productId = removeQuotes(fields[1], "Product ID");
        string barcode = removeQuotes(fields[2], "Barcode");

        if (orderId.empty() || productId.empty())
            throw runtime_error("Order ID and Product ID cannot be empty.");

        if (!isNumeric(barcode) || barcode.size() != 6 || barcode[0] == '0')
            throw runtime_error("Invalid barcode.");

        TempDetail detail;

        detail.productId = productId;
        detail.barcode = barcode;
        detail.quantity = parseQuantity(fields[3]);

        return {orderId, detail};
    }

    BillLine decodeBill(const vector<Field> &fields)
    {
        int fieldCount = (int)fields.size();

        if (fieldCount < 12 || (fieldCount - 8) % 4 != 0)
            throw runtime_error("Invalid number of fields.");

        int itemCount = (fieldCount - 8) / 4;
        BillLine bill;

        bill.ids.billId = removeQuotes(fields[0], "Bill ID");
        bill.customerName = removeQuotes(fields[1], "Customer name");
        bill.ids.orderId = removeQuotes(fields[2], "Order ID");

        if (bill.ids.billId.empty() || bill.customerName.empty() || bill.ids.orderId.empty())
            throw runtime_error("Bill IDs and customer name cannot be empty.");

        int index = 3;

        for (int i = 0; i < itemCount; ++i)
        {
            BillItem item;

            item.acronym = removeQuotes(fields[index], "Acronym");

            if (item.acronym.empty())
                throw runtime_error("Acronym cannot be empty.");

            item.unitPrice = parseMoney(fields[index + 1], "Unit price");
            item.discountPerUnit = parseMoney(fields[index + 2], "Discount per unit");
            item.quantity = parseQuantity(fields[index + 3]);

            bill.items.push_back(move(item));
            index += 4;
        }

        bill.totals.subtotal = parseMoney(fields[index], "Subtotal");
        bill.totals.tax = parseMoney(fields[index + 1], "Tax");
        bill.totals.discount = parseMoney(fields[index + 2], "Discount");
        bill.totals.payable = parseMoney(fields[index + 3], "Payable");

        string statusStr = removeUnquoted(fields[index + 4], "Order status");

        if (statusStr == "on-hold")
            bill.status = OrderStatus::OnHold;
        else if (statusStr == "paid")
            bill.status = OrderStatus::Paid;
        else if (statusStr == "archived")
            bill.status = OrderStatus::Archived;
        else
            throw runtime_error("Invalid order status.");

        return bill;
    }

    bool validTimestamp(const Timestamp &timestamp)
    {
        const string &date = timestamp.date;
        const string &time = timestamp.time;
        if (date.size() != 10 || date[4] != '-' || date[7] != '-' ||
            time.size() != 8 || time[2] != ':' || time[5] != ':' ||
            !isNumeric(date.substr(0, 4) + date.substr(5, 2) + date.substr(8, 2)) ||
            !isNumeric(time.substr(0, 2) + time.substr(3, 2) + time.substr(6, 2)))
            return false;

        const int year = stoi(date.substr(0, 4));
        const int month = stoi(date.substr(5, 2));
        const int day = stoi(date.substr(8, 2));
        if (year == 0 || month < 1 || month > 12 || day < 1)
            return false;

        constexpr int daysByMonth[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
        const bool leap = year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
        return day <= daysByMonth[month - 1] + (month == 2 && leap ? 1 : 0) &&
            stoi(time.substr(0, 2)) < 24 && stoi(time.substr(3, 2)) < 60 &&
            stoi(time.substr(6, 2)) < 60;
    }

    bool validBarcode(const string &barcode)
    {
        return barcode.size() == 6 && barcode.front() != '0' && isNumeric(barcode);
    }

    void validateSnapshot(const StoreData &data)
    {
        unordered_map<string, const Product *> products;
        unordered_set<string> barcodes;
        for (const Product &product : data.products)
        {
            const filesystem::path imagePath(product.barcodeImagePath);
            if (!isNumeric(product.id) || !validBarcode(product.barcode) ||
                trim(product.acronym).empty() || product.acronym.size() > 8 ||
                trim(product.name).empty() || imagePath.empty() || imagePath.has_root_path() ||
                any_of(imagePath.begin(), imagePath.end(),
                       [](const filesystem::path &part) { return part == ".."; }) ||
                product.available < 0 || product.unitPrice < 0 || product.taxPerUnit < 0 ||
                product.discountPerUnit < 0 || product.discountPerUnit > product.unitPrice ||
                (product.unit != Unit::Count && product.unit != Unit::Weight) ||
                (product.unit == Unit::Count && product.available % 1000 != 0))
                throw runtime_error("Invalid fields for product " + product.id + ".");
            if (!products.emplace(product.id, &product).second)
                throw runtime_error("Duplicate product ID.");
            if (!barcodes.insert(product.barcode).second)
                throw runtime_error("Duplicate barcode.");
        }

        unordered_set<string> orders;
        unordered_set<string> bills;
        for (const SaleRecord &sale : data.sales)
        {
            const SaleIds &ids = sale.ids;
            if (!validTimestamp(sale.timestamp) ||
                ids.orderId.size() != 8 || !isNumeric(ids.orderId) ||
                ids.orderId.substr(6) != sale.timestamp.date.substr(8) ||
                ids.billId.size() != 9 || !isNumeric(ids.billId))
                throw runtime_error("Invalid date, time or IDs for order " + ids.orderId + ".");
            if (!orders.insert(ids.orderId).second || !bills.insert(ids.billId).second)
                throw runtime_error("Duplicate order or bill ID.");
            if (trim(sale.customer.name).empty() || sale.lines.empty() ||
                (sale.status != OrderStatus::OnHold && sale.status != OrderStatus::Paid &&
                 sale.status != OrderStatus::Archived))
                throw runtime_error("Invalid customer, items or status for order " + ids.orderId + ".");

            const Totals &totals = sale.totals;
            if (totals.subtotal < 0 || totals.tax < 0 || totals.discount < 0 ||
                totals.discount > totals.subtotal)
                throw runtime_error("Invalid totals for order " + ids.orderId + ".");
            const auto net = totals.subtotal - totals.discount;
            if (totals.tax > numeric_limits<int64_t>::max() - net || totals.payable != net + totals.tax)
                throw runtime_error("Inconsistent payable amount for order " + ids.orderId + ".");

            const Payment &payment = sale.payment;
            if ((sale.status == OrderStatus::Paid && !payment.recorded) ||
                (sale.status == OrderStatus::OnHold && payment.recorded) ||
                (!payment.recorded && (payment.amount != 0 || !payment.merchant.empty())) ||
                (payment.recorded && (payment.amount != totals.payable ||
                    (payment.mode != PaymentMode::Cash && payment.mode != PaymentMode::Card &&
                     payment.mode != PaymentMode::Wallet))))
                throw runtime_error("Invalid payment for order " + ids.orderId + ".");

            unordered_set<string> lineProducts;
            for (const SaleLine &line : sale.lines)
            {
                if (!isNumeric(line.productId) || !validBarcode(line.barcode) ||
                    trim(line.acronym).empty() || line.acronym.size() > 8 ||
                    line.quantity <= 0 || line.unitPrice < 0 || line.discountPerUnit < 0 ||
                    line.discountPerUnit > line.unitPrice || !lineProducts.insert(line.productId).second)
                    throw runtime_error("Invalid purchase line for order " + ids.orderId + ".");
                if (sale.status == OrderStatus::OnHold)
                {
                    const auto product = products.find(line.productId);
                    if (product == products.end() || product->second->barcode != line.barcode ||
                        (product->second->unit == Unit::Count && line.quantity % 1000 != 0))
                        throw runtime_error("Held order " + ids.orderId + " does not match the catalogue.");
                }
            }
        }
    }

} // anonymous namespace

// =============================================================================
// PosStorage implementation
// =============================================================================

PosStorage::PosStorage(filesystem::path dataRoot)
    : dataRoot_(move(dataRoot)), loaded_(false)
{
}

Status PosStorage::load()
{
    loaded_ = false;

    try
    {
        if (filesystem::exists(dataRoot_) && !filesystem::is_directory(dataRoot_))
            throw runtime_error("Data path is not a directory: " + dataRoot_.string());

        filesystem::create_directories(dataRoot_);

        const filesystem::path productPath = dataRoot_ / "Product.txt";
        const filesystem::path orderPath = dataRoot_ / "Order.txt";
        const filesystem::path detailPath = dataRoot_ / "OrderDetail.txt";
        const filesystem::path billPath = dataRoot_ / "Bill.txt";

        vector<filesystem::path> paths = {
            productPath,
            orderPath,
            detailPath,
            billPath};

        int existingFiles = 0;

        for (const auto &path : paths)
        {
            if (filesystem::exists(path))
            {
                if (!filesystem::is_regular_file(path))
                    throw runtime_error(path.filename().string() + " is not a regular file.");

                ++existingFiles;
            }
        }

        if (existingFiles == 0)
        {
            data_ = StoreData{};
            loaded_ = true;
            return {true, "New empty store loaded successfully."};
        }

        if (existingFiles != (int)paths.size())
        {
            string message = "Incomplete data files. Missing:";

            for (const auto &path : paths)
            {
                if (!filesystem::exists(path))
                    message += " " + path.filename().string();
            }

            throw runtime_error(message);
        }

        StoreData loadedData;

        unordered_map<string, vector<TempDetail>> details;
        unordered_map<string, size_t> orderIndex;
        unordered_map<string, BillLine> billsByOrderId;
        unordered_set<string> productIds;
        unordered_set<string> barcodes;
        unordered_set<string> billIds;

        string line;
        int lineNumber = 0;

        // 1. Product.txt
        {
            ifstream productStream(productPath);

            if (!productStream.is_open())
                throw runtime_error("Unable to open Product.txt.");

            while (getline(productStream, line))
            {
                ++lineNumber;
                line = storage_helpers::trim(line);

                if (line.empty())
                    continue;

                try
                {
                    vector<Field> fields = removeRecordMarker(line);

                    Product product = deserializeProduct(fields);

                    if (!productIds.insert(product.id).second)
                        throw runtime_error("Duplicate product ID.");

                    if (!barcodes.insert(product.barcode).second)
                        throw runtime_error("Duplicate barcode.");

                    loadedData.products.push_back(move(product));
                }
                catch (const exception &e)
                {
                    throw runtime_error(
                        "Product.txt: Line " +
                        to_string(lineNumber) + ": " + e.what());
                }
            }
            if (productStream.bad() || !productStream.eof())
                throw runtime_error("Error reading Product.txt.");
        }

        // 2. Order.txt
        {
            ifstream orderStream(orderPath);

            if (!orderStream.is_open())
                throw runtime_error("Unable to open Order.txt.");

            lineNumber = 0;

            while (getline(orderStream, line))
            {
                ++lineNumber;
                line = storage_helpers::trim(line);

                if (line.empty())
                    continue;

                try
                {
                    vector<Field> fields = removeRecordMarker(line);

                    SaleRecord sale = deserializeOrder(fields);

                    if (orderIndex.find(sale.ids.orderId) != orderIndex.end())
                        throw runtime_error("Duplicate order ID.");

                    orderIndex[sale.ids.orderId] = loadedData.sales.size();
                    loadedData.sales.push_back(move(sale));
                }
                catch (const exception &e)
                {
                    throw runtime_error(
                        "Order.txt: Line " +
                        to_string(lineNumber) + ": " + e.what());
                }
            }
            if (orderStream.bad() || !orderStream.eof())
                throw runtime_error("Error reading Order.txt.");
        }

        // 3. OrderDetail.txt
        {
            ifstream detailStream(detailPath);

            if (!detailStream.is_open())
                throw runtime_error("Unable to open OrderDetail.txt.");

            lineNumber = 0;

            while (getline(detailStream, line))
            {
                ++lineNumber;
                line = storage_helpers::trim(line);

                if (line.empty())
                    continue;

                try
                {
                    vector<Field> fields = storage_helpers::slices(line);
                    auto [orderId, detail] = deserializeDetail(fields);

                    if (orderIndex.find(orderId) == orderIndex.end())
                        throw runtime_error(
                            "Order ID " + orderId +
                            " does not exist in Order.txt.");

                    details[orderId].push_back(move(detail));
                }
                catch (const exception &e)
                {
                    throw runtime_error(
                        "OrderDetail.txt: Line " +
                        to_string(lineNumber) + ": " + e.what());
                }
            }
            if (detailStream.bad() || !detailStream.eof())
                throw runtime_error("Error reading OrderDetail.txt.");
        }

        // 4. Bill.txt
        {
            ifstream billStream(billPath);

            if (!billStream.is_open())
                throw runtime_error("Unable to open Bill.txt.");

            lineNumber = 0;

            while (getline(billStream, line))
            {
                ++lineNumber;
                line = storage_helpers::trim(line);

                if (line.empty())
                    continue;

                try
                {
                    vector<Field> fields = removeRecordMarker(line);

                    BillLine bill = decodeBill(fields);

                    if (!billIds.insert(bill.ids.billId).second)
                        throw runtime_error("Duplicate bill ID.");

                    if (orderIndex.find(bill.ids.orderId) == orderIndex.end())
                        throw runtime_error(
                            "Order ID " + bill.ids.orderId +
                            " does not exist in Order.txt.");

                    if (billsByOrderId.find(bill.ids.orderId) != billsByOrderId.end())
                        throw runtime_error(
                            "Order " + bill.ids.orderId +
                            " already has a bill.");

                    billsByOrderId.emplace(bill.ids.orderId, move(bill));
                }
                catch (const exception &e)
                {
                    throw runtime_error(
                        "Bill.txt: Line " +
                        to_string(lineNumber) + ": " + e.what());
                }
            }
            if (billStream.bad() || !billStream.eof())
                throw runtime_error("Error reading Bill.txt.");
        }

        // Validate 1-to-1 relationship
        for (const SaleRecord &sale : loadedData.sales)
        {
            if (billsByOrderId.find(sale.ids.orderId) == billsByOrderId.end())
                throw runtime_error(
                    "Order " + sale.ids.orderId + " does not have a bill.");
        }

        // Construct complete SaleRecord objects
        for (SaleRecord &sale : loadedData.sales)
        {
            const string &orderId = sale.ids.orderId;
            const BillLine &bill = billsByOrderId.at(orderId);

            if (sale.customer.name != bill.customerName)
                throw runtime_error(
                    "Customer name mismatch for order " + orderId + ".");

            if (sale.status != bill.status)
                throw runtime_error(
                    "Order status mismatch for order " + orderId + ".");

            auto detailIt = details.find(orderId);

            if (detailIt == details.end())
                throw runtime_error(
                    "No order details found for order " + orderId + ".");

            const vector<TempDetail> &orderDetails = detailIt->second;

            if (orderDetails.size() != bill.items.size())
                throw runtime_error(
                    "Item count mismatch for order " + orderId + ".");

            sale.ids.billId = bill.ids.billId;
            sale.totals = bill.totals;

            for (size_t i = 0; i < bill.items.size(); ++i)
            {
                const TempDetail &detail = orderDetails[i];
                const BillItem &item = bill.items[i];

                if (detail.quantity != item.quantity)
                    throw runtime_error(
                        "Quantity mismatch for order " + orderId +
                        ", item " + to_string(i + 1) + ".");

                SaleLine saleLine;

                saleLine.productId = detail.productId;
                saleLine.barcode = detail.barcode;
                saleLine.acronym = item.acronym;
                saleLine.unitPrice = item.unitPrice;
                saleLine.discountPerUnit = item.discountPerUnit;
                saleLine.quantity = item.quantity;

                sale.lines.push_back(move(saleLine));
            }
        }

        validateSnapshot(loadedData);
        data_ = std::move(loadedData);
        loaded_ = true;

        return {true, "All four files loaded successfully."};
    }
    catch (const exception &e)
    {
        loaded_ = false;
        return {false, e.what()};
    }
}

Result<StoreData> PosStorage::read() const
{
    if (!loaded_)
    {
        return {false, "Storage has not been loaded successfully.", {}};
    }

    return {true, "", data_};
}

Status PosStorage::save(const StoreData &data)
{
    if (!loaded_)
    {
        return {false, "Storage has not been loaded successfully."};
    }

    namespace fs = std::filesystem;
    using namespace storage_helpers;

    vector<string> fileNames = {
        "Product.txt",
        "Order.txt",
        "OrderDetail.txt",
        "Bill.txt"};

    vector<fs::path> paths, tempPaths, backupPaths;
    vector<bool> backedUp(4, false);
    vector<bool> installed(4, false);
    vector<bool> temporary(4, false);

    try
    {
        validateSnapshot(data);
        StoreData nextData = data;
        fs::create_directories(dataRoot_);

        for (const string &fileName : fileNames)
        {
            paths.push_back(dataRoot_ / fileName);
            tempPaths.push_back(dataRoot_ / (fileName + ".tmp"));
            backupPaths.push_back(dataRoot_ / (fileName + ".bak"));
            if (fs::exists(tempPaths.back()) || fs::exists(backupPaths.back()) ||
                (fs::exists(paths.back()) && !fs::is_regular_file(paths.back())))
                throw runtime_error("Unexpected file blocking save of " + fileName + ".");
        }

        vector<string> productRows;

        for (const Product &product : data.products)
            productRows.push_back(serializeProduct(product));

        vector<string> orderRows;
        vector<string> detailRows;

        for (const SaleRecord &sale : data.sales)
        {
            orderRows.push_back(serializeOrder(sale));

            for (const SaleLine &item : sale.lines)
                detailRows.push_back(
                    serializeOrderDetail(sale.ids.orderId, item));
        }

        vector<string> billRows;

        for (const SaleRecord &sale : data.sales)
            billRows.push_back(serializeBill(sale));

        vector<vector<string>> allRows = {
            productRows,
            orderRows,
            detailRows,
            billRows};

        for (size_t i = 0; i < allRows.size(); ++i)
        {
            temporary[i] = true;
            ofstream file(tempPaths[i], ios::binary | ios::trunc);

            if (!file.is_open())
                throw runtime_error(
                    "Unable to create " +
                    tempPaths[i].filename().string());

            for (const string &row : allRows[i])
                file << row << '\n';

            file.close();

            if (!file)
                throw runtime_error(
                    "Error writing " +
                    tempPaths[i].filename().string());
        }

        for (size_t i = 0; i < paths.size(); ++i)
        {
            if (fs::exists(paths[i]))
            {
                fs::rename(paths[i], backupPaths[i]);
                backedUp[i] = true;
            }
        }

        for (size_t i = 0; i < paths.size(); ++i)
        {
            fs::rename(tempPaths[i], paths[i]);
            installed[i] = true;
            temporary[i] = false;
        }

        data_ = std::move(nextData);
        for (size_t i = 0; i < paths.size(); ++i)
        {
            if (backedUp[i])
            {
                error_code ec;
                fs::remove(backupPaths[i], ec);
            }
        }

        return {true, "All four files saved successfully."};
    }
    catch (const exception &e)
    {
        bool restored = true;
        for (size_t i = 0; i < paths.size(); ++i)
        {
            error_code ec;

            if (installed[i])
            {
                fs::remove(paths[i], ec);
                restored = restored && !ec;
            }

            if (backedUp[i])
            {
                ec.clear();
                fs::rename(backupPaths[i], paths[i], ec);
                restored = restored && !ec;
            }

            if (temporary[i])
            {
                ec.clear();
                fs::remove(tempPaths[i], ec);
            }
        }

        if (!restored)
        {
            loaded_ = false;
            return {false, std::string(e.what()) + " Rollback failed. Reload and validate storage before saving."};
        }
        return {false, e.what()};
    }
}
