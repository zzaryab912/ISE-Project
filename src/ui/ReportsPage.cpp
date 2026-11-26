#include "ReportsPage.h"
#include "PosWindow.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDateEdit>
#include <QFrame>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QVBoxLayout>

using namespace std;

namespace {

const array<const char*, 8> reportNames = {
    "Incomplete Orders", "Completed Orders", "Cash Sales", "All Sales", "Payment Mix",
    "Top Ten Sold", "Highest Remaining Stock", "Lowest Remaining Stock"
};
const array<const char*, 3> paymentNames = {"Cash", "Card", "Wallet"};

QString amount(int64_t value)
{
    const int64_t fraction = value % 100;
    return QString(value < 0 ? "−PKR " : "PKR ") +
        QString::number(value < 0 ? -(value / 100) : value / 100) + '.' +
        QString::number(fraction < 0 ? -fraction : fraction).rightJustified(2, '0');
}

QString quantity(int64_t value, Unit unit)
{
    if (unit == Unit::Count) return QString::number(value / 1000) + " items";
    return QString::number(value / 1000) + '.' +
        QString::number(value % 1000).rightJustified(3, '0') + " kg";
}

QString billState(OrderStatus status, BillChange change)
{
    if (change == BillChange::CompleteRefund) return "Complete Refund";
    if (change == BillChange::Modified) return "Modified";
    return status == OrderStatus::OnHold ? "On hold" : "Paid";
}

QLabel* reportLabel(const QString& title, const QString& role = {})
{
    auto* label = new QLabel(title);
    label->setObjectName(role);
    label->setTextFormat(Qt::PlainText);
    label->setWordWrap(true);
    return label;
}

QWidget* reportField(const QString& title, QWidget* control)
{
    auto* block = new QWidget;
    auto* layout = new QVBoxLayout(block);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6);
    auto* label = reportLabel(title);
    label->setBuddy(control);
    control->setAccessibleName(title);
    control->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    control->setFixedHeight(40);
    layout->addWidget(label);
    layout->addWidget(control);
    return block;
}

QFrame* reportCard()
{
    auto* card = new QFrame;
    card->setObjectName("reportCard");
    return card;
}

void addTableRow(QTableWidget& table, const QStringList& cells)
{
    const int row = table.rowCount();
    table.insertRow(row);
    for (int column = 0; column < cells.size(); ++column) {
        auto* value = new QTableWidgetItem(cells[column]);
        value->setToolTip(cells[column]);
        table.setItem(row, column, value);
    }
}

} // namespace

