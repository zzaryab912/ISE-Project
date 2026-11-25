#pragma once

#include "PosTypes.h"

using namespace std;

class PricingCalculator {
public:
    PricingCalculator() = default;

    Result<PricingResult> calculate(const vector<SaleLine>& lines,
                                    const PricingTerms& terms) const;
};
