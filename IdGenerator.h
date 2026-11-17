
#ifndef IDGENERATOR_H
#define IDGENERATOR_H

#include "PosTypes.h"

class IdGenerator {
public:
    IdGenerator() = default;

    Result<std::string> productId(const StoreData& data) const;

    Result<SaleIds> saleIds(
        const StoreData& data,
        const Timestamp& timestamp
    ) const;
};

#endif
