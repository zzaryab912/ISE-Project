#include "BillPdfService.h"

#include <QFile>
#include <QGuiApplication>
#include <QPageLayout>
#include <QPageSize>
#include <QPainter>
#include <QPdfWriter>
#include <QSaveFile>
#include <QTextDocument>
#include <QTextOption>

#include <algorithm>
#include <limits>
#include <string_view>
#include <system_error>
#include <utility>

using namespace std;

namespace {

constexpr string_view stageSuffix = ".pdf.archiving";

bool validBillId(string_view id)
{
    return id.size() == 9 &&
           all_of(id.begin(), id.end(), [](char c) { return c >= '0' && c <= '9'; });
}

Result<filesystem::file_status> inspect(const filesystem::path& path)
{
    error_code error;
    const auto status = filesystem::symlink_status(path, error);
    if (error == errc::no_such_file_or_directory) {
        return {true, {}, filesystem::file_status(filesystem::file_type::not_found)};
    }
    if (error) {
        return {false, "Could not inspect " + path.u8string() + ". " + error.message(), {}};
    }
    return {true, {}, status};
}

Result<filesystem::path> billsDirectory(const filesystem::path& dataRoot)
{
    if (dataRoot.empty()) {
        return {false, "A data root is required to access bill PDFs.", {}};
    }
    error_code error;
    const auto absoluteRoot = filesystem::absolute(dataRoot, error);
    if (error) {
        return {false, "Could not locate the bill data root. " + error.message(), {}};
    }
    const auto root = filesystem::weakly_canonical(absoluteRoot, error);
    if (error) {
        return {false, "Could not resolve the bill data root. " + error.message(), {}};
    }

    auto directory = root / "bills";
    const auto status = inspect(directory);
    if (!status.success) {
        return {false, status.message, {}};
    }
    if (filesystem::exists(status.value) && !filesystem::is_directory(status.value)) {
        return {false, "The managed bills path must be a directory inside the data root.", {}};
    }
    return {true, {}, std::move(directory)};
}

Result<bool> regularFileExists(const filesystem::path& path)
{
    const auto status = inspect(path);
    if (!status.success) {
        return {false, status.message, false};
    }
    if (!filesystem::exists(status.value)) {
        return {true, {}, false};
    }
    if (!filesystem::is_regular_file(status.value)) {
        return {false, "Expected a regular managed bill file at " + path.u8string() + ".", false};
    }
    return {true, {}, true};
}

QString text(const string& value)
{
    return QString::fromStdString(value).toHtmlEscaped();
}

QString scaled(int64_t value, uint64_t scale, int places)
{
    const uint64_t magnitude = value < 0 ? uint64_t{0} - static_cast<uint64_t>(value) : value;
    QString number = QString::number(magnitude / scale);
    if (places > 0) {
        number += QLatin1Char('.') + QString::number(magnitude % scale).rightJustified(places, QLatin1Char('0'));
    }
    return value < 0 ? QLatin1Char('-') + number : number;
}

QString money(int64_t amount)
{
    return scaled(amount, 100, 2);
}

QString paymentMode(PaymentMode mode)
{
    switch (mode) {
    case PaymentMode::Cash:
        return QStringLiteral("Cash");
    case PaymentMode::Card:
        return QStringLiteral("Card");
    case PaymentMode::Wallet:
        return QStringLiteral("Wallet");
    }
    return {};
}

QString summaryRow(const QString& label, const QString& value)
{
    return QStringLiteral("<tr><td>") + label + QStringLiteral("</td><td align='right'>") +
           value + QStringLiteral("</td></tr>");
}

QString detailRow(const SaleLine& line)
{
    const QString quantity = line.unit == Unit::Weight
        ? scaled(line.quantity, 1000, 3) + QStringLiteral(" kg")
        : scaled(line.quantity, 1000, 0) +
            (line.quantity == 1000 ? QStringLiteral(" item") : QStringLiteral(" items"));
    return QStringLiteral("<tr><td>") + text(line.name) +
           QStringLiteral("</td><td>") + text(line.acronym) +
           QStringLiteral("</td><td align='right'>") + quantity +
           QStringLiteral("</td><td align='right'>") + money(line.unitPrice) +
           QStringLiteral("</td><td align='right'>") + money(line.lineProductDiscount) +
           QStringLiteral("</td><td align='right'>") + money(line.lineTax) + QStringLiteral("</td></tr>");
}

QString detailsTable(const QString& rows)
{
    return QStringLiteral(
        "<table border='0.5' cellspacing='0' cellpadding='4' width='100%'>"
        "<thead><tr><th width='28%'>Product</th><th width='14%'>Acronym</th>"
        "<th width='13%'>Quantity</th><th width='17%'>Unit price<br>before tax</th>"
        "<th width='17%'>Product discount</th><th width='11%'>Tax</th></tr></thead>") +
        rows + QStringLiteral("</table>");
}

struct BillContents {
    QString heading;
    vector<QString> rows;
    QString summary;
};

Result<BillContents> billContents(const SaleRecord& sale)
{
    BillContents bill;
    auto& heading = bill.heading;
    heading = QStringLiteral("<h1>Eyeris Bill</h1><p><b>Bill ID</b> ") + text(sale.ids.billId) +
        QStringLiteral("<br><b>Order ID</b> ") + text(sale.ids.orderId) +
        QStringLiteral("<br><b>Bill date and time</b> ") + text(sale.paidAt->date) +
        QLatin1Char(' ') + text(sale.paidAt->time) +
        QStringLiteral("<br><b>Customer</b> ") + text(sale.customer.name);
    if (!sale.customer.contact.empty()) {
        heading += QStringLiteral("<br><b>Contact</b> ") + text(sale.customer.contact);
    }
    heading += QStringLiteral("<br><b>Status</b> Paid");
    if (sale.billChange == BillChange::Modified) {
        heading += QStringLiteral(" &nbsp; <b>Modified</b>");
    } else if (sale.billChange == BillChange::CompleteRefund) {
        heading += QStringLiteral(" &nbsp; <b>Complete Refund</b>");
    }
    heading += QStringLiteral("<br><b>Payment method</b> ") + paymentMode(sale.payment.mode);
    if (!sale.payment.merchant.empty()) {
        heading += QStringLiteral("<br><b>Merchant</b> ") + text(sale.payment.merchant);
    }
    heading += QStringLiteral("</p><h2>Bill details (PKR)</h2>");
    bill.rows.reserve(sale.lines.size());
    for (const auto& line : sale.lines) {
        bill.rows.push_back(detailRow(line));
    }

    int64_t received = sale.payment.amountReceived;
    QString adjustments;
    for (const auto& adjustment : sale.adjustments) {
        QString description = QStringLiteral("No money movement");
        if (adjustment.amountDelta > 0) {
            if (received > numeric_limits<int64_t>::max() - adjustment.amountDelta) {
                return {false, "Total amount received exceeds the supported range.", {}};
            }
            received += adjustment.amountDelta;
            description = QStringLiteral("Collected");
        } else if (adjustment.amountDelta < 0) {
            description = QStringLiteral("Refund");
        }
        const QString when = text(adjustment.at.date) + QLatin1Char(' ') + text(adjustment.at.time);
        adjustments += summaryRow(when + QStringLiteral(" &nbsp; ") + description,
                                  money(adjustment.amountDelta));
    }

    const Totals& totals = sale.totals;
    auto& summary = bill.summary;
    summary = QStringLiteral("<h2>Bill summary (PKR)</h2><table width='100%' cellpadding='2'>");
    summary += summaryRow(QStringLiteral("Subtotal before tax"), money(totals.subtotal));
    summary += summaryRow(QStringLiteral("Product discount"), money(totals.productDiscount));
    summary += summaryRow(QStringLiteral("Seasonal discount"), money(totals.seasonalDiscount));
    summary += summaryRow(QStringLiteral("Special discount"), money(totals.specialDiscount));
    summary += summaryRow(QStringLiteral("Tax"), money(totals.tax));
    if (totals.roundingAdjustment != 0) {
        summary += summaryRow(QStringLiteral("Rounding"), money(totals.roundingAdjustment));
    }
    summary += summaryRow(QStringLiteral("<b>Total payable</b>"), money(totals.payable));
    summary += summaryRow(QStringLiteral("Original amount received"), money(sale.payment.amountReceived));
    summary += summaryRow(QStringLiteral("Total amount received"), money(received));
    summary += summaryRow(QStringLiteral("Change returned"), money(sale.payment.changeReturned));
    summary += QStringLiteral("</table>");
    if (!adjustments.isEmpty()) {
        summary += QStringLiteral("<h2>Paid adjustments (PKR)</h2><table width='100%' cellpadding='2'>") +
                   adjustments + QStringLiteral("</table>");
    }
    return {true, {}, std::move(bill)};
}

Status renderPdf(QIODevice& file, const BillContents& bill)
{
    if (!qobject_cast<QGuiApplication*>(QCoreApplication::instance())) {
        return {false, "Rendering a bill PDF requires a Qt GUI application."};
    }
    QPdfWriter writer(&file);
    writer.setTitle(QStringLiteral("Eyeris Bill"));
    writer.setCreator(QStringLiteral("Eyeris"));
    writer.setResolution(72);
    writer.setPageSize(QPageSize(QPageSize::A4));
    writer.setPageMargins(QMarginsF(15, 15, 15, 15), QPageLayout::Millimeter);

    const QSize pageSize = writer.pageLayout().paintRectPixels(writer.resolution()).size();
    QTextDocument document;
    QFont font = document.defaultFont();
    font.setPointSize(10);
    document.setDefaultFont(font);
    QTextOption options = document.defaultTextOption();
    options.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    document.setDefaultTextOption(options);
    document.setDocumentMargin(0);
    const QSizeF unpaginated(pageSize.width(), -1);

    QPainter painter;
    if (!painter.begin(&writer)) {
        return {false, "Could not start rendering the bill PDF."};
    }

    const auto fitsPage = [&](const QString& html) {
        document.setPageSize(unpaginated);
        document.setHtml(html);
        return document.size().height() <= pageSize.height();
    };
    bool hasPrintedPage = false;
    const auto print = [&](const QString& html, bool allowSplit = false) {
        document.setPageSize(allowSplit ? QSizeF(pageSize) : unpaginated);
        document.setHtml(html);
        const int pages = allowSplit ? document.pageCount() : 1;
        for (int page = 0; page < pages; ++page) {
            if (hasPrintedPage && !writer.newPage()) {
                return false;
            }
            hasPrintedPage = true;
            const qreal top = static_cast<qreal>(page) * pageSize.height();
            painter.save();
            painter.translate(0, -top);
            document.drawContents(&painter, QRectF(0, top, pageSize.width(), pageSize.height()));
            painter.restore();
        }
        return true;
    };

    QString page = bill.heading;
    size_t row = 0;
    while (row < bill.rows.size()) {
        QString rows;
        while (row < bill.rows.size() && fitsPage(page + detailsTable(rows + bill.rows[row]))) {
            rows += bill.rows[row++];
        }
        if (rows.isEmpty() && page.isEmpty()) {
            // A single exceptionally long item can span several pages.
            if (!print(detailsTable(bill.rows[row++]), true)) {
                return {false, "Could not add a page to the bill PDF."};
            }
            continue;
        }
        if (!rows.isEmpty()) {
            page += detailsTable(rows);
        }
        if (row < bill.rows.size()) {
            if (!print(page, rows.isEmpty())) {
                return {false, "Could not add a page to the bill PDF."};
            }
            page.clear();
        }
    }
    if (fitsPage(page + bill.summary)) {
        if (!print(page + bill.summary)) {
            return {false, "Could not add the summary to the bill PDF."};
        }
    } else if ((!page.isEmpty() && !print(page, !fitsPage(page))) || !print(bill.summary, true)) {
        return {false, "Could not add the summary to the bill PDF."};
    }
    if (!painter.end() || !hasPrintedPage) {
        return {false, "Could not finish rendering the bill PDF."};
    }
    return {true, {}};
}

} // namespace

