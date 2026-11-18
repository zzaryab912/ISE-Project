#include "PricingCalculator.h"
#include <limits>
#include <sstream>

using namespace std;

namespace{


    bool multiplyAndRoundPaisa(int64_t rate, int64_t qty, int64_t& out){


        if (rate < 0 || qty < 0) 
        return false;
    
    if (rate == 0 || qty == 0) {
        out = 0;
        return true;
    }

    constexpr int64_t kMax = numeric_limits<int64_t>::max();

    if(rate > (kMax - 500) / qty)
      return false;

      out = (rate * qty + 500) / 1000;
      return true;

    }
}


Result<Totals> PricingCalculator::calculate(const vector<CartItem>& items) const{

    if(items.empty()){
       return {true, "Cart is empty.", Totals{0,0,0,0}};
    }

       int64_t sumSubtotal = 0;
       int64_t sumTax = 0;
       int64_t sumDiscount = 0;

       for(size_t i = 0; i < items.size(); i++){

        const CartItem& item = items[i];
        const Product& prod = item.product;
        const int64_t qty = item.quantity;
         
         // Validation: rates and quantity must be non-negative
        if (qty < 0 || prod.unitPrice < 0 || prod.taxPerUnit < 0 || prod.discountPerUnit < 0) {
            return {false, "Negative rate or quantity encountered.", {}};
        }

         // Unit discount cannot exceed unit price
        if (prod.discountPerUnit > prod.unitPrice) {
            return {false, "Unit discount cannot exceed unit price.", {}};
        }
        
         // Count items must have quantities as integer multiples of 1000
        if (prod.unit == Unit::Count && (qty % 1000 != 0)) {
            return {false, "Count item quantity must be an integer multiple of 1000.", {}};
        }

        // Line subtotal (base price before tax and discount)
        int64_t lineSubtotal = 0;

        if (!multiplyAndRoundPaisa(prod.unitPrice, qty, lineSubtotal)) {
            return {false, "Arithmetic overflow during subtotal calculation.", {}};
        }

        // Line tax (tax is based on full quantity before discount)
        int64_t lineTax = 0;

        if (!multiplyAndRoundPaisa(prod.taxPerUnit, qty, lineTax)) {
            return {false, "Arithmetic overflow during tax calculation.", {}};
        }

         // Line discount
        int64_t lineDiscount = 0;

        if (!multiplyAndRoundPaisa(prod.discountPerUnit, qty, lineDiscount)) {
            return {false, "Arithmetic overflow during discount calculation.", {}};
        }

        // Check for sum overflow
        constexpr int64_t kMax = numeric_limits<int64_t>::max();
        if (sumSubtotal > kMax - lineSubtotal || sumTax > kMax - lineTax || sumDiscount > kMax - lineDiscount) {
            return {false, "Arithmetic overflow during totals summation.", {}};
        }

        sumSubtotal += lineSubtotal;
        sumTax += lineTax;
        sumDiscount += lineDiscount;
    }
       
    // Payable = subtotal + tax - discount
    int64_t payable = sumSubtotal + sumTax - sumDiscount;
    if (payable < 0) {
        payable = 0;
    }

    Totals resultTotals;
    resultTotals.subtotal = sumSubtotal;
    resultTotals.tax = sumTax;
    resultTotals.discount = sumDiscount;
    resultTotals.payable = payable;

    return {true, "Totals calculated successfully.", resultTotals};
}