
#ifndef POSSTORAGE_H
#define POSSTORAGE_H

#include "IPosStore.h"
#include <filesystem>

class PosStorage : public IPosStore {
private:
    std::filesystem::path dataRoot_;
    StoreData data_;
    bool loaded_ = false;

public:
    PosStorage(std::filesystem::path dataRoot);

    Status load();

    Result<StoreData> read() const override;

    Status save(const StoreData& data) override;

    ~PosStorage() {}
};

#endif
