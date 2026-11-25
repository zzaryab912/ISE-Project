#pragma once

#include "PosTypes.h"

#include <filesystem>

using namespace std;

class BillPdfService;
class IPosStore;
class ProductService;

class HistoryService {
public:
    HistoryService(IPosStore& store, const ProductService& products, const BillPdfService& pdf);

    Result<vector<SaleRecord>> list() const;
    Result<vector<SaleRecord>> listArchived() const;
    Result<SaleRecord> get(const string& orderId) const;

    Status archive(const string& orderId);
    Result<vector<StockShortage>> unarchiveShortages(const string& orderId) const;
    Status unarchive(const string& orderId);

    Result<SaleRecord> generatePdf(const string& orderId);
    Result<filesystem::path> openPdfPath(const string& orderId) const;
    Status recoverPdfRemovals() const;

private:
    Result<vector<SaleRecord>> listByArchiveStatus(bool archived) const;
    Result<vector<StockShortage>> shortagesForRestore(const StoreData& data, const SaleRecord& sale) const;

    IPosStore& store_;
    const ProductService& products_;
    const BillPdfService& pdf_;
};
