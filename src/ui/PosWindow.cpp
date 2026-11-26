#include "PosWindow.h"
#include "BarcodePlotter.h"
#include "CartService.h"
#include "CheckoutService.h"
#include "HistoryService.h"
#include "ManagerCataloguePage.h"
#include "ManagerPinService.h"
#include "ReportService.h"
#include "ReportsPage.h"
#include "PosScanner.h"
#include "ProductService.h"

#include <QApplication>
#include <QCloseEvent>
#include <QComboBox>
#include <QDateTime>
#include <QDesktopServices>
#include <QDialog>
#include <QDir>
#include <QEvent>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFormLayout>
#include <QGraphicsDropShadowEffect>
#include <QGuiApplication>
#include <QImage>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QLocale>
#include <QMouseEvent>
#include <QMessageBox>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPushButton>
#include <QPointer>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QScopedValueRollback>
#include <QScreen>
#include <QScrollArea>
#include <QScrollBar>
#include <QStackedWidget>
#include <QStyle>
#include <QStyledItemDelegate>
#include <QSvgRenderer>
#include <QUrl>
#include <QVBoxLayout>
#include <QWindow>

#include <algorithm>
#include <array>
#include <functional>
#include <limits>
#include <memory>
#include <map>

namespace {

QString text(const string& value) { return QString::fromStdString(value); }

QString fixedText(int64_t value, int scale, int places)
{
    const auto fraction = value % scale;
    return (value < 0 ? QString("−") : QString()) + QString::number(qAbs(value / scale)) +
        "." + QString::number(qAbs(fraction)).rightJustified(places, '0');
}

QString quantityText(int64_t value, Unit unit)
{
    return unit == Unit::Count ? QString::number(value / 1000)
                               : fixedText(value, 1000, 3);
}

Result<int64_t> fixedInput(const QString& input, int places)
{
    static const QRegularExpression pattern(R"(^([0-9]+|[0-9]{1,3}(?:,[0-9]{3})+)(?:\.([0-9]+))?$)");
    const auto match = pattern.match(input.trimmed());
    const QString fraction = match.captured(2);
    if (!match.hasMatch() || fraction.size() > places) {
        return {false, "Enter a nonnegative number with at most " + to_string(places) +
                       " decimal places.", 0};
    }
    bool valid = false;
    QString digits = match.captured(1);
    digits.remove(',');
    digits += fraction.leftJustified(places, '0');
    const auto value = digits.toLongLong(&valid);
    return valid ? Result<int64_t>{true, {}, value}
                 : Result<int64_t>{false, "The number is too large.", 0};
}

Result<int64_t> quantityInput(const QString& input, Unit unit)
{
    if (unit == Unit::Count && input.contains('.')) {
        return {false, "Enter a whole quantity for Count.", 0};
    }
    return fixedInput(input, 3);
}

QRegularExpression quantityPattern(Unit unit)
{
    const QString whole = R"((?:[0-9]+|[0-9]{1,3}(?:,[0-9]{3})+))";
    return QRegularExpression("^" + whole +
        (unit == Unit::Weight ? R"((?:\.[0-9]{1,3})?)" : "") + "$");
}

QString statusText(OrderStatus status)
{
    switch (status) {
    case OrderStatus::OnHold: return "On hold";
    case OrderStatus::Paid: return "Paid";
    case OrderStatus::InProgress: return "In progress";
    }
    return {};
}

// Decorative SVGs exported from the authoritative Figma frame.
constexpr char cameraSvg[] = R"svg(<svg preserveAspectRatio="none" overflow="visible" style="display: block;" width="32" height="32" viewBox="0 0 32 32" fill="none" xmlns="http://www.w3.org/2000/svg">
<g id="Camera icon">
<path id="Vector" d="M5.33333 9.33333H9.33333L11.3333 6.66667H20.6667L22.6667 9.33333H26.6667C27.3739 9.33333 28.0522 9.61428 28.5523 10.1144C29.0524 10.6145 29.3333 11.2928 29.3333 12V24C29.3333 24.7072 29.0524 25.3855 28.5523 25.8856C28.0522 26.3857 27.3739 26.6667 26.6667 26.6667H5.33333C4.62609 26.6667 3.94781 26.3857 3.44771 25.8856C2.94762 25.3855 2.66667 24.7072 2.66667 24V12C2.66667 11.2928 2.94762 10.6145 3.44771 10.1144C3.94781 9.61428 4.62609 9.33333 5.33333 9.33333Z" stroke-width="2" stroke="#777777"/>
<path id="Vector_2" d="M20 17.3333C20 19.5425 18.2091 21.3333 16 21.3333C13.7909 21.3333 12 19.5425 12 17.3333C12 15.1242 13.7909 13.3333 16 13.3333C18.2091 13.3333 20 15.1242 20 17.3333Z" stroke-width="2" stroke="#777777"/>
</g>
</svg>)svg";
constexpr char closeSvg[] = R"svg(<svg preserveAspectRatio="none" overflow="visible" style="display: block;" width="12" height="12" viewBox="0 0 12 12" fill="none" xmlns="http://www.w3.org/2000/svg">
<circle id="Close" cx="6" cy="6" r="6" fill="#FF5F57"/>
</svg>)svg";
constexpr char minimizeSvg[] = R"svg(<svg preserveAspectRatio="none" overflow="visible" style="display: block;" width="12" height="12" viewBox="0 0 12 12" fill="none" xmlns="http://www.w3.org/2000/svg">
<circle id="Minimize" cx="6" cy="6" r="6" fill="#FEBC2E"/>
</svg>)svg";
constexpr char zoomSvg[] = R"svg(<svg preserveAspectRatio="none" overflow="visible" style="display: block;" width="12" height="12" viewBox="0 0 12 12" fill="none" xmlns="http://www.w3.org/2000/svg">
<circle id="Zoom" cx="6" cy="6" r="6" fill="#28C840"/>
</svg>)svg";

// Original Figma wordmark outlines, with the gradient from the supplied wordmark.
constexpr char wordmarkSvg[] = R"svg(<svg width="168" height="48" viewBox="0 0 168 48" fill="none" xmlns="http://www.w3.org/2000/svg">
<defs>
  <linearGradient id="wordmarkColor" gradientUnits="userSpaceOnUse" x1="0" x2="0" y1="0" y2="48">
    <stop offset="0" stop-color="#515756"/>
    <stop offset="0.5" stop-color="#9CA8A8"/>
    <stop offset="1" stop-color="#DEEFEF"/>
  </linearGradient>
</defs>
<path d="M0 38.2061V0.158203H33.0908V4.9043H5.58984V16.4795H31.2715V21.0674H5.58984V33.46H33.0908V38.2061H0Z
M43.2686 47.4609C42.6182 47.4609 41.9854 47.4346 41.3701 47.3818C40.7549 47.3291 40.21 47.25 39.7354 47.1445V42.8467C40.1221 42.917 40.5615 42.9785 41.0537 43.0312C41.5459 43.1016 42.0557 43.1367 42.583 43.1367C44.165 43.1367 45.4922 42.8818 46.5645 42.3721C47.6367 41.8623 48.5684 40.9395 49.3594 39.6035L50.1504 38.1533L36.2549 10.6523H42.0557L53.6045 34.2246L52.3125 33.2227H54.1318L52.8398 34.2246L63.8613 10.6523H69.6357L55.9512 38.6016C54.8438 40.7812 53.71 42.5215 52.5498 43.8223C51.3896 45.123 50.0713 46.0547 48.5947 46.6172C47.1182 47.1797 45.3428 47.4609 43.2686 47.4609Z
M87.1963 38.8125C83.7334 38.8125 80.7539 38.25 78.2578 37.125C75.7617 36 73.8457 34.374 72.5098 32.2471C71.1738 30.1201 70.5059 27.5449 70.5059 24.5215V24.4951C70.5059 21.5771 71.1826 19.0371 72.5361 16.875C73.9072 14.6953 75.8057 13.0078 78.2314 11.8125C80.6748 10.6172 83.5312 10.0371 86.8008 10.0723C90.1055 10.0898 92.9355 10.7051 95.291 11.918C97.6641 13.1133 99.4746 14.792 100.723 16.9541C101.988 19.1162 102.621 21.6475 102.621 24.5479V25.8398H73.2217V21.9639H99.3516L97.3213 25.1016V23.5723C97.3213 21.6914 96.8906 20.0742 96.0293 18.7207C95.1855 17.3496 93.9814 16.2949 92.417 15.5566C90.8525 14.8184 88.9805 14.4492 86.8008 14.4492C84.5156 14.4492 82.5557 14.8096 80.9209 15.5303C79.3037 16.251 78.0645 17.3232 77.2031 18.7471C76.3418 20.1709 75.9111 21.9375 75.9111 24.0469V24.917C75.9111 27.2373 76.4297 29.0918 77.4668 30.4805C78.5039 31.8691 79.8838 32.8799 81.6064 33.5127C83.3467 34.1279 85.2539 34.4355 87.3281 34.4355C88.7695 34.4355 90.123 34.2773 91.3887 33.9609C92.6543 33.6445 93.7529 33.1875 94.6846 32.5898C95.6162 31.9922 96.3018 31.2539 96.7412 30.375L96.8203 30.1641H102.173L102.094 30.4277C101.654 31.7988 100.969 33.0117 100.037 34.0664C99.1055 35.1035 97.9805 35.9736 96.6621 36.6768C95.3438 37.3799 93.8848 37.9072 92.2852 38.2588C90.6855 38.6279 88.9893 38.8125 87.1963 38.8125Z
M107.13 38.2061V10.6523L112.509 10.6787V16.1367H112.667C113.3 14.2734 114.425 12.8057 116.042 11.7334C117.677 10.6611 119.637 10.125 121.922 10.125C122.432 10.125 122.906 10.1514 123.346 10.2041C123.803 10.2393 124.189 10.292 124.506 10.3623V15.2139C124.102 15.1436 123.627 15.0908 123.082 15.0557C122.537 15.0029 121.957 14.9766 121.342 14.9766C119.549 14.9766 117.993 15.3281 116.675 16.0312C115.356 16.7344 114.328 17.7539 113.59 19.0898C112.869 20.4082 112.509 21.999 112.509 23.8623V38.2061H107.13Z
M128.355 38.2061V10.6523H133.734V38.2061H128.355Z
M131.045 6.22266C130.131 6.22266 129.357 5.92383 128.725 5.32617C128.109 4.71094 127.802 3.97266 127.802 3.11133C127.802 2.23242 128.109 1.49414 128.725 0.896484C129.357 0.298828 130.131 0 131.045 0C131.977 0 132.75 0.298828 133.365 0.896484C133.98 1.49414 134.288 2.23242 134.288 3.11133C134.288 3.97266 133.98 4.71094 133.365 5.32617C132.75 5.92383 131.977 6.22266 131.045 6.22266Z
M153.826 38.8125C150.732 38.8125 148.096 38.4873 145.916 37.8369C143.736 37.1689 142.031 36.2109 140.801 34.9629C139.588 33.6973 138.858 32.1768 138.612 30.4014L138.586 30.1904H143.965L144.018 30.4014C144.352 31.8076 145.336 32.8799 146.971 33.6182C148.605 34.3389 150.926 34.6992 153.932 34.6992C155.725 34.6992 157.236 34.541 158.467 34.2246C159.715 33.8906 160.655 33.416 161.288 32.8008C161.921 32.168 162.237 31.4121 162.237 30.5332V30.5068C162.237 29.4521 161.824 28.6611 160.998 28.1338C160.189 27.5889 158.757 27.1934 156.7 26.9473L149.001 26.1299C146.892 25.8662 145.125 25.4092 143.701 24.7588C142.295 24.1084 141.231 23.2646 140.511 22.2275C139.808 21.1904 139.456 19.96 139.456 18.5361V18.5098C139.456 16.7695 139.983 15.2666 141.038 14.001C142.11 12.7354 143.64 11.7598 145.626 11.0742C147.612 10.3887 149.977 10.0459 152.719 10.0459C155.637 10.0459 158.124 10.3887 160.181 11.0742C162.255 11.7422 163.863 12.6914 165.006 13.9219C166.148 15.1523 166.79 16.6201 166.931 18.3252L166.957 18.668H161.868L161.842 18.5098C161.631 17.1738 160.734 16.1191 159.152 15.3457C157.57 14.5723 155.443 14.1855 152.771 14.1855C151.066 14.1855 149.625 14.3525 148.447 14.6865C147.287 15.0029 146.408 15.4688 145.811 16.084C145.213 16.6816 144.914 17.4023 144.914 18.2461V18.2725C144.914 18.9404 145.099 19.5117 145.468 19.9863C145.854 20.4609 146.452 20.8477 147.261 21.1465C148.087 21.4453 149.168 21.6826 150.504 21.8584L158.229 22.6758C161.604 23.0801 164.03 23.8799 165.507 25.0752C166.983 26.2705 167.722 27.9668 167.722 30.1641V30.1904C167.722 31.9658 167.168 33.4951 166.061 34.7783C164.953 36.0615 163.362 37.0547 161.288 37.7578C159.214 38.4609 156.727 38.8125 153.826 38.8125Z" fill="url(#wordmarkColor)"/>
</svg>)svg";

constexpr int canvasWidth = 1784;
constexpr int canvasHeight = 1054;
constexpr int pageLeft = 258;
constexpr int pageWidth = 1502;
constexpr auto ink = "#171717";
constexpr auto muted = "#686868";
constexpr auto detailInk = "#242424";
constexpr auto detailMuted = "#727272";
constexpr auto errorInk = "#a22922";

QFont interfaceFont(int pixels, QFont::Weight weight = QFont::Normal)
{
    QFont result = QFontDatabase::systemFont(QFontDatabase::GeneralFont);
    result.setPixelSize(pixels);
    result.setWeight(static_cast<QFont::Weight>(std::max(300, static_cast<int>(weight) - 65)));
    result.setHintingPreference(QFont::PreferNoHinting);
    if (weight == QFont::DemiBold && pixels >= 18 && pixels <= 28) {
        result.setStretch(103);
    }
    return result;
}

QLabel* label(QWidget* parent, QRect bounds, const QString& value, int pixels = 14,
              const char* color = ink, QFont::Weight weight = QFont::Normal,
              Qt::Alignment alignment = Qt::AlignLeft | Qt::AlignVCenter)
{
    auto* widget = new QLabel(value, parent);
    widget->setGeometry(bounds);
    widget->setTextFormat(Qt::PlainText);
    widget->setAlignment(alignment);
    widget->setFont(interfaceFont(pixels, weight));
    widget->setStyleSheet(QString("color: %1; background: transparent; border: none;").arg(color));
    widget->setAttribute(Qt::WA_TransparentForMouseEvents);
    return widget;
}