BillPdfService::BillPdfService(filesystem::path dataRoot)
    : dataRoot_(std::move(dataRoot))
{
}

Result<string> BillPdfService::write(const SaleRecord& sale) const
{
    if (sale.status != OrderStatus::Paid || !sale.payment.recorded || !sale.paidAt || sale.archived ||
        !validBillId(sale.ids.billId)) {
        return {false, "Only an active paid bill with a valid ID can be printed.", {}};
    }

    const auto bill = billContents(sale);
    if (!bill.success) {
        return {false, bill.message, {}};
    }
    const auto directory = billsDirectory(dataRoot_);
    if (!directory.success) {
        return {false, directory.message, {}};
    }
    error_code error;
    filesystem::create_directories(directory.value, error);
    if (error) {
        return {false, "Could not create the bills directory. " + error.message(), {}};
    }

    const string filename = sale.ids.billId + ".pdf";
    const auto path = directory.value / filename;
    const auto target = regularFileExists(path);
    if (!target.success) {
        return {false, target.message, {}};
    }
    QSaveFile file(QString::fromStdString(path.u8string()));
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly)) {
        return {false, "Could not open a temporary bill PDF. " + file.errorString().toStdString(), {}};
    }
    const auto rendered = renderPdf(file, bill.value);
    if (!rendered.success) {
        return {false, rendered.message, {}};
    }
    if (!file.commit()) {
        return {false, "Could not save the bill PDF. " + file.errorString().toStdString(), {}};
    }
    return {true, {}, "bills/" + filename};
}

