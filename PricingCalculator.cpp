#include "PricingCalculator.h"

#include <limits>

namespace {

constexpr auto maxAmount = std::numeric_limits<std::int64_t>::max();

// Split the fixed-point multiplication so a representable result does not
// fail just because the intermediate product exceeds the integer range.
bool roundedPaisa(std::int64_t rate, std::int64_t quantity, std::int64_t& amount)
{
    const auto whole = quantity / 1000;
    const auto remainder = quantity % 1000;
    const auto fraction = (rate / 1000) * remainder +
        ((rate % 1000) * remainder + 500) / 1000;
    if (whole != 0 && rate > (maxAmount - fraction) / whole) {
        return false;
    }
    amount = rate * whole + fraction;
    return true;
}

} // namespace

Result<Totals> PricingCalculator::calculate(const std::vector<CartItem>& items) const
{
    Totals totals;
    for (const CartItem& item : items) {
        const Product& product = item.product;
        if (item.quantity < 0 || product.unitPrice < 0 || product.taxPerUnit < 0 ||
            product.discountPerUnit < 0 || product.discountPerUnit > product.unitPrice) {
            return {false, "Invalid quantity, price, tax or discount.", {}};
        }
        if ((product.unit != Unit::Count && product.unit != Unit::Weight) ||
            (product.unit == Unit::Count && item.quantity % 1000 != 0)) {
            return {false, "Invalid measurement unit or fractional Count quantity.", {}};
        }

        Totals line;
        if (!roundedPaisa(product.unitPrice, item.quantity, line.subtotal) ||
            !roundedPaisa(product.taxPerUnit, item.quantity, line.tax) ||
            !roundedPaisa(product.discountPerUnit, item.quantity, line.discount) ||
            line.subtotal > maxAmount - totals.subtotal ||
            line.tax > maxAmount - totals.tax ||
            line.discount > maxAmount - totals.discount) {
            return {false, "The bill amounts exceed the supported range.", {}};
        }
        totals.subtotal += line.subtotal;
        totals.tax += line.tax;
        totals.discount += line.discount;
    }

    const auto net = totals.subtotal - totals.discount;
    if (totals.tax > maxAmount - net) {
        return {false, "The payable amount exceeds the supported range.", {}};
    }
    totals.payable = net + totals.tax;
    return {true, {}, totals};
}