int wrapHeight(QLabel& widget, const QString& value, int minimum)
{
    widget.setWordWrap(true);
    widget.setAlignment(Qt::AlignLeft | Qt::AlignTop);
    widget.setText(value);
    widget.setToolTip(value);
    return std::max(minimum, widget.heightForWidth(widget.width()));
}

QWidget* surface(QWidget* parent, QRect bounds, const char* fill = "#ffffff",
                 const char* border = "#e7e7e7", int radius = 21)
{
    auto* widget = new QWidget(parent);
    widget->setGeometry(bounds);
    widget->setObjectName("posSurface");
    widget->setAttribute(Qt::WA_StyledBackground);
    widget->setStyleSheet(QString("QWidget#posSurface {background:%1; border:1px solid %2; border-radius:%3px;}")
                              .arg(fill, border).arg(radius));
    return widget;
}

class PanelShadow final : public QWidget {
public:
    PanelShadow(QWidget* parent, QRect bounds) : QWidget(parent)
    {
        setGeometry(bounds.adjusted(-5, -5, 5, 7));
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setAttribute(Qt::WA_NoSystemBackground);
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setBrush(Qt::NoBrush);
        const QRectF card(5, 5, width() - 10, height() - 12);
        for (int spread = 4; spread >= 1; --spread) {
            painter.setPen(QPen(QColor(0, 0, 0, spread == 1 ? 5 : 3), 1));
            painter.drawRoundedRect(card.adjusted(-spread, -spread + 2, spread, spread + 2),
                                    21 + spread, 21 + spread);
        }
    }
};

QWidget* panel(QWidget* parent, QRect bounds, const char* fill = "#ffffff",
               const char* border = "#e7e7e7")
{
    new PanelShadow(parent, bounds);
    return surface(parent, bounds, fill, border);
}

void rule(QWidget* parent, int x, int y, int width, const char* color = "#e7e7e7")
{
    auto* line = new QWidget(parent);
    line->setGeometry(x, y, width, 1);
    line->setStyleSheet(QString("background:%1;").arg(color));
    line->setAttribute(Qt::WA_TransparentForMouseEvents);
}

QString formattedMoney(int64_t value)
{
    return (value < 0 ? QString("−") : QString()) +
           QLocale(QLocale::English, QLocale::UnitedStates).toString(qAbs(value / 100)) + "." +
           QString::number(qAbs(value % 100)).rightJustified(2, '0');
}

QString unitName(Unit unit) { return unit == Unit::Count ? "Count" : "Weight (kg)"; }
QString unitShort(Unit unit) { return unit == Unit::Count ? "item" : "kg"; }

QString availability(const ProductStock& item)
{
    return quantityText(item.stock.available, item.product.unit) +
        (item.product.unit == Unit::Weight ? " kg" : item.stock.available == 1000 ? " item" : " items");
}

QString dateText(const Timestamp& time)
{
    const auto date = QDate::fromString(text(time.date), Qt::ISODate);
    const auto hour = QTime::fromString(text(time.time), Qt::ISODate);
    if (!date.isValid() || !hour.isValid()) return text(time.date + " · " + time.time);
    return QLocale::c().toString(date, "d MMM yyyy") + " · " + hour.toString("h:mm ap");
}

class SvgIcon final : public QWidget {
public:
    SvgIcon(QWidget* parent, QRect bounds, const char* svg, bool flip = false)
        : QWidget(parent), art_(QByteArray(svg)), flip_(flip)
    {
        setGeometry(bounds);
        setAttribute(Qt::WA_TransparentForMouseEvents);
    }

    void setFlipped(bool value)
    {
        flip_ = value;
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        if (flip_) {
            painter.translate(width(), height());
            painter.rotate(180);
        }
        art_.render(&painter, rect());
    }

private:
    QSvgRenderer art_;
    bool flip_;
};

enum class Action { Primary, Checkout, Hold, Secondary, Text, Navigation };

void markSelected(QPushButton& button, bool selected)
{
    button.setProperty("selected", selected);
    button.setAccessibleDescription(selected ? "Selected" : "");
    button.style()->unpolish(&button);
    button.style()->polish(&button);
    button.update();
}

QPushButton* action(QWidget* parent, QRect bounds, const QString& title, Action style = Action::Secondary,
                    bool secondaryShadow = false)
{
    auto* widget = new QPushButton(title, parent);
    widget->setGeometry(bounds);
    widget->setCursor(Qt::PointingHandCursor);
    widget->setFont(interfaceFont(14, style == Action::Navigation ? QFont::Normal : QFont::Medium));
    const bool colored = style == Action::Primary || style == Action::Checkout;
    if (colored) {
        widget->setStyleSheet(QString(
            "QPushButton { background:qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #CADDDD, stop:1 #111111); color:white; border:1px solid transparent; border-radius:%1px; }"
            "QPushButton:hover { background:qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #d9e8e8, stop:1 #292929); }"
            "QPushButton:pressed { background:qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #a5b8b8, stop:1 #080808); }"
            "QPushButton:focus { border:1px solid #ffffff; }"
            "QPushButton:disabled { background:#a0a0a0; color:white; }")
            .arg(bounds.height() / 2));
    } else if (style == Action::Hold) {
        widget->setStyleSheet(
            "QPushButton { color:#242424; background:white; border:1px solid #111111; border-radius:14px; }"
            "QPushButton:hover { background:#f1f1f1; } QPushButton:pressed { background:#dedede; }"
            "QPushButton:focus { border:2px solid #777777; }"
            "QPushButton:disabled { color:#919191; border-color:#cccccc; background:#f5f5f5; }");
    } else if (style == Action::Navigation) {
        widget->setStyleSheet(
            "QPushButton { text-align:left; padding-left:19px; color:#171717; border:1px solid transparent; border-radius:8px; }"
            "QPushButton:hover { background:#f1f1f1; } QPushButton[selected=true] { background:white; }"
            "QPushButton[primaryNav=true][selected=true] { background:#f1f1f1; }"
            "QPushButton:focus { border-color:#878787; }");
    } else if (style == Action::Text) {
        widget->setStyleSheet(
            "QPushButton { text-align:left; color:#727272; border:1px solid transparent; background:transparent; }"
            "QPushButton[selected=true], QPushButton:hover { color:#242424; } QPushButton:focus { border-color:#878787; }");
    } else {
        widget->setStyleSheet(
            "QPushButton { color:#242424; background:white; border:1px solid #e7e7e7; border-radius:8px; }"
            "QPushButton:hover { background:#f6f6f6; } QPushButton:pressed { background:#ededed; }"
            "QPushButton:focus { border-color:#878787; }"
            "QPushButton:disabled { color:#919191; border-color:#eeeeee; }");
    }
    if (colored || style == Action::Hold || secondaryShadow) {
        auto* shadow = new QGraphicsDropShadowEffect(widget);
        shadow->setBlurRadius(colored ? 14 : 8);
        shadow->setOffset(0, colored ? 4 : 2);
        shadow->setColor(QColor(0, 0, 0, colored ? 64 : 16));
        widget->setGraphicsEffect(shadow);
    }
    return widget;
}

QLineEdit* edit(QWidget* parent, QRect bounds, const QString& title, const QString& hint = {},
                bool readOnly = false, bool white = false)
{
    auto* widget = new QLineEdit(parent);
    widget->setGeometry(bounds);
    widget->setAccessibleName(title);
    widget->setPlaceholderText(hint);
    widget->setReadOnly(readOnly);
    widget->setFont(interfaceFont(14));
    widget->setStyleSheet(QString(
        "QLineEdit { color:#242424; background:%1; border:1px solid %2; border-radius:8px; padding:0 12px; }"
        "QLineEdit:focus { border-color:#858585; }"
        "QLineEdit:disabled { color:#777777; background:#f2f2f2; }")
        .arg(readOnly ? "#f2f2f2" : white ? "#ffffff" : "#fafafa",
             white ? "#d6d6d6" : "#e7e7e7"));
    return widget;
}

QLineEdit* field(QWidget* parent, int x, int y, int width, const QString& title,
                 const QString& value = {}, bool readOnly = false, QLabel** caption = nullptr)
{
    auto* titleLabel = label(parent, {x, y, width, 16}, title, 13, detailInk);
    if (caption) *caption = titleLabel;
    auto* widget = edit(parent, {x, y + 21, width, 39}, title, {}, readOnly);
    widget->setText(value);
    return widget;
}

