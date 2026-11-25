
#ifndef POSSTORAGE_H
#define POSSTORAGE_H

#include "IPosStore.h"
#include <filesystem>

class PosStorage : public IPosStore
{
private:
    std::filesystem::path dataRoot_;
    StoreData data_;
    bool loaded_ = false;

public:
    explicit PosStorage(std::filesystem::path dataRoot);

    Status load();

    Status importIncrement2Catalogue(const std::filesystem::path& sourceRoot);

    Result<StoreData> read() const override;

    Status save(const StoreData& data) override;
};

#endif
