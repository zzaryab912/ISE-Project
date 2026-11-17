#include "PosTypes.h"
#include <vector>

class PricingCalculator{


public:

    PricingCalculator() = default;

    Result<Totals> calculate(const std::vector<CartItem>& items);
    
}