class ChoiceRows final : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    QSize sizeHint(const QStyleOptionViewItem&, const QModelIndex&) const override
    {
        return {0, 40};
    }

    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override
    {
        const bool chosen = index.data(Qt::UserRole).toBool();
        const bool highlighted = option.state & (QStyle::State_Selected | QStyle::State_MouseOver);
        const QRect row = option.rect.adjusted(0, 1, 0, -1);
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        if (chosen || highlighted) {
            painter->setPen(Qt::NoPen);
            painter->setBrush(QColor(chosen ? "#e9e9e9" : "#efefef"));
            painter->drawRoundedRect(row, 7, 7);
        }
        painter->setPen(QColor(ink));
        painter->setFont(interfaceFont(14, chosen ? QFont::Medium : QFont::Normal));
        painter->drawText(row.adjusted(11, 0, -32, 0), Qt::AlignVCenter,
                          index.data(Qt::DisplayRole).toString());
        if (chosen) {
            const QPointF end(row.right() - 15, row.center().y() - 4);
            QPainterPath tick;
            tick.moveTo(end + QPointF(-9, 5));
            tick.lineTo(end + QPointF(-6, 8));
            tick.lineTo(end);
            painter->setPen(QPen(QColor(ink), 1.3, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            painter->drawPath(tick);
        }
        painter->restore();
    }
};

class ChoiceBox final : public QComboBox {
public:
    explicit ChoiceBox(QWidget* parent) : QComboBox(parent)
    {
        setStyleSheet(
            "QComboBox { color:#242424; background:#fafafa; border:1px solid #e7e7e7;"
            " border-radius:9px; padding:0 12px; } QComboBox:disabled { background:#f2f2f2; }"
            "QComboBox:focus { border-color:#858585; }"
            "QComboBox[menuOpen=true] { border:2px solid #171717; padding-left:11px; }"
            "QComboBox::drop-down { border:0; width:30px; }"
            "QComboBox::down-arrow { image:none; width:0; height:0; }");
    }

    void showPopup() override
    {
        if (count() == 0 || !isEnabled()) return;
        if (!popup_) createPopup();
        rows_->clear();
        for (int index = 0; index < count(); ++index) {
            auto* row = new QListWidgetItem(itemText(index), rows_);
            row->setData(Qt::UserRole, index == currentIndex());
        }
        rows_->setCurrentRow(currentIndex());
        const int bodyHeight = count() * 40 + 14;
        popup_->resize(width() + 2 * shadowInset, bodyHeight + 2 * shadowInset);
        body_->setGeometry(shadowInset, shadowInset, width(), bodyHeight);
        rows_->setGeometry(6, 6, width() - 12, bodyHeight - 12);

        QPoint position = mapToGlobal(QPoint(0, height() + 5));
        if (const auto* screen = QGuiApplication::screenAt(position)) {
            const QRect available = screen->availableGeometry();
            if (position.y() + bodyHeight > available.bottom())
                position = mapToGlobal(QPoint(0, -bodyHeight - 5));
        }
        popup_->move(position - QPoint(shadowInset, shadowInset));
        setMenuOpen(true);
        popup_->show();
        rows_->setFocus(Qt::PopupFocusReason);
    }

    void hidePopup() override
    {
        if (popup_) popup_->hide();
        QComboBox::hidePopup();
    }

protected:
    void paintEvent(QPaintEvent* event) override
    {
        QComboBox::paintEvent(event);
        if (!isEnabled()) return;
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(QPen(QColor(ink), 1.35, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        const QPointF center(width() - 20, height() / 2.0);
        const int direction = menuOpen_ ? 1 : -1;
        QPainterPath arrow;
        arrow.moveTo(center + QPointF(-4, direction * 2));
        arrow.lineTo(center + QPointF(0, -direction * 2));
        arrow.lineTo(center + QPointF(4, direction * 2));
        painter.drawPath(arrow);
    }

    bool eventFilter(QObject* source, QEvent* event) override
    {
        if (source == popup_ && event->type() == QEvent::Hide) {
            setMenuOpen(false);
            QComboBox::hidePopup();
        }
        if (source == rows_ && event->type() == QEvent::KeyPress) {
            const int key = static_cast<QKeyEvent*>(event)->key();
            if (key == Qt::Key_Escape) {
                hidePopup();
                return true;
            }
            if (key == Qt::Key_Return || key == Qt::Key_Enter || key == Qt::Key_Space) {
                selectRow();
                return true;
            }
        }
        return QComboBox::eventFilter(source, event);
    }

private:
    static constexpr int shadowInset = 12;

    void setMenuOpen(bool open)
    {
        menuOpen_ = open;
        setProperty("menuOpen", open);
        style()->unpolish(this);
        style()->polish(this);
        update();
    }

    void selectRow()
    {
        const int index = rows_->currentRow();
        if (index >= 0) {
            setCurrentIndex(index);
            emit activated(index);
        }
        hidePopup();
    }

    void createPopup()
    {
        popup_ = new QWidget(this, Qt::Popup | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint);
        popup_->setAttribute(Qt::WA_TranslucentBackground);
        popup_->installEventFilter(this);
        body_ = surface(popup_, {}, "#f7f7f7", "#e4e4e4", 10);
        auto* shadow = new QGraphicsDropShadowEffect(body_);
        shadow->setBlurRadius(19);
        shadow->setOffset(0, 3);
        shadow->setColor(QColor(0, 0, 0, 32));
        body_->setGraphicsEffect(shadow);
        rows_ = new QListWidget(body_);
        rows_->setAccessibleName(accessibleName() + " options");
        rows_->setFrameShape(QFrame::NoFrame);
        rows_->setMouseTracking(true);
        rows_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        rows_->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        rows_->setUniformItemSizes(true);
        rows_->setStyleSheet("QListWidget {background:transparent; border:none; outline:none;}");
        rows_->setItemDelegate(new ChoiceRows(rows_));
        rows_->installEventFilter(this);
        connect(rows_, &QListWidget::itemClicked, this, [this] { selectRow(); });
    }

    QWidget* popup_ = nullptr;
    QWidget* body_ = nullptr;
    QListWidget* rows_ = nullptr;
    bool menuOpen_ = false;
};

QComboBox* selector(QWidget* parent, QRect bounds, const QString& title)
{
    auto* widget = new ChoiceBox(parent);
    widget->setGeometry(bounds);
    widget->setFont(interfaceFont(14));
    widget->setAccessibleName(title);
    return widget;
}

QScrollArea* viewport(QWidget* parent, QRect bounds)
{
    auto* widget = new QScrollArea(parent);
    bounds.setWidth(bounds.width() + 7);
    widget->setGeometry(bounds);
    widget->setFrameShape(QFrame::NoFrame);
    widget->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    widget->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    widget->setStyleSheet(
        "QScrollArea { background:transparent; border:none; }"
        "QScrollBar:vertical { width:7px; background:transparent; }"
        "QScrollBar::handle:vertical { background:#d2d2d2; min-height:25px; border-radius:3px; }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height:0; }");
    widget->viewport()->setAutoFillBackground(false);
    return widget;
}

QWidget* newRows(QScrollArea& area, int width)
{
    const int y = area.verticalScrollBar()->value();
    if (auto* old = area.takeWidget()) {
        old->hide();
        old->deleteLater();
    }
    auto* rows = new QWidget;
    rows->setAutoFillBackground(false);
    rows->setAttribute(Qt::WA_NoSystemBackground);
    rows->setFixedSize(width, 1);
    area.setWidget(rows);
    rows->setAutoFillBackground(false);
    rows->hide();
    rows->setProperty("previousScroll", y);
    return rows;
}

void finishRows(QScrollArea& area, QWidget& rows, int height)
{
    rows.setFixedHeight(std::max(1, height));
    rows.show();
    area.verticalScrollBar()->setValue(rows.property("previousScroll").toInt());
}

class CameraFeed final : public QWidget {
public:
    CameraFeed(QWidget* parent, QRect bounds) : QWidget(parent)
    {
        setGeometry(bounds);
        setAccessibleName("Camera preview");
    }

    void setFrame(const QImage& frame)
    {
        frame_ = frame;
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);
        QRectF bounds(rect());
        if (!frame_.isNull()) {
            QSizeF fitted(frame_.size());
            fitted.scale(bounds.size(), Qt::KeepAspectRatio);
            bounds.setSize(fitted);
            bounds.moveCenter(QRectF(rect()).center());
        }
        QPainterPath clip;
        clip.addRoundedRect(bounds, 10, 10);
        painter.setClipPath(clip);
        if (frame_.isNull()) painter.fillRect(bounds, QColor("#f1f1f1"));
        else painter.drawImage(bounds, frame_);
    }

private:
    QImage frame_;
};

class PaymentFields final {
public:
    PaymentFields(QWidget* parent, int x, int y, int width, int gap, int merchantWidth,
                  const QString& merchantName)
    {
        amount = field(parent, x, y, width, "Payment amount (PKR)");
        amount->setPlaceholderText("Enter exact amount");
        QObject::connect(amount, &QLineEdit::textEdited, amount, [this] { amountEdited_ = true; });
        const int modeX = x + width + gap;
        label(parent, {modeX, y, width, 16}, "Payment mode", 13, detailInk);
        mode = selector(parent, {modeX, y + 21, width, 39}, "Payment mode");
        mode->addItem("Cash", static_cast<int>(PaymentMode::Cash));
        mode->addItem("Card", static_cast<int>(PaymentMode::Card));
        mode->addItem("Wallet", static_cast<int>(PaymentMode::Wallet));
        merchant = field(parent, modeX + width + gap, y, merchantWidth, merchantName);
        merchant->setPlaceholderText(merchantName == "Merchant" ? "—" : "Enter merchant");
    }

    Result<Payment> read() const
    {
        const auto parsed = fixedInput(amount->text(), 2);
        if (!parsed.success) return {false, "Payment amount: " + parsed.message, {}};
        return {true, {}, {true, parsed.value, 0, static_cast<PaymentMode>(mode->currentData().toInt()),
                            merchant->text().trimmed().toStdString()}};
    }

    void setAmount(int64_t money) { if (!amountEdited_ && !amount->hasFocus()) amount->setText(formattedMoney(money)); }
    void clearAmount() { if (!amount->hasFocus()) amount->clear(); }
    bool hasDraft() const { return amountEdited_ || !merchant->text().isEmpty() || mode->currentIndex() != 0; }

    void reset()
    {
        amount->clear();
        amountEdited_ = false;
        mode->setCurrentIndex(mode->findData(static_cast<int>(PaymentMode::Cash)));
        merchant->clear();
    }

private:
    bool amountEdited_ = false;
    QLineEdit* amount = nullptr;
    QComboBox* mode = nullptr;
    QLineEdit* merchant = nullptr;
};

class QuantityStepper final : public QWidget {
public:
    QuantityStepper(QWidget* parent, QRect bounds)
        : QWidget(parent)
    {
        setGeometry(bounds);
        setObjectName("quantityStepper");
        setAttribute(Qt::WA_StyledBackground);
        setStyleSheet("QWidget#quantityStepper {background:white; border:1px solid #dadada; border-radius:8px;}");
        const int buttonWidth = 36;
        const int fieldWidth = bounds.width() - 2 * buttonWidth;
        const auto stepButton = [this, bounds](const QString& title, int x) {
            auto* widget = action(this, {x, 1, buttonWidth, bounds.height() - 2}, title, Action::Text);
            widget->setFont(interfaceFont(17));
            widget->setStyleSheet(
                "QPushButton { background:transparent; border:1px solid transparent; color:#171717; }"
                "QPushButton:hover { background:#f1f1f1; } QPushButton:disabled { color:#aaaaaa; }"
                "QPushButton:focus { border-color:#858585; }");
            return widget;
        };
        less_ = stepButton("−", 1);
        more_ = stepButton("+", buttonWidth + fieldWidth);
        less_->setAccessibleName("Decrease quantity");
        more_->setAccessibleName("Increase quantity");
        value_ = new QLineEdit(this);
        value_->setGeometry(buttonWidth, 1, fieldWidth, bounds.height() - 2);
        value_->setFont(interfaceFont(13, QFont::Normal));
        value_->setAlignment(Qt::AlignCenter);
        countValidator_ = new QRegularExpressionValidator(quantityPattern(Unit::Count), value_);
        weightValidator_ = new QRegularExpressionValidator(quantityPattern(Unit::Weight), value_);
        value_->installEventFilter(this);
        value_->setStyleSheet(
            "QLineEdit { background:transparent; color:#171717; padding:0; border:none;"
            " border-left:1px solid #e7e7e7; border-right:1px solid #e7e7e7; }"
            "QLineEdit:focus { border:1px solid #858585; }");
        connect(less_, &QPushButton::clicked, this, [this] { moveBy(-1); });
        connect(more_, &QPushButton::clicked, this, [this] { moveBy(1); });
        connect(value_, &QLineEdit::textEdited, this, [this] {
            if (typing) typing();
        });
        setQuantity(0, Unit::Count);
    }

    void setQuantity(int64_t value, Unit unit)
    {
        quantity_ = value;
        unit_ = unit;
        value_->setValidator(unit == Unit::Count ? countValidator_ : weightValidator_);
        value_->setAccessibleName(unit == Unit::Count ? "Quantity (items)" : "Quantity (kg)");
        value_->setText(quantityText(value, unit));
        value_->setFont(interfaceFont(unit == Unit::Weight ? 12 : 14, QFont::Medium));
    }

    Result<int64_t> read() const { return quantityInput(value_->text(), unit_); }
    QString draft() const { return value_->text(); }
    bool editing() const { return value_->hasFocus(); }
    void restoreDraft(const QString& draft) { value_->setText(draft); }
    int cursorPosition() const { return value_->cursorPosition(); }
    void focusAt(int position) { value_->setFocus(); value_->setCursorPosition(position); }
    void select() { value_->setFocus(); value_->selectAll(); }
    std::function<void()> cancel;

    std::function<void(int64_t)> changed;
    std::function<void()> typing;
    std::function<void(const string&)> invalid;

protected:
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (watched != value_ || event->type() != QEvent::KeyPress)
            return QWidget::eventFilter(watched, event);
        const auto* key = static_cast<QKeyEvent*>(event);
        if (key->key() == Qt::Key_Escape) {
            value_->setText(quantityText(quantity_, unit_));
            if (cancel) cancel();
            return true;
        }
        if (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) {
            const auto value = read();
            if (value.success) {
                if (changed) changed(value.value);
            } else if (invalid) {
                invalid(value.message);
            }
            return true;
        }
        return QWidget::eventFilter(watched, event);
    }

private:
    void moveBy(int direction)
    {
        const auto value = read();
        if (!value.success) {
            if (invalid) invalid(value.message);
            return;
        }
        const int64_t step = unit_ == Unit::Count ? 1000 : 500;
        if (direction > 0 && value.value > std::numeric_limits<int64_t>::max() - step) {
            if (invalid) invalid("The quantity is too large.");
            return;
        }
        const auto updated = std::max<int64_t>(0, value.value + direction * step);
        setQuantity(updated, unit_);
        if (changed) changed(updated);
    }

    QPushButton* less_ = nullptr;
    QPushButton* more_ = nullptr;
    QLineEdit* value_ = nullptr;
    QRegularExpressionValidator* countValidator_ = nullptr;
    QRegularExpressionValidator* weightValidator_ = nullptr;
    Unit unit_ = Unit::Count;
    int64_t quantity_ = 0;
};

class CurrentOrderPage final : public PosWindow::CurrentOrderView {
public:
    explicit CurrentOrderPage(PosWindow& window) : window_(window)
    {
        auto* heading = label(this, {0, -1, 520, 42}, "Current Order", 36, ink, QFont::Medium);
        auto headingFont = heading->font();
        headingFont.setStretch(99);
        heading->setFont(headingFont);
        label(this, {0, 42, 520, 17}, "Scan products to build your order", 14, muted);
        auto* order = panel(this, {0, 88, 998, 870}, "#ffffff", "#e3e3e3");
        label(order, {28, 26, 300, 24}, "Order items", 20, ink, QFont::DemiBold);
        count_ = label(order, {28, 56, 600, 16}, {}, 13, muted);
        auto* held = action(order, {750, 24, 220, 38}, "Held Orders", Action::Secondary);
        connect(held, &QPushButton::clicked, &window, &PosWindow::onHeldOrdersClicked);
        discardResume_ = action(order, {510, 24, 224, 38}, "Discard Resumed Draft", Action::Secondary);
        connect(discardResume_, &QPushButton::clicked, &window, &PosWindow::onDiscardResumedOrder);
        discardResume_->hide();
        const QStringList captions{"Bill ID", "Order ID", "Status"};
        const QStringList values{"—", "—", "In progress"};
        for (int i = 0; i < captions.size(); ++i) {
            label(order, {28 + 172 * i, 100, 150, 14}, captions[i], 12, muted);
            orderIdentity_[i] = label(order, {28 + 172 * i, 120, 150, 17}, values[i]);
        }
        label(order, {574, 92, 380, 14}, "Customer name", 12, muted);
        name_ = edit(order, {574, 113, 396, 38}, "Customer name", "Enter customer name", false, true);
        label(order, {28, 190, 314, 17}, "Product", 14, muted, QFont::Medium);
        label(order, {366, 190, 114, 17}, "Unit price", 14, muted, QFont::Medium, right_);
        label(order, {366, 211, 114, 13}, "(excl. tax)", 11, muted, QFont::Normal, right_);
        label(order, {500, 190, 118, 17}, "Discount / unit", 14, muted, QFont::Medium, right_);
        label(order, {642, 190, 176, 17}, "Quantity", 14, muted, QFont::Medium, Qt::AlignCenter);
        label(order, {842, 190, 128, 17}, "Amount", 14, muted, QFont::Medium, right_);
        label(order, {810, 211, 160, 13}, "Before tax & discounts", 11, muted, QFont::Normal, right_);
        rule(order, 28, 233, 942, "#e3e3e3");
        rows_ = viewport(order, {28, 234, 942, 288});
        label(order, {28, 536, 210, 16}, "Contact (optional)", 13);
        contact_ = edit(order, {28, 557, 210, 39}, "Contact (optional)", "Enter contact");
        contact_->setValidator(new QRegularExpressionValidator(
            QRegularExpression(R"((?:\+?[0-9]{7,15})?)"), contact_));
        contact_->setToolTip("7–15 digits, with an optional leading +.");
        payment_ = std::make_unique<PaymentFields>(order, 256, 536, 210, 18, 258, "Merchant name");
        billError_ = label(order, {28, 501, 942, 30}, {}, 14, muted);
        billError_->setAlignment(Qt::AlignLeft | Qt::AlignTop);
        billError_->setWordWrap(true);
        rule(order, 28, 612, 942, "#e3e3e3");
        const QStringList totals{"Subtotal (before tax & discounts)", "Product discounts",
                                 "Seasonal discount", "Special discount", "Tax", "Rounding"};
        for (int i = 0; i < totals.size(); ++i) {
            label(order, {28, 621 + i * 20, 480, 17}, totals[i], 14, muted);
            totals_[i] = label(order, {760, 621 + i * 20, 210, 17}, {}, 14, ink, QFont::Normal, right_);
        }
        rule(order, 28, 746, 942, "#e3e3e3");
        label(order, {28, 753, 220, 24}, "Total payable", 20, ink, QFont::DemiBold);
        totals_[6] = label(order, {650, 753, 320, 24}, {}, 20, ink, QFont::Bold, right_);
        pay_ = action(order, {28, 801, 455, 44}, "Generate Bill", Action::Checkout);
        hold_ = action(order, {515, 801, 455, 44}, "Put on Hold", Action::Hold);

        auto* camera = panel(this, {1022, 0, 480, 350}, "#ffffff", "#e3e3e3");
        label(camera, {24, 24, 350, 24}, "Camera", 20, ink, QFont::DemiBold);
        label(camera, {24, 54, 350, 16}, "Camera preview", 13, muted);
        preview_ = new CameraFeed(camera, {24, 88, 432, 202});
        cameraIcon_ = new SvgIcon(preview_, {200, 62, 32, 32}, cameraSvg);
        previewStatus_ = label(preview_, {12, 110, 408, 32}, "Waiting for a barcode…",
                               13, muted, QFont::Medium, Qt::AlignHCenter | Qt::AlignTop);
        previewStatus_->setWordWrap(true);
        scannerMessage_ = label(camera, {24, 311, 432, 16}, "Scanning while Current Order is open", 13, muted);

        auto* product = panel(this, {1022, 370, 480, 588}, "#ffffff", "#e3e3e3");
        label(product, {24, 24, 430, 24}, "Scanned product", 20, ink, QFont::DemiBold);
        productName_ = label(product, {24, 68, 432, 39}, "Scan a barcode to select a product.",
                             18, ink, QFont::DemiBold, Qt::AlignLeft | Qt::AlignTop);
        productName_->setWordWrap(true);
        productPriceLabel_ = label(product, {24, 110, 215, 17}, "Price / item (excl. tax)", 14, muted);
        productPrice_ = label(product, {260, 110, 196, 17}, "—", 14, ink, QFont::Medium, right_);
        auto* description = action(product, {24, 205, 432, 38}, "Description  ▾", Action::Secondary);
        description->setCheckable(true);
        description->setChecked(true);
        auto* details = new QWidget(product);
        details->setGeometry(24, 250, 432, 242);
        connect(description, &QPushButton::toggled, details, &QWidget::setVisible);
        const QStringList detailLabels{"Product ID", "Acronym", "Barcode", "Measurement unit",
                                       "Available stock", "Unit price", "Tax rate", "Product discount"};
        for (int i = 0; i < detailLabels.size(); ++i) {
            const int y = 3 + i * 30;
            label(details, {0, y, 175, 20}, detailLabels[i], 14, muted);
            detailValues_[i] = label(details, {178, y, 254, 20}, "—", 14, ink, QFont::Medium, right_);
        }
        stockSummary_ = label(product, {24, 143, 432, 19}, {}, 14, ink, QFont::DemiBold);
        cartError_ = label(product, {24, 164, 432, 38}, {}, 13, errorInk);
        cartError_->setWordWrap(true);
        productPriceLabel_->hide();
        productPrice_->hide();
        setBillSaving(false);

        connect(pay_, &QPushButton::clicked, &window, &PosWindow::onGenerateBillClicked);
        connect(hold_, &QPushButton::clicked, &window, &PosWindow::onGenerateAndHoldClicked);
    }

    Result<Customer> readCustomer() const override
    {
        const auto name = name_->text().trimmed();
        if (name.isEmpty()) return {false, "Enter the customer's name.", {}};
        if (!contact_->hasAcceptableInput())
            return {false, "Contact must contain 7–15 digits, with an optional leading +.", {}};
        return {true, {}, {name.toStdString(), contact_->text().toStdString()}};
    }

    Result<Payment> readPayment() const override { return payment_->read(); }

    void resetCheckoutFields() override
    {
        name_->clear();
        contact_->clear();
        drafts_.clear();
        payment_->reset();
    }

    void showCameraPreview(const QImage& image) override
    {
        preview_->setFrame(image);
        cameraIcon_->setVisible(image.isNull());
        previewStatus_->setVisible(image.isNull());
        hasPreview_ = !image.isNull();
    }

    void showScannerMessage(const string& message) override
    {
        if (hasPreview_) {
            scannerMessage_->setText(message.empty() || message == "No barcode found."
                                     ? "Scanning while Current Order is open" : text(message));
        } else {
            const bool waiting = message.empty() || message == "Camera scanner started." ||
                                 message == "Waiting for a barcode…";
            previewStatus_->setText(waiting ? "Waiting for a barcode…" : text(message));
            scannerMessage_->setText(waiting ? "Scanning while Current Order is open" : "");
        }
    }

    void showScannedProduct(const ProductStock& item) override
    {
        const auto& product = item.product;
        stockSummary_->setText("Available stock   " + availability(item));
        productName_->setText(text(product.name));
        productName_->setToolTip(text(product.name));
        productPriceLabel_->setText("Price / " + unitShort(product.unit) + " (excl. tax)");
        productPrice_->setText("PKR " + formattedMoney(product.unitPrice));
        const QStringList details{text(product.id), text(product.acronym), text(product.barcode),
                                  unitName(product.unit), availability(item),
                                  "PKR " + formattedMoney(product.unitPrice) + " / " + unitShort(product.unit),
                                  fixedText(product.taxRate, 100, 2) + "%",
                                  "PKR " + formattedMoney(product.discountPerUnit)};
        for (int i = 0; i < details.size(); ++i) {
            detailValues_[i]->setText(details[i]);
            detailValues_[i]->setToolTip(details[i]);
        }
        productPriceLabel_->show();
        productPrice_->show();
    }

    void showCart(const vector<CartItem>& items, const vector<optional<int64_t>>& amounts,
                  const optional<Totals>& totals) override
    {
        for (auto it = drafts_.begin(); it != drafts_.end();) {
            const bool present = std::any_of(items.begin(), items.end(), [&it](const CartItem& item) { return item.product.id == it->first; });
            if (!present) it = drafts_.erase(it); else ++it;
        }
        string focused;
        int cursor = 0;
        for (const auto& [id, stepper] : steppers_) {
            if (stepper->editing()) {
                focused = id;
                cursor = stepper->cursorPosition();
                break;
            }
        }
        steppers_.clear();
        auto* content = newRows(*rows_, 942);
        int y = 0;
        long double units = 0;
        long double kilograms = 0;
        int committedRows = 0;
        for (size_t i = 0; i < items.size(); ++i) {
            const auto& item = items[i];
            auto* row = new QWidget(content);
            const auto& product = item.product;
            auto* identity = label(row, {0, 14, 330, 17}, {}, 14, ink, QFont::DemiBold);
            const int identityHeight = wrapHeight(*identity, text(product.name), 17);
            identity->setFixedHeight(identityHeight);
            label(row, {0, 18 + identityHeight, 330, 14}, text(product.acronym), 12, muted);
            label(row, {0, 36 + identityHeight, 330, 16}, text(product.barcode) + " · " +
                  unitName(product.unit), 13, muted);
            label(row, {362, 30, 90, 17}, formattedMoney(product.unitPrice), 14, ink, QFont::Normal, right_);
            label(row, {500, 30, 90, 17}, formattedMoney(product.discountPerUnit), 14, ink, QFont::Normal, right_);
            auto* stepper = new QuantityStepper(row, {626, 25, 152, 36});
            stepper->setQuantity(item.quantity, product.unit);
            steppers_[product.id] = stepper;
            if (drafts_.count(product.id)) stepper->restoreDraft(drafts_[product.id]);
            stepper->typing = [this, id = product.id, stepper] { drafts_[id] = stepper->draft(); };
            stepper->cancel = [this, id = product.id] {
                drafts_.erase(id);
                window_.cancelPendingQuantity(id);
            };
            stepper->changed = [this, id = product.id, stepper](int64_t value) {
                drafts_[id] = stepper->draft();
                window_.onCartQuantityChanged(id, value);
            };
            stepper->invalid = [this](const string& message) { showCartError(message); };
            const QString amount = item.quantity == 0 ? "Enter quantity"
                : i < amounts.size() && amounts[i] ? formattedMoney(*amounts[i]) : "—";
            label(row, {782, 30, 160, 17}, amount, 14, ink, QFont::Medium, right_);
            const int height = std::max(86, 69 + identityHeight);
            row->setGeometry(0, y, 942, height);
            rule(row, 0, height - 1, 942, "#e3e3e3");
            y += height;
            if (item.quantity > 0) ++committedRows;
            if (product.unit == Unit::Count) units += item.quantity / 1000;
            else kilograms += static_cast<long double>(item.quantity) / 1000;
        }
        if (items.empty()) {
            label(content, {0, 30, 850, 18}, "Your order is empty. Scan a product to get started.", 14, muted);
            y = 90;
        }
        finishRows(*rows_, *content, y);
        if (const auto editor = steppers_.find(focused); editor != steppers_.end()) {
            if (drafts_.count(focused)) editor->second->focusAt(cursor);
            else editor->second->select();
        }
        count_->setText(QString("%1 %2 · %3 %4%5").arg(committedRows)
            .arg(committedRows == 1 ? "product" : "products").arg(QString::number(static_cast<double>(units), 'f', 0))
            .arg(units == 1 ? "item" : "items")
            .arg(" · " + QString::number(static_cast<double>(kilograms), 'f', 3) + " kg"));
        const Totals displayed = totals.value_or(Totals{});
        totals_[0]->setText(totals ? formattedMoney(displayed.subtotal) : "—");
        totals_[1]->setText(totals ? formattedMoney(displayed.productDiscount) : "—");
        totals_[2]->setText(totals ? formattedMoney(displayed.seasonalDiscount) : "—");
        totals_[3]->setText(totals ? formattedMoney(displayed.specialDiscount) : "—");
        totals_[4]->setText(totals ? formattedMoney(displayed.tax) : "—");
        totals_[5]->setText(totals && displayed.roundingAdjustment ? formattedMoney(displayed.roundingAdjustment) : "—");
        totals_[6]->setText(totals ? "PKR " + formattedMoney(displayed.payable) : "—");
        if (committedRows == 0) payment_->clearAmount();
        else if (totals) payment_->setAmount(displayed.payable);
        canBill_ = committedRows > 0 && totals.has_value();
        setBillSaving(false);
    }

    void selectQuantity(const string& id) override
    {
        const auto next = steppers_.find(id);
        if (next == steppers_.end()) return;
        const auto* focused = QApplication::focusWidget();
        if (qobject_cast<const QLineEdit*>(focused) && !rows_->isAncestorOf(focused)) return;
        for (const auto& [rowId, stepper] : steppers_) {
            if (stepper->editing() && drafts_.count(rowId)) return;
        }
        rows_->ensureWidgetVisible(next->second);
        next->second->select();
    }
    void clearQuantityDraft(const string& id) override { drafts_.erase(id); }
    void setCustomer(const Customer& c) override { name_->setText(text(c.name)); contact_->setText(text(c.contact)); }
    void setCheckoutOrder(const SaleRecord* sale) override
    {
        orderIdentity_[0]->setText(sale ? text(sale->ids.billId) : "—");
        orderIdentity_[1]->setText(sale ? text(sale->ids.orderId) : "—");
        orderIdentity_[2]->setText(sale ? "Resumed from hold" : "In progress");
        discardResume_->setVisible(sale != nullptr);
    }
    bool hasDraft() const override { return !name_->text().isEmpty() || !contact_->text().isEmpty() || payment_->hasDraft() || !drafts_.empty(); }
    bool hasQuantityDraft() const override { return !drafts_.empty(); }
    void showCartError(const string& message) override { cartError_->setText(text(message)); }
    void showBillError(const string& message) override
    {
        billError_->setText(text(message));
        updateBillFeedback();
    }
    void setBillSaving(bool saving) override
    {
        pay_->setEnabled(!saving && canBill_);
        hold_->setEnabled(!saving && canBill_);
        if (saving) {
            billError_->setText("Saving order…");
        } else if (billError_->text() == "Saving order…") {
            billError_->clear();
        }
        updateBillFeedback();
    }

private:
    void updateBillFeedback()
    {
        rows_->setFixedHeight(billError_->text().isEmpty() ? 288 : 264);
    }

    static constexpr auto right_ = Qt::AlignRight | Qt::AlignVCenter;
    PosWindow& window_;
    std::map<string, QuantityStepper*> steppers_;
    std::map<string, QString> drafts_;
    QScrollArea* rows_ = nullptr;
    QLabel* count_ = nullptr;
    CameraFeed* preview_ = nullptr;
    QLabel* previewStatus_ = nullptr;
    QLabel* scannerMessage_ = nullptr;
    QLabel* productName_ = nullptr;
    QLabel* productPriceLabel_ = nullptr;
    QLabel* productPrice_ = nullptr;
    std::array<QLabel*, 8> detailValues_{};
    QLabel* cartError_ = nullptr;
    QLabel* billError_ = nullptr;
    std::array<QLabel*, 7> totals_{};
    std::array<QLabel*, 3> orderIdentity_{};
    QLabel* stockSummary_ = nullptr;
    SvgIcon* cameraIcon_ = nullptr;
    QLineEdit* name_ = nullptr;
    QLineEdit* contact_ = nullptr;
    QPushButton* pay_ = nullptr;
    QPushButton* hold_ = nullptr;
    QPushButton* discardResume_ = nullptr;
    std::unique_ptr<PaymentFields> payment_;
    bool hasPreview_ = false;
    bool canBill_ = false;
};

constexpr auto alignRight = Qt::AlignRight | Qt::AlignVCenter;

QPushButton* selectableRow(QWidget* parent, QRect bounds, const QString& name, bool selected)
{
    auto* row = new QPushButton(parent);
    row->setGeometry(bounds);
    row->setAccessibleName(name);
    row->setCursor(Qt::PointingHandCursor);
    row->setProperty("selected", selected);
    row->setAccessibleDescription(selected ? "Selected" : "");
    row->setStyleSheet(
        "QPushButton { border:1px solid transparent; background:transparent; border-radius:12px; }"
        "QPushButton:hover { background:#fafafa; }"
        "QPushButton[selected=true] { border-color:#dadada; background:#f6f6f6; }"
        "QPushButton:focus { border-color:#858585; }");
    return row;
}

QString billStatus(const SaleRecord& bill)
{
    if (bill.archived) return "Archived";
    if (bill.billChange == BillChange::CompleteRefund) return "Complete Refund";
    if (bill.billChange == BillChange::Modified) return "Modified";
    return statusText(bill.status);
}

QString paymentName(PaymentMode mode)
{
    switch (mode) {
    case PaymentMode::Cash: return "Cash";
    case PaymentMode::Card: return "Card";
    case PaymentMode::Wallet: return "Wallet";
    }
    return {};
}

Timestamp currentTimestamp()
{
    const auto now = QDateTime::currentDateTime();
    return {now.date().toString(Qt::ISODate).toStdString(),
            now.time().toString("HH:mm:ss").toStdString(), now.toSecsSinceEpoch()};
}

bool paidEditEligible(const SaleRecord& order)
{
    if (order.archived || order.status != OrderStatus::Paid ||
        order.billChange == BillChange::CompleteRefund || !order.paidAt) return false;
    const auto now = QDateTime::currentSecsSinceEpoch();
    return order.paidAt->epochSeconds >= 0 && now >= order.paidAt->epochSeconds &&
           now - order.paidAt->epochSeconds <= 604800;
}

class HistoryPage final : public PosWindow::OrderHistoryView {
public:
    explicit HistoryPage(PosWindow& window) : window_(window)
    {
        label(this, {0, -1, 600, 43}, "Order History", 36, detailInk, QFont::Medium);
        historyTab_ = action(this, {0, 61, 72, 32}, "History", Action::Text);
        archivesTab_ = action(this, {99, 61, 81, 32}, "Archives", Action::Text);
        rule(this, 0, 103, 1502);
        tabUnderline_ = new QWidget(this);
        tabUnderline_->setStyleSheet("background:#252525; border-radius:1px;");
        tabUnderline_->setAttribute(Qt::WA_TransparentForMouseEvents);
        connect(historyTab_, &QPushButton::clicked, this, [&window] { window.showHistoryTab(false); });
        connect(archivesTab_, &QPushButton::clicked, this, [&window] { window.showHistoryTab(true); });

        auto* list = panel(this, {0, 118, 650, 840});
        listTitle_ = label(list, {24, 23, 450, 24}, "Past orders", 20, detailInk, QFont::DemiBold);
        label(list, {24, 53, 530, 16}, "Select an order to view its saved bill", 13, detailMuted);
        rule(list, 24, 88, 602);
        orders_ = viewport(list, {16, 105, 618, 707});
        auto* bill = panel(this, {674, 118, 828, 840});
        bill_ = viewport(bill, {0, 0, 820, 677});
        recoveryWarning_ = label(bill, {28, 682, 772, 34}, {}, 13, "#806017");
        recoveryWarning_->setWordWrap(true);
        recoveryWarning_->setAlignment(Qt::AlignLeft | Qt::AlignTop);
        error_ = label(bill, {28, 719, 772, 45}, {}, 14, errorInk);
        error_->setAlignment(Qt::AlignLeft | Qt::AlignTop);
        error_->setWordWrap(true);
        archive_ = action(bill, {28, 773, 145, 42}, "Archive Order", Action::Secondary, true);
        pdf_ = action(bill, {182, 773, 192, 42}, "Open Bill PDF", Action::Secondary);
        resume_ = action(bill, {600, 773, 200, 42}, "Resume Order", Action::Primary);
        correction_ = action(bill, {600, 773, 200, 42}, "Edit Paid Quantities", Action::Primary);
        connect(resume_, &QPushButton::clicked, this, [this] {
            if (order_) window_.onResumeOrder(order_->ids.orderId);
        });
        connect(correction_, &QPushButton::clicked, this, &HistoryPage::beginEdit);
        connect(archive_, &QPushButton::clicked, this, [this] {
            if (!order_) return;
            const auto id = order_->ids.orderId;
            if (order_->archived) window_.onUnarchiveOrder(id);
            else window_.onArchiveOrder(id);
        });
        connect(pdf_, &QPushButton::clicked, this, [this] {
            if (!order_) return;
            if (canOpenPdf_) window_.onOpenPdf(order_->ids.orderId);
            else window_.onGeneratePdf(order_->ids.orderId);
        });
        setArchiveView(false);
        clearBill();
    }

    bool showingArchives() const override { return showingArchives_; }
    void setCashierActions(bool cashier) { cashier_ = cashier; }

    void setArchiveView(bool archived) override
    {
        showingArchives_ = archived;
        markSelected(*historyTab_, !archived);
        markSelected(*archivesTab_, archived);
        tabUnderline_->setGeometry(archived ? 99 : 0, 102, archived ? 63 : 54, 3);
        listTitle_->setText(archived ? "Archived orders" : "Past orders");
    }

    void showError(const string& message) override
    {
        error_->setStyleSheet(QString("color:%1; background:transparent;").arg(errorInk));
        error_->setText(text(message));
        error_->setToolTip(text(message));
    }

    void showNotice(const string& message) override
    {
        error_->setStyleSheet(QString("color:%1; background:transparent;").arg(detailMuted));
        error_->setText(text(message));
        error_->setToolTip(text(message));
    }

    void showPdfRecoveryWarning(const string& message) override
    {
        recoveryWarning_->setText(text(message));
        recoveryWarning_->setToolTip(text(message));
    }

    void showOrders(const vector<SaleRecord>& orders, const string& selectedId) override
    {
        auto* rows = newRows(*orders_, 618);
        int y = 0;
        for (const auto& order : orders) {
            const bool selected = selectedId == order.ids.orderId;
            auto* row = selectableRow(rows, {0, y, 618, 112}, text(order.customer.name), selected);
            const int x = selected ? 17 : 16;
            label(row, {x, 15, 340, 16}, "#" + text(order.ids.orderId), 13, detailInk, QFont::DemiBold);
            auto* customer = label(row, {x, 37, 340, 19}, {}, 16, detailInk);
            const int height = wrapHeight(*customer, text(order.customer.name), 19);
            customer->setFixedHeight(height);
            label(row, {x, 43 + height, 340, 16}, dateText(order.paidAt.value_or(order.createdAt)), 13, detailMuted);
            label(row, {408, 16, 192, 15}, billStatus(order), 13, detailMuted, QFont::Medium, alignRight);
            label(row, {382, 72, 218, 17}, "PKR " + formattedMoney(order.totals.payable), 14,
                  detailInk, QFont::DemiBold, alignRight);
            const int rowHeight = std::max(112, height + 75);
            row->resize(618, rowHeight);
            row->setProperty("orderId", text(order.ids.orderId));
            connect(row, &QPushButton::clicked, this, [this, id = order.ids.orderId] {
                window_.onOrderSelected(id);
                if (auto* current = orders_->widget()) {
                    for (auto* child : current->findChildren<QPushButton*>(QString{}, Qt::FindDirectChildrenOnly))
                        markSelected(*child, child->property("orderId").toString() == text(id));
                }
            });
            y += rowHeight;
            if (&order != &orders.back()) {
                rule(rows, 0, y + 6, 602);
                y += 13;
            }
        }
        if (orders.empty()) {
            label(rows, {32, 24, 540, 21}, showingArchives_ ? "No archived orders" : "No saved orders",
                  18, detailInk, QFont::DemiBold);
            y = 100;
        }
        finishRows(*orders_, *rows, y);
    }

    void showBill(const SaleRecord& order) override
    {
        order_ = order;
        resetEditor();
        auto* content = newRows(*bill_, 820);
        label(content, {28, 23, 500, 24}, "Saved Bill", 20, detailInk, QFont::DemiBold);
        const bool changed = order.billChange != BillChange::Unchanged;
        label(content, {552, 24, 248, 23}, billStatus(order), 14,
              changed ? (order.billChange == BillChange::CompleteRefund ? errorInk : "#806017") : detailMuted,
              QFont::DemiBold, alignRight);
        const QStringList names{"Bill ID", "Order ID", "Date and time"};
        const QStringList values{text(order.ids.billId), text(order.ids.orderId), dateText(order.paidAt.value_or(order.createdAt))};
        for (int i = 0; i < names.size(); ++i) {
            const int x = 28 + 197 * i;
            label(content, {x, 68, 190, 15}, names[i], 12, detailMuted);
            label(content, {x, 88, i == 2 ? 366 : 183, 18}, values[i], 14, detailInk);
        }
        auto* customer = label(content, {28, 124, 772, 18}, {}, 14, detailInk);
        const QString identity = order.customer.contact.empty() ? text(order.customer.name) :
            text(order.customer.name) + "   ·   " + text(order.customer.contact);
        const int customerHeight = wrapHeight(*customer, identity, 18);
        customer->setFixedHeight(customerHeight);
        int y = 142 + customerHeight;
        rule(content, 28, y, 772);
        const array<QRect, 5> columns{QRect(28, 0, 244, 18), QRect(278, 0, 128, 18),
            QRect(410, 0, 125, 18), QRect(540, 0, 124, 18), QRect(672, 0, 128, 18)};
        const QStringList headings{"Product", "Unit price", "Quantity", "Product discount", "Line tax"};
        for (int i = 0; i < headings.size(); ++i) {
            label(content, columns[i].translated(0, y + 14), headings[i], 12, detailMuted,
                  QFont::Medium, i == 0 ? Qt::AlignLeft | Qt::AlignVCenter : alignRight);
        }
        y += 48;
        for (const auto& line : order.lines) {
            auto* identityLabel = label(content, columns[0].translated(0, y), {}, 13, detailInk);
            const auto printable = text(line.name) + " · " + text(line.acronym);
            const int height = wrapHeight(*identityLabel, printable, 18);
            identityLabel->setFixedHeight(height);
            const QStringList row{formattedMoney(line.unitPrice),
                quantityText(line.quantity, line.unit) + " " + unitShort(line.unit),
                formattedMoney(line.lineProductDiscount), formattedMoney(line.lineTax)};
            for (int i = 0; i < row.size(); ++i) {
                label(content, columns[i + 1].translated(0, y), row[i], 13, detailInk, QFont::Normal, alignRight);
            }
            auto* rates = label(content, {28, y + height + 3, 650, 15},
                "Tax " + fixedText(line.taxRate, 100, 2) + "%   ·   Discount per unit PKR " +
                formattedMoney(line.discountPerUnit), 11, detailMuted);
            rates->setToolTip(text(line.barcode));
            y += height + 31;
        }
        rule(content, 28, y, 772);
        y += 12;
        const auto& totals = order.totals;
        const QStringList captions{"Subtotal", "Product discounts",
            "Seasonal discount (" + fixedText(order.terms.seasonalRate, 100, 2) + "%)",
            "Special discount (" + fixedText(order.terms.specialRate, 100, 2) + "%)",
            "Tax", "Rounding", "Total payable"};
        const array<int64_t, 7> figures{totals.subtotal, totals.productDiscount, totals.seasonalDiscount,
            totals.specialDiscount, totals.tax, totals.roundingAdjustment, totals.payable};
        for (size_t i = 0; i < figures.size(); ++i) {
            if (i == 5 && !figures[i]) continue;
            const bool total = i == figures.size() - 1;
            label(content, {420, y, 235, 20}, captions[static_cast<int>(i)], total ? 16 : 13,
                  total ? detailInk : detailMuted, total ? QFont::DemiBold : QFont::Normal);
            label(content, {650, y, 150, 20}, "PKR " + formattedMoney(figures[i]), total ? 16 : 13,
                  detailInk, total ? QFont::DemiBold : QFont::Normal, alignRight);
            y += total ? 35 : 23;
        }
        rule(content, 28, y, 772);
        y += 18;
        if (order.payment.recorded) {
            const auto& payment = order.payment;
            label(content, {28, y, 720, 23}, "Payment recorded", 16, detailInk, QFont::DemiBold);
            y += 28;
            const auto tender = paymentName(payment.mode) + "  ·  Received PKR " +
                formattedMoney(payment.amountReceived) + "  ·  Change PKR " + formattedMoney(payment.changeReturned);
            const QString merchant = payment.merchant.empty() ? QString() : "  ·  " + text(payment.merchant);
            auto* detail = label(content, {28, y, 772, 18}, {}, 13, detailMuted);
            const auto height = wrapHeight(*detail, tender + merchant, 18);
            detail->setFixedHeight(height);
            y += height + 13;
            for (const auto& adjustment : order.adjustments) {
                const QString direction = adjustment.amountDelta < 0 ? "Refund" :
                    adjustment.amountDelta > 0 ? "Additional collection" : "No payment difference";
                label(content, {28, y, 772, 18}, direction + "   ·   " + dateText(adjustment.at) +
                    "   ·   PKR " + formattedMoney(adjustment.amountDelta), 13, detailInk);
                y += 23;
            }
            if (!order.archived && order.billChange != BillChange::CompleteRefund && !paidEditEligible(order)) {
                label(content, {28, y + 8, 772, 22}, "The seven-day paid-edit window has closed.", 13, detailMuted);
                y += 34;
            }
        } else {
            label(content, {28, y, 720, 24}, "Payment is pending", 15, detailInk, QFont::DemiBold);
            y += 34;
        }
        finishRows(*bill_, *content, y + 12);
        archive_->setText(order.archived ? "Unarchive Order" : "Archive Order");
        archive_->show();
        archive_->setEnabled(archiveAvailable_);
        resume_->setVisible(cashier_ && !order.archived && order.status == OrderStatus::OnHold);
        correction_->setVisible(paidEditEligible(order));
    }

    void clearBill() override
    {
        order_.reset();
        resetEditor();
        auto* content = newRows(*bill_, 820);
        label(content, {28, 23, 772, 24}, "Saved Bill", 20, detailInk, QFont::DemiBold);
        label(content, {28, 70, 724, 21}, "No order selected", 18, detailInk, QFont::DemiBold);
        label(content, {28, 104, 724, 17}, "Select an order to view its saved bill.", 14, detailMuted);
        finishRows(*bill_, *content, 180);
        archive_->hide();
        resume_->hide();
        correction_->hide();
        pdf_->hide();
    }

    void showPaidEdit(const PaidEditDraft& draft) override
    {
        if (!summary_) return;
        settledDelta_ = draft.amountDelta;
        const QString direction = draft.amountDelta < 0 ? "Refund due" :
            draft.amountDelta > 0 ? "Additional amount due" : "No money movement";
        const bool full = order_ && std::all_of(order_->lines.begin(), order_->lines.end(),
            [&draft](const SaleLine& line) {
                const auto change = std::find_if(draft.changes.begin(), draft.changes.end(),
                    [&line](const QuantityChange& entry) { return entry.productId == line.productId; });
                return change == draft.changes.end() ? line.quantity == 0 : change->quantity == 0;
            });
        summary_->setText((full ? QString("Complete Refund\n") : QString()) +
            "Current payable  PKR " + formattedMoney(draft.before.payable) + "\nRevised payable  PKR " +
            formattedMoney(draft.after.payable) + "\n" + direction + "  PKR " +
            formattedMoney(qAbs(draft.amountDelta)) + "  ·  " + paymentName(order_->payment.mode));
        previewValid_ = true;
        synchronizeEditor();
    }

    void clearPaidEditApproval() override
    {
        previewValid_ = false;
        approved_ = false;
        if (summary_) summary_->clear();
        if (pin_) pin_->clear();
        synchronizeEditor();
    }

    void setPaidEditBusy(bool busy) override
    {
        editBusy_ = busy;
        synchronizeEditor();
    }

    void setPaidEditApproved(bool approved) override
    {
        approved_ = approved;
        if (pin_) pin_->clear();
        synchronizeEditor();
    }

    void setPdfActions(bool canGenerate, bool canOpen) override
    {
        canOpenPdf_ = canOpen;
        pdf_->setText(canOpen ? "Open Bill PDF" : "Generate Bill PDF");
        pdf_->setVisible(!editing_ && (canOpen || canGenerate));
    }

    void setArchiveActionsAvailable(bool available) override
    {
        archiveAvailable_ = available;
        archive_->setEnabled(available);
    }

    void showUnarchiveShortages(const vector<StockShortage>& shortages) override
    {
        if (!order_ || shortages.empty()) return;
        auto* content = newRows(*bill_, 820);
        auto* layout = new QVBoxLayout(content);
        layout->setContentsMargins(28, 22, 20, 22);
        auto* title = new QLabel("Order cannot be restored");
        title->setFont(interfaceFont(20, QFont::DemiBold));
        layout->addWidget(title);
        for (const auto& shortage : shortages) {
            auto* row = new QLabel(text(shortage.name) + "   ·   Required " +
                quantityText(shortage.required, shortage.unit) + " " + unitShort(shortage.unit) +
                "   ·   Available " + quantityText(shortage.available, shortage.unit) + " " + unitShort(shortage.unit));
            row->setTextFormat(Qt::PlainText);
            row->setWordWrap(true);
            row->setStyleSheet("color:#a22922;");
            layout->addWidget(row);
        }
        layout->addStretch();
        finishRows(*bill_, *content, std::max(200, layout->sizeHint().height()));
        showError("The order remains archived.");
    }

private:
    struct EditRow {
        SaleLine line;
        QLineEdit* quantity = nullptr;
        QLabel* restockLabel = nullptr;
        QLineEdit* restock = nullptr;
    };

    PosWindow& window_;
    optional<SaleRecord> order_;
    vector<EditRow> editRows_;
    QPushButton* historyTab_ = nullptr;
    QPushButton* archivesTab_ = nullptr;
    QWidget* tabUnderline_ = nullptr;
    QLabel* listTitle_ = nullptr;
    QScrollArea* orders_ = nullptr;
    QScrollArea* bill_ = nullptr;
    QLabel* recoveryWarning_ = nullptr;
    QLabel* error_ = nullptr;
    QPushButton* archive_ = nullptr;
    QPushButton* pdf_ = nullptr;
    QPushButton* resume_ = nullptr;
    QPushButton* correction_ = nullptr;
    QLabel* summary_ = nullptr;
    QLineEdit* pin_ = nullptr;
    QPushButton* approve_ = nullptr;
    QPushButton* commit_ = nullptr;
    QPushButton* cancel_ = nullptr;
    bool showingArchives_ = false;
    bool cashier_ = false;
    bool archiveAvailable_ = true;
    bool canOpenPdf_ = false;
    bool editing_ = false;
    bool previewValid_ = false;
    bool approved_ = false;
    bool editBusy_ = false;
    int64_t settledDelta_ = 0;

    void resetEditor()
    {
        editing_ = false;
        previewValid_ = false;
        approved_ = false;
        editBusy_ = false;
        summary_ = nullptr;
        pin_ = nullptr;
        approve_ = nullptr;
        commit_ = nullptr;
        cancel_ = nullptr;
        editRows_.clear();
    }

    void synchronizeEditor()
    {
        if (!editing_ || !pin_) return;
        for (const auto& row : editRows_) {
            row.quantity->setEnabled(!editBusy_);
            const auto quantity = quantityInput(row.quantity->text(), row.line.unit);
            const bool reduction = quantity.success && quantity.value < row.line.quantity;
            row.restockLabel->setVisible(reduction);
            row.restock->setVisible(reduction);
            row.restock->setEnabled(!editBusy_);
        }
        pin_->setEnabled(previewValid_ && !approved_ && !editBusy_);
        approve_->setEnabled(previewValid_ && !approved_ && !editBusy_);
        commit_->setEnabled(approved_ && !editBusy_);
        cancel_->setEnabled(!editBusy_);
    }

    void changePreview()
    {
        if (editBusy_ || !editing_ || !order_) return;
        vector<QuantityChange> edits;
        const auto id = order_->ids.orderId;
        for (const auto& line : editRows_) {
            const auto quantity = quantityInput(line.quantity->text(), line.line.unit);
            if (!quantity.success) {
                window_.cancelPaidEdit();
                showError("Enter a valid revised quantity.");
                return;
            }
            if (quantity.value == line.line.quantity) continue;
            const auto restock = quantity.value < line.line.quantity ?
                quantityInput(line.restock->text(), line.line.unit) : Result<int64_t>{true, {}, 0};
            if (!restock.success) {
                window_.cancelPaidEdit();
                showError("Enter a valid restock quantity, including zero if nothing can be returned.");
                return;
            }
            edits.push_back({line.line.productId, quantity.value, restock.value});
        }
        window_.onPreviewPaidEdit(id, edits);
    }

    void beginEdit()
    {
        if (!order_ || !paidEditEligible(*order_)) return;
        window_.cancelPaidEdit();
        showError({});
        editing_ = true;
        archive_->hide();
        pdf_->hide();
        correction_->hide();

        auto* content = newRows(*bill_, 820);
        auto* layout = new QVBoxLayout(content);
        layout->setContentsMargins(28, 22, 20, 22);
        auto* title = new QLabel("Edit Paid Quantities");
        title->setFont(interfaceFont(20, QFont::DemiBold));
        title->setStyleSheet("color:#242424;");
        layout->addWidget(title);
        auto* form = new QFormLayout;
        form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
        for (const auto& line : order_->lines) {
            auto* fields = new QWidget;
            auto* inputs = new QHBoxLayout(fields);
            inputs->setContentsMargins(0, 0, 0, 0);
            auto* quantity = new QLineEdit(quantityText(line.quantity, line.unit));
            auto* restock = new QLineEdit(quantityText(0, line.unit));
            quantity->setAccessibleName("Revised quantity for " + text(line.name));
            restock->setAccessibleName("Restock quantity for " + text(line.name));
            quantity->setValidator(new QRegularExpressionValidator(quantityPattern(line.unit), quantity));
            restock->setValidator(new QRegularExpressionValidator(quantityPattern(line.unit), restock));
            auto* restockLabel = new QLabel("Restock");
            inputs->addWidget(new QLabel("New qty"));
            inputs->addWidget(quantity);
            inputs->addWidget(restockLabel);
            inputs->addWidget(restock);
            auto* label = new QLabel(text(line.name) + "\n" + quantityText(line.quantity, line.unit) + " " + unitShort(line.unit));
            label->setTextFormat(Qt::PlainText);
            label->setWordWrap(true);
            form->addRow(label, fields);
            editRows_.push_back({line, quantity, restockLabel, restock});
            connect(quantity, &QLineEdit::textEdited, this, [this] { changePreview(); });
            connect(restock, &QLineEdit::textEdited, this, [this] { changePreview(); });
        }
        layout->addLayout(form);
        summary_ = new QLabel;
        summary_->setTextFormat(Qt::PlainText);
        summary_->setWordWrap(true);
        summary_->setMinimumHeight(82);
        layout->addWidget(summary_);

        auto* controls = new QHBoxLayout;
        pin_ = new QLineEdit;
        pin_->setEchoMode(QLineEdit::Password);
        pin_->setMaxLength(4);
        pin_->setPlaceholderText("Manager PIN");
        pin_->setAccessibleName("Manager PIN");
        pin_->setValidator(new QRegularExpressionValidator(QRegularExpression("[0-9]{4}"), pin_));
        approve_ = new QPushButton("Approve");
        commit_ = new QPushButton("Confirm Settlement");
        cancel_ = new QPushButton("Cancel");
        for (auto* button : {approve_, commit_, cancel_}) button->setMinimumHeight(39);
        controls->addWidget(pin_, 1);
        controls->addWidget(approve_);
        controls->addWidget(commit_);
        controls->addWidget(cancel_);
        layout->addLayout(controls);
        layout->addStretch();

        connect(approve_, &QPushButton::clicked, this, [this] { window_.onApprovePaidEdit(pin_->text().toStdString()); });
        connect(commit_, &QPushButton::clicked, this, [this] {
            if (!approved_ || editBusy_ || !order_) return;
            if (QMessageBox::question(this, "Confirm Settlement",
                "Confirm the approved payment adjustment has been settled.") == QMessageBox::Yes)
                window_.onCommitPaidEdit(settledDelta_);
        });
        connect(cancel_, &QPushButton::clicked, this, [this] {
            if (order_) {
                const auto id = order_->ids.orderId;
                window_.onOrderSelected(id);
            }
        });
        finishRows(*bill_, *content, std::max(600, layout->sizeHint().height()));
        synchronizeEditor();
    }
};

QString assetPath(const QString& filename)
{
    const QString candidate = QDir(QCoreApplication::applicationDirPath()).filePath("assets/" + filename);
    if (QFileInfo::exists(candidate)) return candidate;
#ifdef Q_OS_MACOS
    const QString bundled = QDir(QCoreApplication::applicationDirPath()).filePath("../Resources/assets/" + filename);
    if (QFileInfo::exists(bundled)) return bundled;
#endif
    return QDir(QString::fromUtf8(EYERIS_SOURCE_ASSET_DIR)).filePath(filename);
}

class Artwork final : public QWidget {
public:
    Artwork(QWidget* parent, const QString& filename, int radius)
        : QWidget(parent), image_(assetPath(filename)), radius_(radius) {}

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);
        QPainterPath clip;
        clip.addRoundedRect(rect(), radius_, radius_);
        painter.setClipPath(clip);
        painter.fillRect(rect(), QColor("#f5f5f5"));
        if (!image_.isNull()) {
            const QSize fitted = image_.size().scaled(size(), Qt::KeepAspectRatioByExpanding);
            painter.drawImage(QRect(QPoint((width() - fitted.width()) / 2,
                                           (height() - fitted.height()) / 2), fitted), image_);
        }
    }

