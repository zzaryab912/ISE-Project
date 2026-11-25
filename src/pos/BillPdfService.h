#pragma once

#include "PosTypes.h"

#include <filesystem>

using namespace std;

class BillPdfService {
public:
    explicit BillPdfService(filesystem::path dataRoot);

    Result<string> write(const SaleRecord& sale) const;
    Result<filesystem::path> resolve(const string& relativePath) const;
    Status stageRemoval(const string& billId) const;
    Status finishRemoval(const string& billId, bool archived) const;
    Status reconcileRemovals(const StoreData& data) const;

private:
    filesystem::path dataRoot_;
};
