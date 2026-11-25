#pragma once

#include "PosTypes.h"

using namespace std;

class IPosStore;

class DiscountService {
private:
    IPosStore& store_;

public:
    DiscountService(IPosStore& store);

    Result<vector<Season>> listSeasons() const;
    Result<SpecialDiscount> getSpecial() const;
    Status saveSeason(const optional<Season>& original, const Season& replacement);
    Status saveSpecial(const SpecialDiscount& special);
    PricingTerms termsFor(const StoreData& data, const Timestamp& now) const;
};