private:
    QImage image_;
    int radius_;
};

QLineEdit* pinInput(QWidget* parent, QRect bounds, const QString& caption)
{
    auto* field = edit(parent, bounds, caption, caption);
    field->setEchoMode(QLineEdit::Password);
    field->setMaxLength(4);
    field->setValidator(new QRegularExpressionValidator(QRegularExpression("[0-9]{4}"), field));
    return field;
}

class LaunchPage final : public QWidget {
public:
    LaunchPage(QWidget* parent, PosWindow& window) : QWidget(parent)
    {
        setGeometry(0, 40, canvasWidth, canvasHeight - 40);
        auto* launch = panel(this, {310, 138, 1164, 698}, "#ffffff", "#e3e3e3");
        auto* brand = new Artwork(launch, "eyeris-background.png", 17);
        brand->setGeometry(36, 29, 455, 639);
        new SvgIcon(brand, {104, 289, 247, 70}, wordmarkSvg);

        pick_ = new QWidget(launch);
        pick_->setGeometry(570, 176, 526, 336);
        label(pick_, {0, 0, 526, 43}, "Choose a role", 36, ink, QFont::Medium);
        const auto card = [this, &window](Role role, const QString& name, const QString& detail, int y) {
            auto* hit = action(pick_, {0, y, 526, 94}, {}, Action::Secondary);
            hit->setAccessibleName(name);
            label(hit, {20, 20, 440, 24}, name, 20, ink, QFont::DemiBold);
            label(hit, {20, 54, 440, 16}, detail, 14, muted);
            label(hit, {478, 31, 24, 29}, "→", 24, ink);
            connect(hit, &QPushButton::clicked, &window, [&window, role] { window.onRoleSelected(role); });
        };
        card(Role::Cashier, "Cashier", "Scan products and take payments.", 109);
        card(Role::Manager, "Manager", "Manage products, orders and reports.", 219);

        pinPanel_ = new QWidget(launch);
        pinPanel_->setGeometry(570, 176, 526, 365);
        pinHeading_ = label(pinPanel_, {0, 0, 526, 43}, "Manager PIN", 36, ink, QFont::Medium);
        pinHint_ = label(pinPanel_, {0, 55, 526, 31}, {}, 14, muted);
        pin_ = pinInput(pinPanel_, {0, 111, 402, 40}, "Manager PIN");
        confirm_ = pinInput(pinPanel_, {0, 166, 402, 40}, "Confirm Manager PIN");
        auto* back = action(pinPanel_, {0, 258, 112, 42}, "Back", Action::Secondary);
        submit_ = action(pinPanel_, {355, 258, 170, 42}, "Continue", Action::Primary);
        connect(back, &QPushButton::clicked, this, &LaunchPage::showPicker);
        const auto submit = [this] {
            if (submitted) submitted(pin_->text().toStdString(), confirm_->text().toStdString(), initialize_);
        };
        connect(submit_, &QPushButton::clicked, this, submit);
        connect(pin_, &QLineEdit::returnPressed, this, submit);
        connect(confirm_, &QLineEdit::returnPressed, this, submit);
        feedback_ = label(launch, {570, 512, 526, 55}, {}, 14, errorInk);
        feedback_->setAlignment(Qt::AlignLeft | Qt::AlignTop);
        feedback_->setWordWrap(true);
        showPicker();
    }