Result<filesystem::path> BillPdfService::resolve(const string& relativePath) const
{
    const string_view path(relativePath);
    if (path.size() != 19 || path.substr(0, 6) != "bills/" ||
        !validBillId(path.substr(6, 9)) || path.substr(15) != ".pdf") {
        return {false, "The PDF path is outside the managed bill format.", {}};
    }
    const auto directory = billsDirectory(dataRoot_);
    if (!directory.success) {
        return {false, directory.message, {}};
    }
    auto absolutePath = directory.value / relativePath.substr(6);
    const auto present = regularFileExists(absolutePath);
    if (!present.success) {
        return {false, present.message, {}};
    }
    if (!present.value) {
        return {false, "The saved bill PDF is missing.", {}};
    }
    QFile file(QString::fromStdString(absolutePath.u8string()));
    if (!file.open(QIODevice::ReadOnly) || file.read(5) != "%PDF-") {
        return {false, "The saved bill PDF cannot be read.", {}};
    }
    // A partial file can still have a valid header. Check the trailer written by QPdfWriter.
    if (!file.seek(max<qint64>(0, file.size() - 1024)) || !file.readAll().trimmed().endsWith("%%EOF")) {
        return {false, "The saved bill PDF is incomplete.", {}};
    }
    return {true, {}, std::move(absolutePath)};
}

