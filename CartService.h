#include "PosTypes.h"
#include <string>
#include <vector>

class ProductService;
class PricingCalculator;

class CartService{

public:

  CartService(ProductService& products, const PricingCalculator& pricing);

  Status AddByBarcode(const std::string& barcode, std::int64_t addition);

  Status setQuantity(const std::string& productId, std::int64_t quantity);

  std::vector<CartItem> items();

  //totals struct includes tax, discount, subtotal and payable (in paisas)

  Result<Totals> totals();

  Result<Totals> lineTotals(const std::string& productId);

  void clear();

}