    function<void(const string&, const string&, bool)> submitted;

    void showPicker()
    {
        pick_->show();
        pinPanel_->hide();
        pin_->clear();
        confirm_->clear();
        showFeedback({});
    }

    void showPin(bool initialize)
    {
        initialize_ = initialize;
        pick_->hide();
        pinPanel_->show();
        pinHeading_->setText(initialize ? "Set Manager PIN" : "Manager PIN");
        pinHint_->setText(initialize ? "Choose a four-digit PIN for manager access and paid edits." :
            "Enter the four-digit PIN to open Manager.");
        confirm_->setVisible(initialize);
        submit_->setText(initialize ? "Set PIN" : "Continue");
        showFeedback({});
        pin_->setFocus();
    }

    void setPending(bool pending)
    {
        pick_->setEnabled(!pending);
        pinPanel_->setEnabled(!pending);
        if (pending) showFeedback("Checking Manager access…");
    }

    void showFeedback(const string& message) { feedback_->setText(text(message)); }

    void clearPin()
    {
        pin_->clear();
        confirm_->clear();
    }

private:
    QWidget* pick_ = nullptr;
    QWidget* pinPanel_ = nullptr;
    QLabel* pinHeading_ = nullptr;
    QLabel* pinHint_ = nullptr;
    QLabel* feedback_ = nullptr;
    QLineEdit* pin_ = nullptr;
    QLineEdit* confirm_ = nullptr;
    QPushButton* submit_ = nullptr;
    bool initialize_ = false;
};