Status BillPdfService::stageRemoval(const string& billId) const
{
    if (!validBillId(billId)) {
        return {false, "Invalid bill ID for PDF removal."};
    }
    const auto directory = billsDirectory(dataRoot_);
    if (!directory.success) {
        return {false, directory.message};
    }

    const auto path = directory.value / (billId + ".pdf");
    const auto present = regularFileExists(path);
    if (!present.success) {
        return {false, present.message};
    }
    const auto stagedPath = directory.value / (billId + string(stageSuffix));
    const auto staged = regularFileExists(stagedPath);
    if (!staged.success) {
        return {false, staged.message};
    }
    if (!present.value) {
        return {true, {}};
    }
    if (staged.value) {
        return {false, "Both the original and staged bill PDF exist. PDF recovery is required."};
    }

    error_code error;
    filesystem::rename(path, stagedPath, error);
    if (error) {
        return {false, "Could not stage the bill PDF for removal. " + error.message()};
    }
    return {true, {}};
}

Status BillPdfService::finishRemoval(const string& billId, bool archived) const
{
    if (!validBillId(billId)) {
        return {false, "Invalid bill ID for PDF recovery."};
    }
    const auto directory = billsDirectory(dataRoot_);
    if (!directory.success) {
        return {false, directory.message};
    }

    const auto stagedPath = directory.value / (billId + string(stageSuffix));
    const auto staged = regularFileExists(stagedPath);
    if (!staged.success || !staged.value) {
        return {staged.success, staged.message};
    }

    error_code error;
    if (archived) {
        filesystem::remove(stagedPath, error);
    } else {
        const auto path = directory.value / (billId + ".pdf");
        const auto present = regularFileExists(path);
        if (!present.success) {
            return {false, present.message};
        }
        if (present.value) {
            return {false, "Both the original and staged bill PDF exist. PDF recovery is required."};
        }
        filesystem::rename(stagedPath, path, error);
    }
    if (error) {
        return {false, "Could not finish recovering the staged bill PDF. " + error.message()};
    }
    return {true, {}};
}

Status BillPdfService::reconcileRemovals(const StoreData& data) const
{
    const auto directory = billsDirectory(dataRoot_);
    if (!directory.success) {
        return {false, directory.message};
    }
    const auto status = inspect(directory.value);
    if (!status.success || !filesystem::exists(status.value)) {
        return {status.success, status.message};
    }

    string problems;
    vector<string> stagedFiles;
    error_code error;
    const filesystem::directory_iterator end;
    for (filesystem::directory_iterator it(directory.value, error); !error && it != end; it.increment(error)) {
        const auto filename = it->path().filename().u8string();
        if (filename.size() >= stageSuffix.size() &&
            filename.compare(filename.size() - stageSuffix.size(), stageSuffix.size(), stageSuffix) == 0) {
            stagedFiles.push_back(filename);
        }
    }
    if (error) {
        return {false, "Could not inspect staged bill PDFs. " + error.message()};
    }

    for (const auto& filename : stagedFiles) {
        const auto billId = filename.substr(0, filename.size() - stageSuffix.size());
        const auto sale = find_if(data.sales.begin(), data.sales.end(), [&billId](const SaleRecord& record) {
            return record.status == OrderStatus::Paid && record.ids.billId == billId;
        });
        if (sale == data.sales.end()) {
            problems += "Unknown staged bill file " + filename + ". ";
            continue;
        }
        const auto finished = finishRemoval(billId, sale->archived);
        if (!finished.success) {
            problems += billId + ": " + finished.message + " ";
        }
    }
    return {problems.empty(), std::move(problems)};
}
