#include "CartService.h"
#include "PricingCalculator.h"
#include "ProductService.h"

#include <algorithm>
#include <iomanip>
#include <sstream>

using namespace std;

namespace {

// Formats quantity from internal thousandths into user-friendly display units
// Count: integer string (e.g. 4000 -> "4")

// decimal kg string without trailing zeros (e.g. 2500 -> "2.5")

string formatQuantity(Unit unit, int64_t qtyThousandths) {
    if (unit == Unit::Count) {
        return std::to_string(qtyThousandths / 1000);
    }

    double kg = static_cast<double>(qtyThousandths) / 1000.0;
    ostringstream ss;
    ss << std::fixed << std::setprecision(3) << kg;
    string s = ss.str();

    // Trim trailing zeros and decimal point
    s.erase(s.find_last_not_of('0') + 1, std::string::npos);
    if (!s.empty() && s.back() == '.') {
        s.pop_back();
    }
    return s;
}

// Formats the required insufficient stock error message
// "Insufficient stock: X already in order + Y requested exceeds Z available."

string makeInsufficientStockMessage(Unit unit, int64_t inCart, int64_t requested, int64_t available) {
    
    ostringstream ss;
    ss << "Insufficient stock: "
       << formatQuantity(unit, inCart) << " already in order + "
       << formatQuantity(unit, requested) << " requested exceeds "
       << formatQuantity(unit, available) << " available.";
    return ss.str();
}

} // namespace

CartService::CartService(ProductService& products, const PricingCalculator& pricing)
    : produc ts_(products),
      pricing_(pricing),
      items_({})
{
}

Status CartService::addByBarcode(const std::string& barcode, int64_t addition) {
 
    if (barcode.empty()) {
        return {false, "Barcode cannot be empty."};
    }

    if (addition <= 0) {
        return {false, "Quantity addition must be positive."};
    }

    // Look up live product from catalog
    Result<Product> prodRes = products_.findByBarcode(barcode);

    if (!prodRes.success) {
        return {false, prodRes.message.empty() ? "Product not found in catalogue." : prodRes.message};
    }
    const Product& liveProduct = prodRes.value;

    // Validate measurement unit rule for Count items
    if (liveProduct.unit == Unit::Count && (addition % 1000 != 0)) {
        return {false, "Count item quantity must be an integer multiple of 1000."};
    }

    // Check if product already exists in current cart
    CartItem* existing = nullptr;

    for (CartItem& item : items_) {

        if (item.product.id == liveProduct.id) {
            existing = &item;
            break;
        }
    }

    if (existing != nullptr) {
        // Block positive quantity changes if unit has changed
        if (existing->product.unit != liveProduct.unit) {
            return {false, "Product measurement unit has changed. Restore the original unit or remove this item from the order."};
        }

        const int64_t combined = existing->quantity + addition;

        // Check stock: required quantity = existing row quantity + addition
        if (!products_.hasStock(liveProduct, combined)) {
            return {false, makeInsufficientStockMessage(liveProduct.unit, existing->quantity, addition, liveProduct.available)};
        }

        existing->quantity = combined;
        return {true, "Cart item quantity updated."};
    }

    // New cart row: check stock for addition
    if (!products_.hasStock(liveProduct, addition)) {
        return {false, makeInsufficientStockMessage(liveProduct.unit, 0, addition, liveProduct.available)};
    }

    // Capture unit and pricing snapshot into new CartItem
    CartItem newItem;
    newItem.product = liveProduct;
    newItem.quantity = addition;
    items_.push_back(newItem);

    return {true, "Item added to cart."};
}

Status CartService::setQuantity(const string& productId, int64_t quantity) {
    if (productId.empty()) {
        return {false, "Product ID cannot be empty."};
    }

    CartItem* existing = nullptr;
    size_t foundIndex = 0;
    for (size_t i = 0; i < items_.size(); ++i) {

        if (items_[i].product.id == productId) {
            existing = &items_[i];
            foundIndex = i;
            break;
        }
    }

    if (existing == nullptr) {
        return {false, "Product not found in cart."};
    }

    // Setting quantity to zero removes the row even if deleted from catalog or unit changed 
    if (quantity == 0) {

        items_.erase(items_.begin() + foundIndex);
        return {true, "Item removed from cart."};
    }

    if (quantity < 0) {
        return {false, "Quantity cannot be negative."};
    }

    // For positive quantity changes, check live catalog product
    Result<Product> prodRes = products_.findById(productId);
    if (!prodRes.success) {
        return {false, prodRes.message.empty() ? "Product no longer available in catalogue." : prodRes.message};
    }
    const Product& liveProduct = prodRes.value;

    // Unit mismatch check
    if (existing->product.unit != liveProduct.unit) {
        return {false, "Product measurement unit has changed. Restore the original unit or remove this item from the order."};
    }

    if (liveProduct.unit == Unit::Count && (quantity % 1000 != 0)) {
        return {false, "Count item quantity must be an integer multiple of 1000."};
    }

    // Check live stock for absolute requested quantity
    if (!products_.hasStock(liveProduct, quantity)) {
        return {false, makeInsufficientStockMessage(liveProduct.unit, 0, quantity, liveProduct.available)};
    }

    existing->quantity = quantity;
    return {true, "Item quantity updated."};
}

std::vector<CartItem> CartService::items() const {

    return items_;
}

Result<Totals> CartService::totals() const {

    return pricing_.calculate(items_);
}

Result<Totals> CartService::lineTotals(const string& productId) const {

    const CartItem* existing = nullptr;
    for (const CartItem& item : items_) {

        if (item.product.id == productId) {
            existing = &item;
            break;
        }
    }

    if (existing == nullptr) {
        return {false, "Product not found in cart.", {}};
    }

    // Delegate row calculation to shared PricingCalculator via one-item vector

    const vector<CartItem> singleRow = { *existing };
    return pricing_.calculate(singleRow);
}

void CartService::clear() {

    items_.clear();
}