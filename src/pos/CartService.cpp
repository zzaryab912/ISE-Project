#include "CartService.h"

#include "ProductService.h"

#include <iomanip>
#include <limits>
#include <sstream>

using namespace std;

namespace {

// Format thousandth quantities without trailing zeros (e.g., 4000 -> "4", 2500 -> "2.5").
string formatQuantity(Unit unit, int64_t qtyThousandths)
{
    if (unit == Unit::Count) {
        return to_string(qtyThousandths / 1000);
    }

    ostringstream ss;
    ss << qtyThousandths / 1000 << '.' << setfill('0') << setw(3) << qtyThousandths % 1000;
    string text = ss.str();

    text.erase(text.find_last_not_of('0') + 1, string::npos);
    if (!text.empty() && text.back() == '.') {
        text.pop_back();
    }
    return text;
}

string makeInsufficientStockMessage(Unit unit, int64_t inCart, int64_t requested, int64_t available)
{
    const int64_t displayAvailable = available < 0 ? 0 : available;
    ostringstream ss;
    ss << "Insufficient stock: "
       << formatQuantity(unit, inCart) << " already in order + "
       << formatQuantity(unit, requested) << " requested exceeds "
       << formatQuantity(unit, displayAvailable) << " available.";
    return ss.str();
}

string activeHeldOrderId(const optional<HeldCheckout>& held)
{
    return held.has_value() ? held->sale.ids.orderId : string{};
}

} // namespace

CartService::CartService(ProductService& products)
    : products_(products)
{
}

Status CartService::addByBarcode(const string& barcode, int64_t addition)
{
    if (barcode.empty()) {
        return {false, "Barcode cannot be empty."};
    }

    if (addition <= 0) {
        return {false, "Quantity addition must be positive."};
    }

    const Result<ProductStock> stockRes =
        products_.findWithStockByBarcode(barcode, activeHeldOrderId(held_));
    if (!stockRes.success) {
        return {false, stockRes.message.empty() ? "Product not found in catalogue." : stockRes.message};
    }

    const Product& liveProduct = stockRes.value.product;
    const StockPosition& stock = stockRes.value.stock;

    if (liveProduct.unit != Unit::Count && liveProduct.unit != Unit::Weight) {
        return {false, "Invalid product measurement unit."};
    }
    if (liveProduct.unit == Unit::Count && (addition % 1000 != 0)) {
        return {false, "Count item quantity must be an integer multiple of 1000."};
    }

    CartItem* existing = nullptr;
    for (CartItem& item : items_) {
        if (item.product.id == liveProduct.id) {
            existing = &item;
            break;
        }
    }

    if (existing != nullptr) {
        if (existing->product.barcode != liveProduct.barcode) {
            return {false, "The catalogue product changed. Remove the old cart row before adding it."};
        }
        if (existing->product.unit != liveProduct.unit) {
            return {false, "Product measurement unit has changed. Restore the original unit or remove this item from the order."};
        }
        if (addition > numeric_limits<int64_t>::max() - existing->quantity) {
            return {false, makeInsufficientStockMessage(liveProduct.unit, existing->quantity,
                                                        addition, stock.available)};
        }

        const int64_t combined = existing->quantity + addition;
        if (combined > stock.available) {
            return {false, makeInsufficientStockMessage(liveProduct.unit, existing->quantity,
                                                        addition, stock.available)};
        }

        existing->quantity = combined;
        return {true, "Cart item quantity updated."};
    }

    if (addition > stock.available) {
        return {false, makeInsufficientStockMessage(liveProduct.unit, 0, addition, stock.available)};
    }

    CartItem newItem;
    newItem.product = liveProduct;
    newItem.quantity = addition;
    items_.push_back(newItem);
    return {true, "Item added to cart."};
}

Status CartService::setQuantity(const string& productId, int64_t quantity)
{
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

    // Zero removes the row even if the product was deleted or its unit changed.
    if (quantity == 0) {
        items_.erase(items_.begin() + static_cast<ptrdiff_t>(foundIndex));
        return {true, "Item removed from cart."};
    }

    if (quantity < 0) {
        return {false, "Quantity cannot be negative."};
    }

    const Result<ProductStock> stockRes =
        products_.findWithStock(productId, activeHeldOrderId(held_));
    if (!stockRes.success) {
        return {false, stockRes.message.empty() ? "Product no longer available in catalogue." : stockRes.message};
    }

    const Product& liveProduct = stockRes.value.product;
    const StockPosition& stock = stockRes.value.stock;

    if (existing->product.barcode != liveProduct.barcode) {
        return {false, "The catalogue product changed. Remove the old cart row before editing it."};
    }
    if (existing->product.unit != liveProduct.unit) {
        return {false, "Product measurement unit has changed. Restore the original unit or remove this item from the order."};
    }
    if (liveProduct.unit != Unit::Count && liveProduct.unit != Unit::Weight) {
        return {false, "Invalid product measurement unit."};
    }
    if (liveProduct.unit == Unit::Count && (quantity % 1000 != 0)) {
        return {false, "Count item quantity must be an integer multiple of 1000."};
    }
    if (quantity > stock.available) {
        return {false, makeInsufficientStockMessage(liveProduct.unit, 0, quantity, stock.available)};
    }

    existing->quantity = quantity;
    return {true, "Item quantity updated."};
}

vector<CartItem> CartService::items() const
{
    return items_;
}

void CartService::restoreHeldItems(const HeldCheckout& held)
{
    items_ = held.items;
    held_ = held;
}

void CartService::clear()
{
    held_.reset();
    items_.clear();
}
