#include "PricingCalculator.h"

#include <algorithm>
#include <limits>
#include <numeric>

using namespace std;

namespace {

constexpr int64_t maxAmount = numeric_limits<int64_t>::max();
constexpr int32_t maxBasisPoints = 10000;
constexpr int32_t maxSpecialBasisPoints = 800;

struct UInt128 {
    uint64_t hi = 0;
    uint64_t lo = 0;
};

UInt128 multiply64(uint64_t a, uint64_t b)
{
    const uint64_t aLo = a & 0xffffffffULL;
    const uint64_t aHi = a >> 32;
    const uint64_t bLo = b & 0xffffffffULL;
    const uint64_t bHi = b >> 32;

    const uint64_t p0 = aLo * bLo;
    const uint64_t p1 = aLo * bHi;
    const uint64_t p2 = aHi * bLo;
    const uint64_t p3 = aHi * bHi;

    const uint64_t mid = (p0 >> 32) + (p1 & 0xffffffffULL) + (p2 & 0xffffffffULL);
    const uint64_t lo = (p0 & 0xffffffffULL) | (mid << 32);
    const uint64_t hi = p3 + (p1 >> 32) + (p2 >> 32) + (mid >> 32);
    return {hi, lo};
}

UInt128 add64(UInt128 value, uint64_t addend)
{
    value.lo += addend;
    if (value.lo < addend) {
        ++value.hi;
    }
    return value;
}

// Compute (a * b + roundingBias) / divisor and its exact remainder in 128-bit
// arithmetic so large intermediate products never trigger false overflows.
bool mulDivModInt64(int64_t a, int64_t b, int64_t divisor, int64_t roundingBias,
                    int64_t& quotient, int64_t& remainder)
{
    if (a < 0 || b < 0 || divisor <= 0 || roundingBias < 0) {
        return false;
    }

    UInt128 num = multiply64(static_cast<uint64_t>(a), static_cast<uint64_t>(b));
    if (roundingBias > 0) {
        num = add64(num, static_cast<uint64_t>(roundingBias));
    }

    const uint64_t div = static_cast<uint64_t>(divisor);
    uint64_t qHi = 0;
    uint64_t qLo = 0;
    uint64_t rem = 0;

    for (int bit = 127; bit >= 0; --bit) {
        const uint64_t nextBit = (bit >= 64)
            ? ((num.hi >> (bit - 64)) & 1ULL)
            : ((num.lo >> bit) & 1ULL);
        rem = (rem << 1) | nextBit;
        if (rem >= div) {
            rem -= div;
            if (bit >= 64) {
                qHi |= (1ULL << (bit - 64));
            } else {
                qLo |= (1ULL << bit);
            }
        }
    }

    if (qHi != 0 || qLo > static_cast<uint64_t>(maxAmount)) {
        return false;
    }

    quotient = static_cast<int64_t>(qLo);
    remainder = static_cast<int64_t>(rem);
    return true;
}

bool roundedThousandths(int64_t ratePerUnit, int64_t quantity, int64_t& amount)
{
    int64_t rem = 0;
    return mulDivModInt64(ratePerUnit, quantity, 1000, 500, amount, rem);
}

bool roundedPercent(int64_t amount, int32_t basisPoints, int64_t& result)
{
    int64_t rem = 0;
    return mulDivModInt64(amount, basisPoints, maxBasisPoints, maxBasisPoints / 2, result, rem);
}

} // namespace

