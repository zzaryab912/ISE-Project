#pragma once
#include "PosTypes.h"
#include <string>
#include <vector>

class ProductService;
class PricingCalculator;

class CartService{


    ProductService& products_;
    const PricingCalculator& pricing_;
    std::vector<CartItem> items_;
    std::optional<HeldCheckout> held_;

public:

  CartService(ProductService& products, const PricingCalculator& pricing);

  Status addByBarcode(const std::string& barcode, std::int64_t addition);

  Status setQuantity(const std::string& productId, std::int64_t quantity);

  std::vector<CartItem> items() const;

  //totals struct includes tax, discount, subtotal and payable (in paisas)

  Result<Totals> totals() const;

  Result<Totals> lineTotals(const std::string& productId) const;

  void restoreHeldItems(const HeldCheckout& held);
  void clear();

};