#include "HistoryService.h"

#include "BillPdfService.h"
#include "IPosStore.h"
#include "ProductService.h"

#include <algorithm>
#include <utility>

using namespace std;

namespace {

SaleRecord* findSale(StoreData& data, const string& orderId)
{
    const auto sale = find_if(data.sales.begin(), data.sales.end(), [&orderId](const SaleRecord& record) {
        return record.ids.orderId == orderId;
    });
    return sale == data.sales.end() ? nullptr : &*sale;
}

bool canUsePdf(const SaleRecord& sale)
{
    return sale.status == OrderStatus::Paid && sale.payment.recorded && sale.paidAt && !sale.archived;
}

bool hasManagedPdfReference(const SaleRecord& sale)
{
    return sale.pdfPath == "bills/" + sale.ids.billId + ".pdf";
}

} // namespace

HistoryService::HistoryService(IPosStore& store, const ProductService& products, const BillPdfService& pdf)
    : store_(store), products_(products), pdf_(pdf)
{
}

Result<vector<SaleRecord>> HistoryService::list() const
{
    return listByArchiveStatus(false);
}

Result<vector<SaleRecord>> HistoryService::listArchived() const
{
    return listByArchiveStatus(true);
}

Result<vector<SaleRecord>> HistoryService::listByArchiveStatus(bool archived) const
{
    auto data = store_.read();
    if (!data.success) {
        return {false, "Could not read order history. " + data.message, {}};
    }

    auto& sales = data.value.sales;
    sales.erase(remove_if(sales.begin(), sales.end(), [archived](const SaleRecord& sale) {
        return sale.archived != archived;
    }), sales.end());
    return {true, {}, std::move(sales)};
}

Result<SaleRecord> HistoryService::get(const string& orderId) const
{
    auto data = store_.read();
    if (!data.success) {
        return {false, "Could not read order history. " + data.message, {}};
    }
    auto* sale = findSale(data.value, orderId);
    if (!sale) {
        return {false, "Order not found.", {}};
    }
    return {true, {}, std::move(*sale)};
}

Status HistoryService::archive(const string& orderId)
{
    auto data = store_.read();
    if (!data.success) {
        return {false, "Could not read order history. " + data.message};
    }
    auto* sale = findSale(data.value, orderId);
    if (!sale || (sale->status != OrderStatus::OnHold && sale->status != OrderStatus::Paid)) {
        return {false, "Only a saved held or paid bill can be archived."};
    }

    const bool paid = sale->status == OrderStatus::Paid;
    if (sale->archived) {
        return paid ? pdf_.finishRemoval(sale->ids.billId, true) : Status{true, {}};
    }
    if (paid) {
        const auto staged = pdf_.stageRemoval(sale->ids.billId);
        if (!staged.success) {
            return staged;
        }
    }

    sale->archived = true;
    sale->pdfPath.clear();
    auto saved = store_.save(data.value);
    if (paid) {
        // The saved archive flag determines whether the staged file is deleted or put back.
        const auto finished = pdf_.finishRemoval(sale->ids.billId, saved.success);
        if (!finished.success) {
            if (!saved.message.empty()) {
                saved.message += " ";
            }
            saved.message += (saved.success ? "Bill archived. " : "Could not restore the bill PDF. ") +
                             finished.message;
        }
    }
    return saved;
}

Result<vector<StockShortage>> HistoryService::unarchiveShortages(const string& orderId) const
{
    auto data = store_.read();
    if (!data.success) {
        return {false, "Could not read order history. " + data.message, {}};
    }
    const auto* sale = findSale(data.value, orderId);
    if (!sale) {
        return {false, "Order not found.", {}};
    }
    return shortagesForRestore(data.value, *sale);
}

Result<vector<StockShortage>> HistoryService::shortagesForRestore(const StoreData& data,
                                                                 const SaleRecord& sale) const
{
    if (!sale.archived || (sale.status != OrderStatus::OnHold && sale.status != OrderStatus::Paid)) {
        return {false, "Select an archived held or paid bill.", {}};
    }
    if (sale.status == OrderStatus::Paid) {
        return {true, {}, {}};
    }
    return products_.shortages(data, sale.lines, {});
}

Status HistoryService::unarchive(const string& orderId)
{
    auto data = store_.read();
    if (!data.success) {
        return {false, "Could not read order history. " + data.message};
    }
    auto* sale = findSale(data.value, orderId);
    if (!sale) {
        return {false, "Order not found."};
    }
    const auto shortages = shortagesForRestore(data.value, *sale);
    if (!shortages.success) {
        return {false, shortages.message};
    }
    if (!shortages.value.empty()) {
        return {false, "Insufficient stock to restore the held bill."};
    }
    if (sale->status == OrderStatus::Paid) {
        const auto finished = pdf_.finishRemoval(sale->ids.billId, true);
        if (!finished.success) {
            return finished;
        }
    } else {
        // An active hold uses the live barcode for its reservation, including after a relabel.
        for (auto& line : sale->lines) {
            const auto product = find_if(data.value.products.begin(), data.value.products.end(),
                [&line](const Product& item) { return item.id == line.productId; });
            if (product == data.value.products.end()) {
                return {false, "Held product no longer exists: " + line.name + "."};
            }
            line.barcode = product->barcode;
        }
    }

    sale->archived = false;
    sale->pdfPath.clear();
    return store_.save(data.value);
}

Result<SaleRecord> HistoryService::generatePdf(const string& orderId)
{
    auto data = store_.read();
    if (!data.success) {
        return {false, "Could not read order history. " + data.message, {}};
    }
    auto* sale = findSale(data.value, orderId);
    if (!sale || !canUsePdf(*sale)) {
        return {false, "Only an active paid bill can have a PDF.", {}};
    }
    const auto recovered = pdf_.finishRemoval(sale->ids.billId, false);
    if (!recovered.success) {
        return {false, recovered.message, {}};
    }
    if (hasManagedPdfReference(*sale) && pdf_.resolve(sale->pdfPath).success) {
        return {true, {}, std::move(*sale)};
    }

    auto written = pdf_.write(*sale);
    if (!written.success) {
        return {false, written.message, {}};
    }
    if (sale->pdfPath == written.value) {
        return {true, {}, std::move(*sale)};
    }

    sale->pdfPath = std::move(written.value);
    auto saved = store_.save(data.value);
    if (!saved.success) {
        return {false, "Could not record the bill PDF in order history. " + saved.message, {}};
    }
    return {true, std::move(saved.message), std::move(*sale)};
}

Result<filesystem::path> HistoryService::openPdfPath(const string& orderId) const
{
    const auto sale = get(orderId);
    if (!sale.success) {
        return {false, sale.message, {}};
    }
    if (!canUsePdf(sale.value) || !hasManagedPdfReference(sale.value)) {
        return {false, "This bill does not have an available PDF.", {}};
    }
    return pdf_.resolve(sale.value.pdfPath);
}

Status HistoryService::recoverPdfRemovals() const
{
    const auto data = store_.read();
    if (!data.success) {
        return {false, "Could not read order history. " + data.message};
    }
    return pdf_.reconcileRemovals(data.value);
}
