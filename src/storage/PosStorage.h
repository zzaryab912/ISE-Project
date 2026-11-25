#ifndef POSSTORAGE_H
#define POSSTORAGE_H

#include "IPosStore.h"
#include <filesystem>

using namespace std;

class PosStorage : public IPosStore {
private:
    filesystem::path dataRoot_;
    StoreData data_;
    bool loaded_ = false;

public:
    PosStorage(filesystem::path dataRoot);

    Status load();

    Status importIncrement2Catalogue(const filesystem::path& sourceRoot);

    Result<StoreData> read() const override;

    Status save(const StoreData& data) override;

    ~PosStorage() override = default;
};

#endif