constexpr array<const char*, 5> pageNames{
    "Current Order", "Order History", "Products & Discounts", "Reports", "Settings"
};

class TitleBar final : public QWidget {
public:
    TitleBar(QWidget* parent, PosWindow& window) : QWidget(parent), window_(window)
    {
        setGeometry(0, 0, canvasWidth, 40);
        rule(this, 0, 39, canvasWidth, "#e3e3e3");
        label(this, {0, 12, canvasWidth, 16}, "Eyeris", 13, muted, QFont::Medium, Qt::AlignCenter);
        const std::array<const char*, 3> names{"Close", "Minimize", "Zoom"};
        const std::array<const char*, 3> icons{closeSvg, minimizeSvg, zoomSvg};
        for (int i = 0; i < 3; ++i) {
            auto* button = new QPushButton(this);
            button->setGeometry(16 + 20 * i, 10, 20, 20);
            button->setStyleSheet(
                "QPushButton {border:1px solid transparent; background:transparent; border-radius:8px;}"
                "QPushButton:focus {border-color:#858585;}");
            button->setAccessibleName(names[i]);
            new SvgIcon(button, {4, 4, 12, 12}, icons[i]);
            connect(button, &QPushButton::clicked, this, [this, i] {
                if (i == 0) window_.close();
                else if (i == 1) window_.showMinimized();
                else toggleZoom();
            });
        }
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        QPainterPath shape;
        shape.addRoundedRect(QRect(0, 0, width(), 64), 16, 16);
        painter.fillPath(shape, QColor("#f5f5f5"));
    }

