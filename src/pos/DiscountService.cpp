#include "DiscountService.h"

#include "IPosStore.h"

#include <algorithm>
#include <string_view>
#include <tuple>

using namespace std;

namespace {

constexpr int32_t maxSeasonalRate = 10000; // 100.00% in basis points
constexpr int32_t maxSpecialRate = 800;    // 8.00% in basis points

int decimalField(string_view field)
{
    int value = 0;
    for (char ch : field) {
        if (ch < '0' || ch > '9') {
            return -1;
        }
        value = value * 10 + (ch - '0');
    }
    return value;
}

bool validDate(string_view date)
{
    if (date.size() != 10 || date[4] != '-' || date[7] != '-') {
        return false;
    }

    const int year = decimalField(date.substr(0, 4));
    const int month = decimalField(date.substr(5, 2));
    const int day = decimalField(date.substr(8, 2));
    if (year < 1 || month < 1 || month > 12 || day < 1) {
        return false;
    }

    constexpr int monthLengths[] = {31, 28, 31, 30, 31, 30,
                                    31, 31, 30, 31, 30, 31};
    const bool leapYear = year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
    const int lastDay = monthLengths[month - 1] + (month == 2 && leapYear ? 1 : 0);
    return day <= lastDay;
}

Status validateSeason(const Season& season)
{
    if (!validDate(season.startDate) || !validDate(season.endDate)) {
        return {false, "Enter valid seasonal start and end dates in YYYY-MM-DD format."};
    }
    if (season.startDate > season.endDate) {
        return {false, "Season start date cannot be after the end date."};
    }
    if (season.rate < 0 || season.rate > maxSeasonalRate) {
        return {false, "Seasonal discount rate must be between 0.00% and 100.00%."};
    }
    return {true, {}};
}

auto seasonFields(const Season& season)
{
    return tie(season.startDate, season.endDate, season.rate, season.active);
}

} // namespace

DiscountService::DiscountService(IPosStore& store)
    : store_(store)
{
}

Result<vector<Season>> DiscountService::listSeasons() const
{
    const Result<StoreData> storeResult = store_.read();
    if (!storeResult.success) {
        return {false,
                storeResult.message.empty() ? "Failed to read seasonal promotions from storage."
                                            : storeResult.message,
                {}};
    }
    return {true, {}, storeResult.value.seasons};
}

Result<SpecialDiscount> DiscountService::getSpecial() const
{
    const Result<StoreData> storeResult = store_.read();
    if (!storeResult.success) {
        return {false,
                storeResult.message.empty() ? "Failed to read special discount from storage."
                                            : storeResult.message,
                {}};
    }
    return {true, {}, storeResult.value.specialDiscount};
}

Status DiscountService::saveSeason(const optional<Season>& original, const Season& replacement)
{
    const Status valid = validateSeason(replacement);
    if (!valid.success) {
        return valid;
    }

    Result<StoreData> storeResult = store_.read();
    if (!storeResult.success) {
        return {false,
                storeResult.message.empty() ? "Failed to read discounts from storage."
                                            : storeResult.message};
    }
    StoreData& data = storeResult.value;

    optional<size_t> targetIndex;
    if (original.has_value()) {
        for (size_t i = 0; i < data.seasons.size(); ++i) {
            if (seasonFields(data.seasons[i]) == seasonFields(*original)) {
                targetIndex = i;
                break;
            }
        }
        if (!targetIndex.has_value()) {
            const bool sameDatesExist = any_of(data.seasons.begin(), data.seasons.end(),
                [&](const Season& season) {
                    return season.startDate == original->startDate &&
                           season.endDate == original->endDate;
                });
            return {false, sameDatesExist
                               ? "The seasonal promotion changed. Reload it before saving edits."
                               : "The seasonal promotion no longer exists."};
        }
    }

    for (size_t i = 0; i < data.seasons.size(); ++i) {
        if (targetIndex.has_value() && i == *targetIndex) {
            continue;
        }
        const Season& season = data.seasons[i];
        if (season.startDate == replacement.startDate && season.endDate == replacement.endDate) {
            return {false, "A seasonal promotion with those start and end dates already exists."};
        }
        if (replacement.active && season.active) {
            return {false, "Disable the currently enabled seasonal promotion before enabling another."};
        }
    }

    if (targetIndex.has_value()) {
        data.seasons[*targetIndex] = replacement;
    } else {
        data.seasons.push_back(replacement);
    }

    const Status saved = store_.save(data);
    if (!saved.success) {
        return {false, saved.message.empty() ? "Failed to save seasonal promotion." : saved.message};
    }
    return {true, {}};
}

Status DiscountService::saveSpecial(const SpecialDiscount& special)
{
    if (special.rate < 0 || special.rate > maxSpecialRate) {
        return {false, "Special discount rate must be between 0.00% and 8.00%."};
    }

    Result<StoreData> storeResult = store_.read();
    if (!storeResult.success) {
        return {false,
                storeResult.message.empty() ? "Failed to read discounts from storage."
                                            : storeResult.message};
    }
    StoreData& data = storeResult.value;
    data.specialDiscount = special;

    const Status saved = store_.save(data);
    if (!saved.success) {
        return {false, saved.message.empty() ? "Failed to save special discount." : saved.message};
    }
    return {true, {}};
}

PricingTerms DiscountService::termsFor(const StoreData& data, const Timestamp& now) const
{
    PricingTerms terms;

    if (validDate(now.date)) {
        for (const Season& season : data.seasons) {
            if (!season.active) {
                continue;
            }
            if (!validDate(season.startDate) || !validDate(season.endDate) ||
                season.startDate > season.endDate) {
                continue;
            }
            if (season.startDate <= now.date && now.date <= season.endDate) {
                terms.seasonalRate = season.rate;
                break;
            }
        }
    }

    if (data.specialDiscount.active) {
        terms.specialRate = data.specialDiscount.rate;
    }

    return terms;
}