Result<PricingResult> PricingCalculator::calculate(const vector<SaleLine>& lines,
                                                   const PricingTerms& terms) const
{
    if (terms.seasonalRate < 0 || terms.seasonalRate > maxBasisPoints ||
        terms.specialRate < 0 || terms.specialRate > maxSpecialBasisPoints) {
        return {false, "Invalid seasonal or special discount rate.", {}};
    }

    PricingResult result;
    result.lines = lines;
    result.lineSubtotals.reserve(lines.size());

    // Trim only the special rate if combined bill promotions exceed 100%.
    int32_t effSeasonal = terms.seasonalRate;
    int32_t effSpecial = terms.specialRate;
    int32_t combinedRate = effSeasonal + effSpecial;
    if (combinedRate > maxBasisPoints) {
        effSpecial = maxBasisPoints - effSeasonal;
        combinedRate = maxBasisPoints;
    }
    result.terms = {effSeasonal, effSpecial};

    vector<int64_t> lineBases(lines.size(), 0);
    int64_t baseTotal = 0;

    for (size_t i = 0; i < result.lines.size(); ++i) {
        SaleLine& line = result.lines[i];
        line.lineProductDiscount = 0;
        line.lineTax = 0;

        if (line.quantity < 0 || line.unitPrice < 0 ||
            line.discountPerUnit < 0 || line.discountPerUnit > line.unitPrice) {
            return {false, "Invalid quantity, price or product discount.", {}};
        }
        if (line.taxRate < 0 || line.taxRate > maxBasisPoints) {
            return {false, "Invalid tax rate.", {}};
        }
        if ((line.unit != Unit::Count && line.unit != Unit::Weight) ||
            (line.unit == Unit::Count && line.quantity % 1000 != 0)) {
            return {false, "Invalid measurement unit or fractional Count quantity.", {}};
        }

        int64_t lineGross = 0;
        int64_t lineProductDisc = 0;
        if (!roundedThousandths(line.unitPrice, line.quantity, lineGross) ||
            !roundedThousandths(line.discountPerUnit, line.quantity, lineProductDisc) ||
            lineGross > maxAmount - result.totals.subtotal ||
            lineProductDisc > maxAmount - result.totals.productDiscount) {
            return {false, "The bill amounts exceed the supported range.", {}};
        }

        result.lineSubtotals.push_back(lineGross);
        line.lineProductDiscount = lineProductDisc;
        lineBases[i] = lineGross - lineProductDisc;

        result.totals.subtotal += lineGross;
        result.totals.productDiscount += lineProductDisc;
        baseTotal += lineBases[i];
    }

    int64_t combinedDiscount = 0;
    if (baseTotal > 0 && combinedRate > 0) {
        if (!roundedPercent(baseTotal, combinedRate, combinedDiscount)) {
            return {false, "The bill amounts exceed the supported range.", {}};
        }

        if (combinedDiscount > 0) {
            int64_t seasonalRem = 0;
            int64_t specialRem = 0;
            if (!mulDivModInt64(combinedDiscount, effSeasonal, combinedRate, 0,
                                result.totals.seasonalDiscount, seasonalRem) ||
                !mulDivModInt64(combinedDiscount, effSpecial, combinedRate, 0,
                                result.totals.specialDiscount, specialRem)) {
                return {false, "The bill amounts exceed the supported range.", {}};
            }

            const int64_t splitRemaining =
                combinedDiscount - result.totals.seasonalDiscount - result.totals.specialDiscount;
            if (splitRemaining > 0) {
                // Give ties to the seasonal portion per spec.
                if (seasonalRem >= specialRem) {
                    result.totals.seasonalDiscount += splitRemaining;
                } else {
                    result.totals.specialDiscount += splitRemaining;
                }
            }
        }
    }

    // Allocate the combined bill discount across lines proportionally to their bases.
    vector<int64_t> allocatedBillDiscounts(result.lines.size(), 0);
    if (baseTotal > 0 && combinedDiscount > 0) {
        vector<int64_t> remainders(result.lines.size(), 0);
        int64_t allocatedSum = 0;

        for (size_t i = 0; i < result.lines.size(); ++i) {
            if (!mulDivModInt64(combinedDiscount, lineBases[i], baseTotal, 0,
                                allocatedBillDiscounts[i], remainders[i])) {
                return {false, "The bill amounts exceed the supported range.", {}};
            }
            allocatedSum += allocatedBillDiscounts[i];
        }

        const int64_t leftover = combinedDiscount - allocatedSum;
        if (leftover > 0) {
            vector<size_t> order(result.lines.size());
            iota(order.begin(), order.end(), 0);
            stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) {
                return remainders[a] > remainders[b];
            });
            for (int64_t k = 0; k < leftover; ++k) {
                ++allocatedBillDiscounts[order[static_cast<size_t>(k)]];
            }
        }
    }

    for (size_t i = 0; i < result.lines.size(); ++i) {
        const int64_t discountedLine = lineBases[i] - allocatedBillDiscounts[i];
        int64_t lineTax = 0;
        if (!roundedPercent(discountedLine, result.lines[i].taxRate, lineTax) ||
            lineTax > maxAmount - result.totals.tax) {
            return {false, "The bill amounts exceed the supported range.", {}};
        }
        result.lines[i].lineTax = lineTax;
        result.totals.tax += lineTax;
    }

    const int64_t netMerchandise = baseTotal - combinedDiscount;
    if (result.totals.tax > maxAmount - netMerchandise) {
        return {false, "The payable amount exceeds the supported range.", {}};
    }
    const int64_t exactPayable = netMerchandise + result.totals.tax;

    const int64_t paiseRemainder = exactPayable % 100;
    int64_t roundingAdjustment = 0;
    if (paiseRemainder >= 50) {
        roundingAdjustment = 100 - paiseRemainder;
        if (roundingAdjustment > maxAmount - exactPayable) {
            return {false, "The payable amount exceeds the supported range.", {}};
        }
    } else if (paiseRemainder > 0) {
        roundingAdjustment = -paiseRemainder;
    }

    result.totals.roundingAdjustment = roundingAdjustment;
    result.totals.payable = exactPayable + roundingAdjustment;
    return {true, {}, result};
}
