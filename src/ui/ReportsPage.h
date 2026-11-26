#pragma once

#include "PosTypes.h"

#include <QWidget>
#include <array>
#include <functional>

using namespace std;

class QCheckBox;
class QComboBox;
class QDateEdit;
class QLabel;
class QTableWidget;

class ReportsPage final : public QWidget {
private:
    ReportRequest request_;
    optional<ReportResult> displayed_;
    QComboBox* kind_ = nullptr;
    QComboBox* period_ = nullptr;
    QComboBox* unit_ = nullptr;
    QDateEdit* from_ = nullptr;
    QDateEdit* to_ = nullptr;
    QWidget* dates_ = nullptr;
    QWidget* methods_ = nullptr;
    QWidget* units_ = nullptr;
    QLabel* title_ = nullptr;
    QLabel* summary_ = nullptr;
    QLabel* feedback_ = nullptr;
    QWidget* saleTotals_ = nullptr;
    QTableWidget* table_ = nullptr;
    array<QCheckBox*, 3> modeChecks_{};
    array<QLabel*, 4> totalValues_{};
    QLabel* refundTitle_ = nullptr;
    function<void(const ReportRequest&)> generate_;

    void updateFilters();

public:
    explicit ReportsPage(QWidget* parent = nullptr);

    Result<ReportRequest> readRequest() const;
    void showResult(const ReportResult& report);
    void showError(const string& message);
    void setGenerateAction(function<void(const ReportRequest&)> action);
};
