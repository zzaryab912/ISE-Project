#ifndef IPOSTORE_H
#define IPOSTORE_H

#pragma once

#include "PosTypes.h"

class IPosStore {
public:
    virtual Result<StoreData> read() const = 0;

    virtual Status save(const StoreData& data) = 0;

    virtual ~IPosStore() = default;
};

#endif