    void mousePressEvent(QMouseEvent* event) override
    {
        if (event->button() == Qt::LeftButton && window_.windowHandle())
            window_.windowHandle()->startSystemMove();
    }

    void mouseDoubleClickEvent(QMouseEvent* event) override
    {
        if (event->button() == Qt::LeftButton) toggleZoom();
    }

private:
    void toggleZoom()
    {
        if (window_.isMaximized()) window_.showNormal();
        else window_.showMaximized();
    }

    PosWindow& window_;
};

class PosViewport final : public QScrollArea {
public:
    explicit PosViewport(QWidget* content)
    {
        setWidget(content);
        setFrameShape(QFrame::NoFrame);
        setAlignment(Qt::AlignHCenter | Qt::AlignVCenter);
        setStyleSheet("QScrollArea {background:transparent; border:none;}");
        viewport()->setAutoFillBackground(false);
    }
};

} // namespace

PosWindow::PosWindow(ProductService& products, DiscountService& discounts, CartService& cart,
                     PosScanner& scanner, CheckoutService& checkout, HistoryService& history,
                     const ReportService& reports, ManagerPinService& access, QWidget* parent)
    : QMainWindow(parent), products_(products), discounts_(discounts), cart_(cart),
      scanner_(scanner), checkout_(checkout), history_(history), reports_(reports), access_(access)
{
    setWindowTitle("Eyeris");
    setWindowFlag(Qt::FramelessWindowHint);
    setAttribute(Qt::WA_TranslucentBackground);
    auto* shell = new Artwork(nullptr, "eyeris-app-background.png", 16);
    shell->setObjectName("eyerisCanvas");
    shell->setFixedSize(canvasWidth, canvasHeight);
    new TitleBar(shell, *this);
    sidebar_ = new Artwork(shell, "eyeris-sidebar-background.png", 21);
    sidebar_->setGeometry(24, 64, 210, 958);
    auto* wordmark = new SvgIcon(sidebar_, {19, 29, 168, 48}, wordmarkSvg);
    wordmark->setObjectName("eyerisWordmark");
    wordmark->setAccessibleName("Eyeris");
    for (const char* name : pageNames) {
        auto* link = action(sidebar_, {11, 99, 186, 42}, QString::fromLatin1(name), Action::Navigation);
        navigation_.push_back(link);
        connect(link, &QPushButton::clicked, this, [this, destination = string(name)] { showPage(destination); });
    }
    auto* switchRole = action(sidebar_, {11, 888, 186, 42}, "Switch Role", Action::Navigation);
    connect(switchRole, &QPushButton::clicked, this, &PosWindow::showRolePicker);

    pages_ = new QStackedWidget(shell);
    pages_->setGeometry(pageLeft, 64, pageWidth, 958);
    currentOrderPage_ = new CurrentOrderPage(*this);
    historyPage_ = new HistoryPage(*this);
    cataloguePage_ = new ManagerCataloguePage(products_, discounts_);
    reportsPage_ = new ReportsPage;
    reportsPage_->setGenerateAction([this](const ReportRequest& request) { onGenerateReport(request); });
    settingsPage_ = new QWidget;
    label(settingsPage_, {0, -1, 616, 43}, "Settings", 36, ink, QFont::Medium);
    label(settingsPage_, {0, 45, 800, 16}, "Manage access to this terminal.", 14, muted);
    auto* settings = panel(settingsPage_, {0, 81, 816, 596});
    label(settings, {28, 28, 760, 24}, "Change PIN", 20, ink, QFont::DemiBold);
    label(settings, {28, 61, 760, 16}, "Use four digits. The new PIN also applies to paid-bill approvals.", 14, muted);
    label(settings, {28, 146, 402, 16}, "Current PIN", 14, ink);
    auto* currentPin = pinInput(settings, {28, 169, 402, 40}, "Current PIN");
    label(settings, {28, 237, 402, 16}, "New PIN", 14, ink);
    auto* newPin = pinInput(settings, {28, 260, 402, 40}, "New PIN");
    label(settings, {28, 328, 402, 16}, "Confirm new PIN", 14, ink);
    auto* confirm = pinInput(settings, {28, 351, 402, 40}, "Confirm new PIN");
    settingsFeedback_ = label(settings, {28, 401, 760, 39}, {}, 14, errorInk);
    settingsFeedback_->setWordWrap(true);
    rule(settings, 28, 445, 760);
    auto* back = action(settings, {28, 481, 120, 42}, "Back", Action::Secondary);
    changePin_ = action(settings, {631, 481, 156, 42}, "Save PIN", Action::Primary);
    connect(back, &QPushButton::clicked, this, [this] { showPage("Products & Discounts"); });
    connect(changePin_, &QPushButton::clicked, this, [this, currentPin, newPin, confirm] {
        onChangePin(currentPin->text().toStdString(), newPin->text().toStdString(), confirm->text().toStdString());
    });

    pages_->addWidget(currentOrderPage_);
    pages_->addWidget(historyPage_);
    pages_->addWidget(cataloguePage_);
    pages_->addWidget(reportsPage_);
    pages_->addWidget(settingsPage_);
    connect(pages_, &QStackedWidget::currentChanged, this, [this] { updateNavigation(); });
    auto* launch = new LaunchPage(shell, *this);
    launch->submitted = [this](const string& pin, const string& confirmation, bool initialize) {
        submitManagerPin(pin, confirmation, initialize);
    };
    launchPage_ = launch;
    setCentralWidget(new PosViewport(shell));
    if (const auto* screen = QGuiApplication::primaryScreen()) {
        const auto available = screen->availableGeometry().adjusted(8, 8, -8, -8);
        const QSize size(std::min(canvasWidth, available.width()), std::min(canvasHeight, available.height()));
        resize(size);
        move(available.center() - QPoint(size.width() / 2, size.height() / 2));
    } else {
        resize(canvasWidth, canvasHeight);
    }

    cameraTimer_.setInterval(200);
    cameraTimer_.setTimerType(Qt::CoarseTimer);
    connect(&cameraTimer_, &QTimer::timeout, this, &PosWindow::onCameraTick);
    showRolePicker();
}

void PosWindow::onRoleSelected(Role role)
{
    if (busy_ || role_) return;
    if (role == Role::Cashier) {
        enterRole(role);
        return;
    }
    auto* launch = static_cast<LaunchPage*>(launchPage_);
    busy_ = true;
    launch->setPending(true);
    access_.hasPin([window = QPointer<PosWindow>(this)](Result<bool> result) {
        if (!window) return;
        window->busy_ = false;
        auto* launch = static_cast<LaunchPage*>(window->launchPage_);
        launch->setPending(false);
        if (result.success) launch->showPin(!result.value);
        else launch->showFeedback(result.message);
    });
}

void PosWindow::submitManagerPin(const string& pin, const string& confirmation, bool initialize)
{
    if (busy_ || role_) return;
    auto* launch = static_cast<LaunchPage*>(launchPage_);
    if (initialize && pin != confirmation) {
        launch->showFeedback("The two PINs must match.");
        return;
    }
    busy_ = true;
    launch->setPending(true);
    auto completed = [window = QPointer<PosWindow>(this)](Status result) {
        if (!window) return;
        window->busy_ = false;
        auto* launch = static_cast<LaunchPage*>(window->launchPage_);
        launch->setPending(false);
        launch->clearPin();
        if (result.success) window->enterRole(Role::Manager);
        else launch->showFeedback(result.message);
    };
    if (initialize) access_.initializePin(pin, std::move(completed));
    else access_.verifyPin(pin, std::move(completed));
}

void PosWindow::enterRole(Role role)
{
    role_ = role;
    static_cast<HistoryPage*>(historyPage_)->setCashierActions(role == Role::Cashier);
    launchPage_->hide();
    sidebar_->show();
    pages_->show();
    showPage(role == Role::Cashier ? "Current Order" : "Products & Discounts");
}

void PosWindow::showRolePicker()
{
    if (busy_) return;
    stopCurrentOrderCamera();
    clearOrderSelection();
    role_.reset();
    pages_->hide();
    sidebar_->hide();
    for (auto* field : settingsPage_->findChildren<QLineEdit*>()) field->clear();
    settingsFeedback_->clear();
    auto* launch = static_cast<LaunchPage*>(launchPage_);
    launch->showPicker();
    launch->show();
    launch->raise();
}

void PosWindow::updateNavigation()
{
    int y = 99;
    for (size_t i = 0; i < navigation_.size(); ++i) {
        auto* link = navigation_[i];
        const bool available = role_ && (*role_ == Role::Cashier ? i <= 1 : i >= 1);
        link->setVisible(available);
        if (!available) continue;
        link->move(11, y);
        y += 52;
        const bool active = static_cast<int>(i) == pages_->currentIndex();
        markSelected(*link, active);
        link->setFont(interfaceFont(14, active ? QFont::DemiBold : QFont::Normal));
    }
}

void PosWindow::onChangePin(const string& currentPin, const string& newPin, const string& confirmation)
{
    if (busy_ || role_ != Role::Manager) return;
    if (newPin != confirmation) {
        settingsFeedback_->setStyleSheet("color:#a22922;");
        settingsFeedback_->setText("The new PIN and confirmation must match.");
        return;
    }
    busy_ = true;
    changePin_->setEnabled(false);
    for (auto* field : settingsPage_->findChildren<QLineEdit*>()) field->setEnabled(false);
    settingsFeedback_->setText("Saving PIN…");
    access_.changePin(currentPin, newPin, [window = QPointer<PosWindow>(this)](Status result) {
        if (!window) return;
        window->busy_ = false;
        window->changePin_->setEnabled(true);
        for (auto* field : window->settingsPage_->findChildren<QLineEdit*>()) {
            field->setEnabled(true);
            field->clear();
        }
        window->settingsFeedback_->setStyleSheet(result.success ? "color:#225f45;" : "color:#a22922;");
        window->settingsFeedback_->setText(result.success ? "Manager PIN changed." : text(result.message));
    });
}

void PosWindow::onCameraTick()
{
    const auto scan = scanner_.poll();
    if (!scan.success) {
        stopCurrentOrderCamera();
        currentOrderPage_->showCameraPreview({});
        currentOrderPage_->showScannerMessage(scan.message);
        return;
    }
    currentOrderPage_->showCameraPreview(mat2QImage(scan.value.preview));
    if (!scan.value.decoded.found) {
        currentOrderPage_->showScannerMessage(scan.message);
        return;
    }
    if (scan.value.codeChanged && !choosingHeldOrder_ && !busy_) addScannedProduct(scan.value.decoded.code);
    else if (scan.value.decoded.code == scannedBarcode_) currentOrderPage_->showScannerMessage({});
}

void PosWindow::addScannedProduct(const string& barcode)
{
    const auto product = products_.findWithStockByBarcode(barcode, resumedOrderId_);
    if (!product.success) {
        currentOrderPage_->showScannerMessage(product.message);
        return;
    }
    scannedBarcode_ = barcode;
    currentOrderPage_->showScannerMessage({});
    currentOrderPage_->showScannedProduct(product.value);
    const auto& current = product.value.product;
    const auto added = cart_.addByBarcode(barcode, 1000);
    const bool samePending = pendingProduct_ && pendingProduct_->id == current.id;
    if (added.success && samePending) pendingProduct_.reset();
    if (!added.success && product.value.stock.available > 0 && (!pendingProduct_ || samePending)) {
        const auto items = cart_.items();
        const bool present = std::any_of(items.begin(), items.end(), [&current](const CartItem& item) {
            return item.product.id == current.id;
        });
        if (!present) pendingProduct_ = current;
    }
    const auto error = refreshCart();
    currentOrderPage_->showCartError(added.success ? error : added.message);
    currentOrderPage_->selectQuantity(current.id);
    if (added.success) QApplication::beep();
}

void PosWindow::onCartQuantityChanged(const string& productId, int64_t quantity)
{
    if (busy_) return;
    const bool pending = pendingProduct_ && pendingProduct_->id == productId;
    if (pending && quantity == 0) {
        cancelPendingQuantity(productId);
        return;
    }
    const auto result = pending ? cart_.addByBarcode(pendingProduct_->barcode, quantity)
        : cart_.setQuantity(productId, quantity);
    if (result.success) currentOrderPage_->clearQuantityDraft(productId);
    if (pending && result.success) {
        pendingProduct_.reset();
        QApplication::beep();
    }
    const auto error = refreshCart();
    currentOrderPage_->showCartError(result.success ? error : result.message);
    if (!result.success) currentOrderPage_->selectQuantity(productId);
}

string PosWindow::refreshCart()
{
    auto items = cart_.items();
    vector<optional<int64_t>> amounts(items.size());
    string error;
    currentQuote_.reset();
    if (!items.empty()) {
        const auto quote = checkout_.quote(items, resumedOrderId_, currentTimestamp());
        if (quote.success) {
            currentQuote_ = quote.value;
            for (size_t i = 0; i < amounts.size(); ++i) {
                amounts[i] = quote.value.lineSubtotals[i];
                const auto& line = quote.value.lines[i];
                items[i].product.unitPrice = line.unitPrice;
                items[i].product.discountPerUnit = line.discountPerUnit;
                items[i].product.taxRate = line.taxRate;
            }
        } else {
            error = quote.message;
        }
    }
    if (pendingProduct_) {
        items.push_back({*pendingProduct_, 0});
        amounts.push_back(nullopt);
        if (error.empty()) error = "Enter an available quantity for " + pendingProduct_->name + ".";
    }
    currentOrderPage_->showCart(items, amounts, currentQuote_ ? optional<Totals>{currentQuote_->totals} : nullopt);
    return error;
}

void PosWindow::cancelPendingQuantity(const string& productId)
{
    currentOrderPage_->clearQuantityDraft(productId);
    if (!pendingProduct_ || pendingProduct_->id != productId) return;
    pendingProduct_.reset();
    currentOrderPage_->showCartError(refreshCart());
}