ReportsPage::ReportsPage(QWidget* parent) : QWidget(parent)
{
    setStyleSheet(R"(
        QLabel { color: #272727; font-size: 14px; background: transparent; }
        QLabel#reportHeading { font-size: 36px; font-weight: 500; }
        QLabel#reportTitle { font-size: 20px; font-weight: 500; }
        QLabel#reportFigure { font-size: 19px; font-weight: 500; }
        QLabel#reportSubtle { color: #646464; }
        QLabel#reportError { color: #a22922; }
        QFrame#reportCard { background: white; border: 1px solid #e3e3e3; border-radius: 21px; }
        QFrame#reportTile { background: #f6f6f6; border: 1px solid #ebebeb; border-radius: 10px; }
        QDateEdit { padding:0 12px; background:#fafafa;
            border:1px solid #e7e7e7; border-radius:8px; color:#252525; }
        QDateEdit:focus { border-color:#858585; }
        QDateEdit::drop-down { border:none; width:30px; background:transparent; }
        QDateEdit::down-arrow { image:none; width:0; height:0; }
        QCheckBox { color:#272727; spacing:8px; }
        QCheckBox::indicator { width:16px; height:16px; border-radius:4px;
            background:#ffffff; border:1px solid #a0abaa; }
        QCheckBox::indicator:checked { background:#4e6766; border:3px solid #e5ebea; }
        QCheckBox::indicator:hover { border-color:#466765; }
        QTableWidget { border:none; background:white; color:#242424; outline:none;
            selection-background-color:#eef2f2; selection-color:#242424; }
        QTableWidget::item { color:#242424; padding:0 10px; border:none; border-bottom:1px solid #e9e9e9; }
        QTableWidget::item:selected { background:#eef2f2; color:#242424; }
        QHeaderView::section { background:white; border:none; border-bottom:1px solid #e3e3e3;
            padding:9px 10px; color:#656565; font-size:13px; font-weight:500; }
    )");

    auto* page = new QVBoxLayout(this);
    page->setContentsMargins(0, 0, 0, 0);
    page->setSpacing(0);
    auto* heading = reportLabel("Reports", "reportHeading");
    heading->setFixedHeight(43);
    page->addWidget(heading);
    page->addSpacing(3);
    page->addWidget(reportLabel("Review sales, payments, and inventory.", "reportSubtle"));
    page->addSpacing(19);

    auto* options = reportCard();
    auto* optionLayout = new QVBoxLayout(options);
    optionLayout->setContentsMargins(24, 20, 24, 20);
    optionLayout->setSpacing(13);
    optionLayout->addWidget(reportLabel("Report options", "reportTitle"));
    auto* filterRow = new QHBoxLayout;
    filterRow->setSpacing(20);
    kind_ = PosUi::choice("Report type");
    kind_->setMinimumWidth(235);
    for (size_t i = 0; i < reportNames.size(); ++i) kind_->addItem(reportNames[i], static_cast<int>(i));
    kind_->setCurrentIndex(kind_->findData(static_cast<int>(ReportKind::AllSales)));
    filterRow->addWidget(reportField("Report type", kind_), 3, Qt::AlignTop);

    period_ = PosUi::choice("Period");
    period_->setMinimumWidth(160);
    filterRow->addWidget(reportField("Period", period_), 2, Qt::AlignTop);

    methods_ = new QWidget;
    auto* modeLayout = new QVBoxLayout(methods_);
    modeLayout->setContentsMargins(0, 0, 0, 0);
    modeLayout->setSpacing(6);
    modeLayout->addWidget(reportLabel("Payment methods"));
    auto* paymentRow = new QWidget;
    paymentRow->setFixedHeight(40);
    auto* checks = new QHBoxLayout(paymentRow);
    checks->setContentsMargins(0, 0, 0, 0);
    checks->setSpacing(18);
    for (size_t i = 0; i < modeChecks_.size(); ++i) {
        modeChecks_[i] = new QCheckBox(paymentNames[i]);
        modeChecks_[i]->setChecked(true);
        checks->addWidget(modeChecks_[i]);
    }
    checks->addStretch();
    modeLayout->addWidget(paymentRow);
    filterRow->addWidget(methods_, 3, Qt::AlignTop);

    unit_ = PosUi::choice("Measurement unit");
    unit_->addItem("Count and Weight", -1);
    unit_->addItem("Count", static_cast<int>(Unit::Count));
    unit_->addItem("Weight", static_cast<int>(Unit::Weight));
    units_ = reportField("Measurement unit", unit_);
    filterRow->addWidget(units_, 3, Qt::AlignTop);
    filterRow->addStretch(2);
    optionLayout->addLayout(filterRow);

    dates_ = new QWidget;
    auto* dateLayout = new QHBoxLayout(dates_);
    dateLayout->setContentsMargins(0, 0, 0, 0);
    dateLayout->setSpacing(20);
    from_ = PosUi::dateEditor(QDate::currentDate());
    to_ = PosUi::dateEditor(QDate::currentDate());
    for (auto* edit : {from_, to_}) {
        edit->setMinimumWidth(165);
    }
    dateLayout->addWidget(reportField("From", from_));
    dateLayout->addWidget(reportField("To", to_));
    dateLayout->addStretch(1);
    optionLayout->addWidget(dates_);

    auto* run = PosUi::primaryButton("Generate Report");
    run->setObjectName("generateReport");
    run->setMinimumWidth(159);
    optionLayout->addWidget(run, 0, Qt::AlignRight);
    page->addWidget(options, 0);
    page->addSpacing(24);

    auto* output = reportCard();
    auto* outputLayout = new QVBoxLayout(output);
    outputLayout->setContentsMargins(24, 21, 24, 21);
    outputLayout->setSpacing(7);
    title_ = reportLabel("No report generated", "reportTitle");
    summary_ = reportLabel("Choose your options, then select Generate Report.", "reportSubtle");
    feedback_ = reportLabel({}, "reportError");
    for (auto* label : {title_, summary_, feedback_})
        label->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    outputSpacer_ = new QWidget;
    outputSpacer_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Expanding);
    feedback_->hide();
    saleTotals_ = new QWidget;
    auto* totalsLayout = new QHBoxLayout(saleTotals_);
    totalsLayout->setContentsMargins(0, 4, 0, 8);
    const array<const char*, 4> totalTitles = {"Collected", "Total discounts", "Refunds", "Net collections"};
    for (size_t i = 0; i < totalTitles.size(); ++i) {
        auto* tile = new QFrame;
        tile->setObjectName("reportTile");
        auto* tileLayout = new QVBoxLayout(tile);
        tileLayout->setContentsMargins(15, 9, 15, 9);
        auto* title = reportLabel(totalTitles[i], "reportSubtle");
        if (i == 2) refundTitle_ = title;
        tileLayout->addWidget(title);
        totalValues_[i] = reportLabel({}, "reportFigure");
        tileLayout->addWidget(totalValues_[i]);
        totalsLayout->addWidget(tile, 1);
    }
    saleTotals_->hide();
    table_ = new QTableWidget;
    table_->setAccessibleName("Report output");
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setShowGrid(false);
    table_->verticalHeader()->hide();
    table_->verticalHeader()->setDefaultSectionSize(44);
    table_->horizontalHeader()->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    table_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    table_->hide();
    outputLayout->addWidget(title_);
    outputLayout->addWidget(summary_);
    outputLayout->addWidget(feedback_);
    outputLayout->addWidget(saleTotals_);
    outputLayout->addWidget(table_, 1);
    outputLayout->addWidget(outputSpacer_, 1);
    page->addWidget(output, 1);

    connect(kind_, &QComboBox::currentIndexChanged, this, [this] {
        updateFilters();
        displayed_.reset();
        title_->setText("No report generated");
        summary_->setText("Choose your options, then select Generate Report.");
        feedback_->hide();
        saleTotals_->hide();
        table_->hide();
        outputSpacer_->show();
    });
    connect(period_, &QComboBox::currentIndexChanged, this, [this] {
        dates_->setVisible(period_->currentData().toInt() == static_cast<int>(ReportPeriod::Custom));
    });
    connect(run, &QPushButton::clicked, this, [this] {
        const auto request = readRequest();
        if (!request.success) showError(request.message);
        else if (generate_) generate_(request.value);
    });
    updateFilters();
}

void ReportsPage::updateFilters()
{
    request_.kind = static_cast<ReportKind>(kind_->currentData().toInt());
    const bool sold = request_.kind == ReportKind::TopSold;
    const bool stock = request_.kind == ReportKind::HighestStock || request_.kind == ReportKind::LowestStock;
    const bool snapshot = stock || request_.kind == ReportKind::Incomplete;
    const QVariant old = period_->currentData();
    const QSignalBlocker blocker(period_);
    period_->clear();
    const auto add = [this](const char* title, ReportPeriod period) {
        period_->addItem(title, static_cast<int>(period));
    };
    if (!snapshot) {
        add("Day", ReportPeriod::Day);
        if (!sold) add("Week", ReportPeriod::Week);
        add("Month", ReportPeriod::Month);
        add("Year", ReportPeriod::Year);
    }
    add(snapshot ? "Current snapshot" : "All time", ReportPeriod::AllTime);
    if (!snapshot && !sold) add("Custom dates", ReportPeriod::Custom);
    period_->setCurrentIndex(old.isValid() && period_->findData(old) >= 0 ? period_->findData(old) : 0);
    period_->setEnabled(!snapshot);
    dates_->setVisible(!snapshot && period_->currentData().toInt() == static_cast<int>(ReportPeriod::Custom));
    methods_->setVisible(request_.kind == ReportKind::AllSales || request_.kind == ReportKind::Completed);
    units_->setVisible(stock || sold);
}

Result<ReportRequest> ReportsPage::readRequest() const
{
    ReportRequest request = request_;
    request.period = static_cast<ReportPeriod>(period_->currentData().toInt());
    request.anchorDate = QDate::currentDate().toString(Qt::ISODate).toStdString();
    if (request.period == ReportPeriod::Custom) {
        if (!from_->hasAcceptableInput() || !to_->hasAcceptableInput() || from_->date() > to_->date()) {
            return {false, "Choose valid dates with From on or before To.", {}};
        }
        request.startDate = from_->date().toString(Qt::ISODate).toStdString();
        request.endDate = to_->date().toString(Qt::ISODate).toStdString();
    }
    if (request.kind == ReportKind::Completed || request.kind == ReportKind::AllSales) {
        for (size_t i = 0; i < modeChecks_.size(); ++i) {
            if (modeChecks_[i]->isChecked()) request.modes.push_back(static_cast<PaymentMode>(i));
        }
        if (request.modes.empty()) return {false, "Select at least one payment method.", {}};
    }
    if ((request.kind == ReportKind::TopSold || request.kind == ReportKind::HighestStock ||
         request.kind == ReportKind::LowestStock) && unit_->currentData().toInt() >= 0) {
        request.unit = static_cast<Unit>(unit_->currentData().toInt());
    }
    return {true, {}, request};
}

void ReportsPage::showResult(const ReportResult& report)
{
    displayed_ = report;
    feedback_->hide();
    saleTotals_->hide();
    title_->setText(kind_->currentText());
    const QString dateRange = report.startDate.empty() ? QString() :
        QString::fromStdString(report.startDate + "  –  " + report.endDate) + "    ·    ";
    table_->setRowCount(0);
    QStringList headers;
    const auto kind = request_.kind;
    if (kind == ReportKind::PaymentMix) headers = {"Payment method", "Orders", "Share"};
    else if (kind == ReportKind::TopSold) headers = {"Product", "Product ID", "Quantity sold", "On hand", "Reserved", "Available"};
    else if (kind == ReportKind::HighestStock || kind == ReportKind::LowestStock)
        headers = {"Product", "Product ID", "Measurement", "On hand", "Reserved", "Available"};
    else if (kind == ReportKind::Incomplete) headers = {"Order ID", "Customer", "Created", "Unpaid bill", "Reservations"};
    else headers = {"Order ID", "Customer", "First paid", "Status", "Method", "Latest payable", "Collected", "Refunded"};
    table_->setColumnCount(headers.size());
    table_->setHorizontalHeaderLabels(headers);

    for (const auto& row : report.orders) {
        QStringList reserves;
        for (const auto& item : row.reservations) {
            reserves << QString::fromStdString(item.name) + " · " + quantity(item.quantity, item.unit) +
                " reserved, " + quantity(item.stock.available, item.unit) + " available";
        }
        const QString date = QString::fromStdString(row.at.date);
        if (kind == ReportKind::Incomplete) {
            addTableRow(*table_, {QString::fromStdString(row.ids.orderId), QString::fromStdString(row.customerName),
                date, amount(row.totals.payable), reserves.join('\n')});
        } else {
            addTableRow(*table_, {QString::fromStdString(row.ids.orderId), QString::fromStdString(row.customerName),
                date, billState(row.status, row.billChange), row.mode ? paymentNames.at(static_cast<size_t>(*row.mode)) : "—",
                amount(row.totals.payable), amount(row.collected), amount(row.refunded)});
        }
    }
    for (const auto& row : report.products) {
        const auto stockUnit = row.stockUnit.value_or(row.unit);
        const QString lead = kind == ReportKind::TopSold ? quantity(row.quantitySold, row.unit) :
            (row.unit == Unit::Count ? "Count" : "Weight");
        addTableRow(*table_, {QString::fromStdString(row.name), QString::fromStdString(row.productId), lead,
            row.stock ? quantity(row.stock->onHand, stockUnit) : "—",
            row.stock ? quantity(row.stock->reserved, stockUnit) : "—",
            row.stock ? quantity(row.stock->available, stockUnit) : "—"});
    }
    for (const auto& row : report.paymentMix) {
        addTableRow(*table_, {paymentNames.at(static_cast<size_t>(row.mode)), QString::number(row.count),
            QString::number(row.shareBasisPoints / 100) + '.' +
                QString::number(row.shareBasisPoints % 100).rightJustified(2, '0') + '%'});
    }
    if (table_->rowCount() == 0) {
        summary_->setText(dateRange + "No results for these options.");
        table_->hide();
        outputSpacer_->show();
        return;
    }
    QString totals;
    if (kind == ReportKind::Incomplete) totals = "Unpaid bills  " + amount(report.billTotal);
    else if (kind == ReportKind::HighestStock || kind == ReportKind::LowestStock)
        totals = "Out of stock  " + QString::number(report.outOfStockCount);
    else if (kind != ReportKind::PaymentMix && kind != ReportKind::TopSold) {
        const array<int64_t, 4> figures{report.collected, report.totalDiscount, report.refunded, report.netCollections};
        for (size_t i = 0; i < figures.size(); ++i) totalValues_[i]->setText(amount(figures[i]));
        refundTitle_->setText("Refunds (" + QString::number(report.refundCount) + ")");
        saleTotals_->show();
        QStringList methods;
        if (kind == ReportKind::CashSales) {
            methods << "Cash";
        } else {
            for (size_t i = 0; i < modeChecks_.size(); ++i) {
                if (modeChecks_[i]->isChecked()) methods << paymentNames[i];
            }
        }
        totals = methods.join(", ");
    }
    summary_->setText(totals.isEmpty() ? "Archived bills are excluded." : dateRange + totals);
    outputSpacer_->hide();
    table_->show();
    table_->resizeRowsToContents();
}

void ReportsPage::showError(const string& message)
{
    displayed_.reset();
    title_->setText("Report unavailable");
    summary_->clear();
    feedback_->setText(QString::fromStdString(message));
    feedback_->show();
    saleTotals_->hide();
    table_->hide();
    outputSpacer_->show();
}

void ReportsPage::setGenerateAction(function<void(const ReportRequest&)> action)
{
    generate_ = std::move(action);
}
