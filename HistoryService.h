#pragma once

#include "PosTypes.h"

using namespace std;

class IPosStore;
class ProductService;

class HistoryService {
public:
    HistoryService(IPosStore& store, ProductService& products);

    Result<vector<SaleRecord>> list() const;
    Result<vector<SaleRecord>> listArchived() const;
    Result<SaleRecord> get(const string& orderId) const;

    Result<SaleRecord> markPaid(const string& orderId, const Payment& payment);
    Status archive(const string& orderId);

private:
    Result<vector<SaleRecord>> listByArchiveStatus(bool archived) const;
    IPosStore& store_;
    ProductService& products_;
};