void PosWindow::onHeldOrdersClicked()
{
    if (busy_ || choosingHeldOrder_ || role_ != Role::Cashier) return;
    if (!cart_.items().empty() || pendingProduct_ || currentOrderPage_->hasDraft() || !resumedOrderId_.empty()) {
        currentOrderPage_->showBillError("Finish or hold the active order before opening a held order.");
        return;
    }
    const auto orders = history_.list();
    if (!orders.success) { currentOrderPage_->showBillError(orders.message); return; }
    QDialog picker(this);
    picker.setWindowTitle("Held Orders");
    picker.resize(480, 320);
    auto* layout = new QVBoxLayout(&picker);
    layout->addWidget(new QLabel("Select an order to resume."));
    auto* list = new QListWidget;
    list->setAccessibleName("Held orders");
    layout->addWidget(list);
    for (const auto& order : orders.value) {
        if (order.status != OrderStatus::OnHold) continue;
        auto* row = new QListWidgetItem(text(order.customer.name) + "  ·  #" + text(order.ids.orderId) +
            "  ·  PKR " + formattedMoney(order.totals.payable), list);
        row->setData(Qt::UserRole, text(order.ids.orderId));
    }
    if (list->count() == 0) {
        currentOrderPage_->showBillError("No held orders.");
        return;
    }
    QString orderId;
    const auto choose = [&](QListWidgetItem* row) {
        orderId = row->data(Qt::UserRole).toString();
        picker.accept();
    };
    connect(list, &QListWidget::itemClicked, &picker, choose);
    connect(list, &QListWidget::itemActivated, &picker, choose);
    const QScopedValueRollback<bool> choosing(choosingHeldOrder_, true);
    if (picker.exec() == QDialog::Accepted && !orderId.isEmpty()) onResumeOrder(orderId.toStdString());
}

void PosWindow::onResumeOrder(const string& orderId)
{
    if (busy_ || !role_) return;
    const auto notice = [this](const string& value) {
        if (pages_->currentWidget() == historyPage_) historyPage_->showError(value);
        else currentOrderPage_->showBillError(value);
    };
    if (*role_ != Role::Cashier) {
        notice("Switch to Cashier to resume a held order.");
        return;
    }
    if (!cart_.items().empty() || pendingProduct_ || currentOrderPage_->hasDraft() || !resumedOrderId_.empty()) {
        notice("Finish or hold the active order before opening a held order.");
        return;
    }
    const auto held = checkout_.resumeHeldOrder(orderId);
    if (!held.success) { notice(held.message); return; }
    cart_.restoreHeldItems(held.value);
    resumedOrderId_ = held.value.sale.ids.orderId;
    currentOrderPage_->setCheckoutOrder(&held.value.sale);
    currentOrderPage_->setCustomer(held.value.sale.customer);
    showPage("Current Order");
    const auto error = refreshCart();
    currentOrderPage_->showCartError(error);
    currentOrderPage_->showBillError(error.empty() ? "Resumed with current prices and discounts." : error);
}

void PosWindow::onGenerateBillClicked() { generateCurrentBill(OrderStatus::Paid); }

void PosWindow::onGenerateAndHoldClicked() { generateCurrentBill(OrderStatus::OnHold); }

void PosWindow::onDiscardResumedOrder()
{
    if (busy_ || resumedOrderId_.empty() || role_ != Role::Cashier) return;
    const QScopedValueRollback<bool> confirming(busy_, true);
    if (QMessageBox::question(this, "Discard Resumed Draft",
        "Discard unsaved changes and return to an empty order? The saved order is kept in History.",
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) return;
    clearCurrentOrder();
    currentOrderPage_->showBillError({});
}

void PosWindow::clearCurrentOrder()
{
    cart_.clear();
    resumedOrderId_.clear();
    pendingProduct_.reset();
    currentOrderPage_->setCheckoutOrder(nullptr);
    currentOrderPage_->resetCheckoutFields();
    currentOrderPage_->showCartError(refreshCart());
}

void PosWindow::generateCurrentBill(OrderStatus status)
{
    if (busy_ || role_ != Role::Cashier) return;
    auto& view = *currentOrderPage_;
    if (pendingProduct_ || view.hasQuantityDraft()) {
        view.showBillError("Press Enter to apply the quantity, or Escape to cancel it.");
        return;
    }
    const auto customer = view.readCustomer();
    if (!customer.success) { view.showBillError(customer.message); return; }
    Payment payment;
    optional<int64_t> expectedPayable;
    if (status == OrderStatus::Paid) {
        if (!currentQuote_) { view.showBillError("Review the current payable before saving."); return; }
        const auto entered = view.readPayment();
        if (!entered.success) { view.showBillError(entered.message); return; }
        payment = entered.value;
        expectedPayable = currentQuote_->totals.payable;
    }
    view.setBillSaving(true);
    const auto now = currentTimestamp();
    const auto sale = [&] {
        const QScopedValueRollback<bool> saving(busy_, true);
        const auto items = cart_.items();
        return resumedOrderId_.empty() ?
            checkout_.generateBill(items, customer.value, payment, status, now, expectedPayable) :
            checkout_.saveHeldOrder(resumedOrderId_, items, customer.value, payment, status, now, expectedPayable);
    }();
    if (!sale.success) {
        view.showCartError(refreshCart());
        view.showBillError(sale.message);
        view.setBillSaving(false);
        return;
    }
    clearCurrentOrder();
    view.setBillSaving(false);
    if (status == OrderStatus::OnHold) {
        view.showBillError(sale.message.empty() ? "Order saved on hold." : sale.message);
        return;
    }
    view.showBillError({});
    historyPage_->setArchiveView(false);
    showPage("Order History");
    displayOrder(sale.value);
    refreshHistory();
    historyPage_->showNotice(sale.message);
}

void PosWindow::onOrderSelected(const string& orderId)
{
    if (busy_ || !role_) return;
    const auto result = history_.get(orderId);
    if (result.success) displayOrder(result.value);
    else {
        clearOrderSelection();
        historyPage_->showError(result.message);
    }
}

void PosWindow::displayOrder(const SaleRecord& order)
{
    cancelPaidEdit();
    selectedOrderId_ = order.ids.orderId;
    displayedBill_ = order;
    historyPage_->showError({});
    historyPage_->showBill(order);
    historyPage_->setArchiveActionsAvailable(pdfRecoveryWarning_.empty());
    const bool canUsePdf = pdfRecoveryWarning_.empty() && !order.archived && order.status == OrderStatus::Paid;
    const bool canOpenPdf = canUsePdf && history_.openPdfPath(order.ids.orderId).success;
    historyPage_->setPdfActions(canUsePdf && !canOpenPdf, canOpenPdf);
}

void PosWindow::clearOrderSelection()
{
    cancelPaidEdit();
    selectedOrderId_.clear();
    displayedBill_.reset();
    historyPage_->showError({});
    historyPage_->clearBill();
}

void PosWindow::onPreviewPaidEdit(const string& orderId, const vector<QuantityChange>& changes)
{
    if (busy_ || selectedOrderId_ != orderId || !role_) return;
    cancelPaidEdit();
    if (changes.empty()) {
        historyPage_->showError({});
        return;
    }
    const auto preview = checkout_.previewPaidEdit(orderId, changes, currentTimestamp());
    if (!preview.success) { historyPage_->showError(preview.message); return; }
    paidEditDraft_ = preview.value;
    historyPage_->showError({});
    historyPage_->showPaidEdit(*paidEditDraft_);
}

void PosWindow::onApprovePaidEdit(const string& pin)
{
    if (busy_ || !paidEditDraft_ || !role_ || pages_->currentWidget() != historyPage_) return;
    const auto draft = *paidEditDraft_;
    busy_ = true;
    paidEditApproval_.reset();
    historyPage_->setPaidEditBusy(true);
    historyPage_->showNotice("Checking Manager PIN…");
    access_.approvePaidEdit(pin, draft, [window = QPointer<PosWindow>(this), draft](Result<ManagerApproval> approval) {
        if (!window) return;
        window->busy_ = false;
        auto* historyView = window->historyPage_;
        historyView->setPaidEditBusy(false);
        if (window->selectedOrderId_ != draft.orderId || !window->paidEditDraft_ ||
            window->paidEditDraft_->revision != draft.revision || window->pages_->currentWidget() != historyView) return;
        if (!approval.success) {
            historyView->setPaidEditApproved(false);
            historyView->showError(approval.message);
            return;
        }
        window->paidEditApproval_ = approval.value;
        historyView->setPaidEditApproved(true);
        historyView->showNotice("Manager approved this adjustment. Confirm settlement after handling the payment.");
    });
}

void PosWindow::onCommitPaidEdit(int64_t settledDelta)
{
    if (busy_ || !paidEditDraft_ || !paidEditApproval_ || pages_->currentWidget() != historyPage_) return;
    const auto result = [&] {
        const QScopedValueRollback<bool> settling(busy_, true);
        historyPage_->setPaidEditBusy(true);
        return checkout_.applyPaidEdit(*paidEditDraft_, *paidEditApproval_, settledDelta, currentTimestamp());
    }();
    historyPage_->setPaidEditBusy(false);
    paidEditApproval_.reset();
    historyPage_->setPaidEditApproved(false);
    if (!result.success) {
        historyPage_->showError(result.message + " Reconcile any money already moved before retrying settlement.");
        return;
    }
    displayOrder(result.value);
    refreshHistory();
    historyPage_->showNotice(result.message);
}

void PosWindow::cancelPaidEdit()
{
    if (busy_) return;
    paidEditDraft_.reset();
    paidEditApproval_.reset();
    historyPage_->clearPaidEditApproval();
}

void PosWindow::onArchiveOrder(const string& orderId)
{
    if (busy_ || !displayedBill_ || displayedBill_->archived || selectedOrderId_ != orderId ||
        !pdfRecoveryWarning_.empty()) return;
    if (displayedBill_->status == OrderStatus::Paid &&
        QMessageBox::question(this, "Archive Paid Bill",
            "Archive this bill and remove its saved PDF? A restored bill can generate a new PDF.") != QMessageBox::Yes) return;
    const auto result = history_.archive(orderId);
    if (!result.success) { historyPage_->showError(result.message); return; }
    clearOrderSelection();
    refreshHistory();
    if (!result.message.empty()) historyPage_->showNotice(result.message);
}

void PosWindow::onUnarchiveOrder(const string& orderId)
{
    if (busy_ || !displayedBill_ || !displayedBill_->archived || selectedOrderId_ != orderId ||
        !pdfRecoveryWarning_.empty()) return;
    const auto shortages = history_.unarchiveShortages(orderId);
    if (!shortages.success) { historyPage_->showError(shortages.message); return; }
    if (!shortages.value.empty()) { historyPage_->showUnarchiveShortages(shortages.value); return; }
    const auto result = history_.unarchive(orderId);
    if (!result.success) { historyPage_->showError(result.message); return; }
    historyPage_->setArchiveView(false);
    onOrderSelected(orderId);
    refreshHistory();
    if (!result.message.empty()) historyPage_->showNotice(result.message);
}

void PosWindow::onGeneratePdf(const string& orderId)
{
    if (busy_ || selectedOrderId_ != orderId || !pdfRecoveryWarning_.empty()) return;
    const auto result = history_.generatePdf(orderId);
    if (result.success) {
        displayOrder(result.value);
        historyPage_->showNotice(result.message);
    } else {
        historyPage_->showError(result.message);
    }
}

void PosWindow::onOpenPdf(const string& orderId)
{
    if (busy_ || selectedOrderId_ != orderId || !pdfRecoveryWarning_.empty()) return;
    const auto result = history_.openPdfPath(orderId);
    if (!result.success) {
        onOrderSelected(orderId);
        historyPage_->showError(result.message);
        return;
    }
    if (!QDesktopServices::openUrl(QUrl::fromLocalFile(text(result.value.u8string()))))
        historyPage_->showError("Could not open the saved bill PDF.");
}

void PosWindow::onGenerateReport(const ReportRequest& request)
{
    if (busy_ || role_ != Role::Manager) return;
    const auto report = reports_.generate(request);
    if (report.success) reportsPage_->showResult(report.value);
    else reportsPage_->showError(report.message);
}

void PosWindow::showHistoryTab(bool archived)
{
    if (busy_ || !role_ || historyPage_->showingArchives() == archived) return;
    historyPage_->setArchiveView(archived);
    clearOrderSelection();
    refreshHistory();
}

void PosWindow::refreshHistory()
{
    const auto orders = historyPage_->showingArchives() ? history_.listArchived() : history_.list();
    if (orders.success) {
        historyPage_->showOrders(orders.value, selectedOrderId_);
        return;
    }
    historyPage_->showOrders({}, {});
    clearOrderSelection();
    historyPage_->showError(orders.message);
}

void PosWindow::showPage(const string& pageName)
{
    if (busy_ || !role_) return;
    const auto destination = std::find(pageNames.begin(), pageNames.end(), pageName);
    if (destination == pageNames.end()) return;
    const int index = static_cast<int>(std::distance(pageNames.begin(), destination));
    if ((*role_ == Role::Cashier && index > 1) || (*role_ == Role::Manager && index == 0)) return;
    const bool fromHistory = pages_->currentWidget() == historyPage_;
    auto* page = pages_->widget(index);
    if (fromHistory && page != historyPage_) clearOrderSelection();
    if (page != currentOrderPage_) stopCurrentOrderCamera();
    pages_->setCurrentIndex(index);
    updateNavigation();

    if (page == currentOrderPage_ && !currentOrderOpened_) {
        currentOrderPage_->showCartError(refreshCart());
        currentOrderPage_->showCameraPreview({});
        const auto camera = scanner_.start(0);
        currentOrderOpened_ = camera.success;
        if (camera.success) cameraTimer_.start();
        currentOrderPage_->showScannerMessage(camera.message);
    } else if (page == historyPage_) {
        refreshHistory();
    } else if (page == cataloguePage_) {
        cataloguePage_->refresh();
    }
}

void PosWindow::setPdfRecoveryWarning(const string& message)
{
    pdfRecoveryWarning_ = message;
    historyPage_->showPdfRecoveryWarning(message);
    historyPage_->setArchiveActionsAvailable(message.empty());
    if (!message.empty()) historyPage_->setPdfActions(false, false);
}

void PosWindow::stopCurrentOrderCamera()
{
    if (!currentOrderOpened_) return;
    cameraTimer_.stop();
    scanner_.stop();
    currentOrderOpened_ = false;
}

void PosWindow::closeEvent(QCloseEvent* event)
{
    QMainWindow::closeEvent(event);
    if (event->isAccepted()) stopCurrentOrderCamera();
}
