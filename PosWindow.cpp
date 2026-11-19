#include "PosWindow.h"
#include "BarcodeAssetService.h"
#include "BarcodePlotter.h"
#include "CartService.h"
#include "CheckoutService.h"
#include "HistoryService.h"
#include "PosScanner.h"
#include "ProductService.h"

#include <QCloseEvent>
#include <QComboBox>
#include <QDateTime>
#include <QDir>
#include <QEvent>
#include <QFileInfo>
#include <QFontDatabase>
#include <QGraphicsDropShadowEffect>
#include <QGuiApplication>
#include <QImage>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QLocale>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPushButton>
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
#include <QTabBar>
#include <QTabWidget>
#include <QWindow>

#include <algorithm>
#include <array>
#include <functional>
#include <limits>
#include <memory>

namespace {

QString text(const string& value) { return QString::fromStdString(value); }

QString fixedText(int64_t value, int scale, int places)
{
    return QString::number(value / scale) + "." +
        QString::number(value % scale).rightJustified(places, '0');
}

QString money(int64_t value) { return fixedText(value, 100, 2); }

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
    case OrderStatus::Archived: return "Archived";
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
constexpr char disclosureSvg[] = R"svg(<svg preserveAspectRatio="none" overflow="visible" style="display: block;" width="16" height="16" viewBox="0 0 16 16" fill="none" xmlns="http://www.w3.org/2000/svg">
<g id="Disclosure chevron">
<path id="Vector" d="M4 6L8 10L12 6" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round" stroke="#333333"/>
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
    return QLocale(QLocale::English, QLocale::UnitedStates).toString(value / 100) + "." +
           QString::number(value % 100).rightJustified(2, '0');
}

QString ordinaryQuantity(int64_t value)
{
    QString output = fixedText(value, 1000, 3);
    while (output.endsWith('0')) output.chop(1);
    if (output.endsWith('.')) output.chop(1);
    return output;
}

QString unitName(Unit unit) { return unit == Unit::Count ? "Count" : "Weight (kg)"; }
QString unitShort(Unit unit) { return unit == Unit::Count ? "item" : "kg"; }

QString availability(const Product& product)
{
    return quantityText(product.available, product.unit) +
        (product.unit == Unit::Weight ? " kg" : product.available == 1000 ? " item" : " items");
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
        shadow->setBlurRadius(colored ? 12 : 8);
        shadow->setOffset(0, colored ? 3 : 2);
        shadow->setColor(QColor(0, 0, 0, colored ? 48 : 16));
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

class BarcodeDisplay final {
public:
    BarcodeDisplay(QWidget* parent, int x, int y)
    {
        bars_ = label(parent, {x, y, 242, 80}, {}, 14, detailInk, QFont::Normal, Qt::AlignCenter);
        digits_ = label(parent, {x, y + 86, 242, 17}, {}, 14, detailInk, QFont::Normal, Qt::AlignCenter);
    }

    void set(const QImage& image, const QString& barcode = {})
    {
        if (image.isNull()) {
            bars_->clear();
            digits_->clear();
            return;
        }
        // Managed PNGs use the renderer's fixed top margin and bar height.
        const auto bars = image.width() == CODED_STRING_LENGTH * MODULE_WIDTH &&
                          image.height() >= TOP_MARGIN + BARCODE_HEIGHT
            ? image.copy(0, TOP_MARGIN, image.width(), BARCODE_HEIGHT) : image;
        const qreal density = bars_->devicePixelRatioF();
        QPixmap pixels = QPixmap::fromImage(bars.scaled(bars_->size() * density, Qt::IgnoreAspectRatio,
                                                       Qt::FastTransformation));
        pixels.setDevicePixelRatio(density);
        bars_->setPixmap(pixels);
        digits_->setText(barcode);
    }

private:
    QLabel* bars_ = nullptr;
    QLabel* digits_ = nullptr;
};

class PaymentFields final {
public:
    PaymentFields(QWidget* parent, int x, int y, int width, int gap, int merchantWidth,
                  const QString& merchantName)
    {
        amount = field(parent, x, y, width, "Payment amount (PKR)");
        amount->setPlaceholderText("Enter exact amount");
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
        return {true, {}, {true, parsed.value, static_cast<PaymentMode>(mode->currentData().toInt()),
                            merchant->text().trimmed().toStdString()}};
    }

    void setAmount(int64_t money) { amount->setText(formattedMoney(money)); }
    void clearAmount() { amount->clear(); }

    void reset()
    {
        clearAmount();
        mode->setCurrentIndex(mode->findData(static_cast<int>(PaymentMode::Cash)));
        merchant->clear();
    }

private:
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
        const int third = bounds.width() / 3;
        const auto stepButton = [this, third, bounds](const QString& title, int x) {
            auto* widget = action(this, {x, 1, third, bounds.height() - 2}, title, Action::Text);
            widget->setFont(interfaceFont(17));
            widget->setStyleSheet(
                "QPushButton { background:transparent; border:1px solid transparent; color:#171717; }"
                "QPushButton:hover { background:#f1f1f1; } QPushButton:disabled { color:#aaaaaa; }"
                "QPushButton:focus { border-color:#858585; }");
            return widget;
        };
        less_ = stepButton("−", 1);
        more_ = stepButton("+", third * 2);
        less_->setAccessibleName("Decrease quantity");
        more_->setAccessibleName("Increase quantity");
        value_ = new QLineEdit(this);
        value_->setGeometry(third, 1, third + 1, bounds.height() - 2);
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
        connect(value_, &QLineEdit::editingFinished, this, [this] {
            const auto value = read();
            if (value.success && value.value != quantity_) {
                quantity_ = value.value;
                if (changed) changed(quantity_);
            } else if (!value.success && invalid) {
                invalid(value.message);
            }
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

    std::function<void(int64_t)> changed;
    std::function<void()> typing;
    std::function<void(const string&)> invalid;

protected:
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (watched == value_ && event->type() == QEvent::FocusOut && !value_->hasAcceptableInput()) {
            const auto draft = read();
            value_->setText(quantityText(quantity_, unit_));
            if (typing) typing();
            if (!draft.success && invalid) invalid(draft.message);
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
        label(order, {750, 35, 220, 16}, "All prices in PKR", 13, muted, QFont::Normal, right_);
        const QStringList captions{"Bill ID", "Order ID", "Status"};
        const QStringList values{"—", "—", "In progress"};
        for (int i = 0; i < captions.size(); ++i) {
            label(order, {28 + 172 * i, 100, 150, 14}, captions[i], 12, muted);
            label(order, {28 + 172 * i, 120, 150, 17}, values[i]);
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
        rule(order, 28, 621, 942, "#e3e3e3");
        const QStringList totals{"Subtotal (before tax & discounts)", "Total tax", "Total discount"};
        for (int i = 0; i < totals.size(); ++i) {
            label(order, {28, 642 + i * 27, 480, 17}, totals[i], 14, muted);
            totals_[i] = label(order, {760, 642 + i * 27, 210, 17}, {}, 14, ink, QFont::Normal, right_);
        }
        rule(order, 28, 731, 942, "#e3e3e3");
        label(order, {28, 753, 220, 24}, "Total payable", 20, ink, QFont::DemiBold);
        totals_[3] = label(order, {650, 753, 320, 24}, {}, 20, ink, QFont::Bold, right_);
        pay_ = action(order, {28, 801, 455, 44}, "Generate Bill", Action::Checkout);
        hold_ = action(order, {515, 801, 455, 44}, "Generate Bill and Put on Hold", Action::Hold);

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
        quantityLabel_ = label(product, {24, 164, 235, 17}, "Quantity", 14, ink, QFont::Medium);
        quantity_ = new QuantityStepper(product, {344, 154, 112, 36});
        quantity_->setEnabled(false);
        description_ = action(product, {24, 212, 432, 42}, "Description");
        description_->setCheckable(true);
        description_->setEnabled(false);
        description_->setFont(interfaceFont(14, QFont::Medium));
        description_->setStyleSheet(
            "QPushButton { background:white; border:1px solid #dadada; border-radius:8px; text-align:left;"
            " padding-left:13px; color:#171717; } QPushButton:hover {background:#f7f7f7;}"
            " QPushButton:focus {border-color:#858585;} QPushButton:disabled {color:#888888;}");
        disclosure_ = new SvgIcon(description_, {400, 13, 16, 16}, disclosureSvg);
        specsViewport_ = viewport(product, {24, 266, 432, 200});
        auto* specsContent = new QWidget;
        specsContent->setAttribute(Qt::WA_NoSystemBackground);
        specsContent->setFixedWidth(432);
        specs_ = label(specsContent, {12, 7, 408, 136}, {}, 14, muted);
        specsViewport_->setWidget(specsContent);
        specsContent->setAutoFillBackground(false);
        specsViewport_->hide();
        cartError_ = label(product, {24, 479, 432, 39}, {}, 13, errorInk);
        cartError_->setWordWrap(true);
        add_ = action(product, {24, 520, 432, 44}, "Add to Order", Action::Checkout);
        add_->setEnabled(false);
        showSelectionControls(false);
        setBillSaving(false);

        connect(description_, &QPushButton::toggled, this, [this](bool open) {
            specsViewport_->setVisible(open);
            disclosure_->setFlipped(open);
        });
        const auto updateAdd = [this] {
            const auto amount = readAddQuantity();
            add_->setEnabled(amount.success && selected_ && selected_->available > 0);
        };
        quantity_->changed = [updateAdd](int64_t) { updateAdd(); };
        quantity_->typing = updateAdd;
        quantity_->invalid = [this](const string& error) { showCartError(error); };
        connect(add_, &QPushButton::clicked, &window, &PosWindow::onAddClicked);
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
        payment_->reset();
    }

    Result<int64_t> readAddQuantity() const override
    {
        const auto amount = quantity_->read();
        if (!amount.success) return amount;
        if (amount.value == 0) return {false, "Enter a positive quantity.", 0};
        return amount;
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

    void showScannedProduct(const Product& product) override
    {
        selected_ = product;
        productName_->setText(text(product.name));
        productName_->setToolTip(text(product.name));
        productPriceLabel_->setText("Price / " + unitShort(product.unit) + " (excl. tax)");
        quantityLabel_->setText(product.unit == Unit::Count ? "Quantity (items)" : "Quantity (kg)");
        productPrice_->setText("PKR " + formattedMoney(product.unitPrice));
        const auto info = QStringList{
            "Product ID    " + text(product.id), "Acronym    " + text(product.acronym),
            "Barcode    " + text(product.barcode), "Measurement unit    " + unitName(product.unit),
            "Available stock    " + availability(product),
            "Unit price (excl. tax)    PKR " + formattedMoney(product.unitPrice) + " / " + unitShort(product.unit),
            "Tax per " + unitShort(product.unit) + "    PKR " + formattedMoney(product.taxPerUnit),
            "Discount per " + unitShort(product.unit) + "    PKR " + formattedMoney(product.discountPerUnit)
        }.join('\n');
        const auto height = wrapHeight(*specs_, info, 136);
        specs_->setFixedHeight(height);
        specsViewport_->widget()->setFixedHeight(std::max(200, height + 16));
        quantity_->setEnabled(true);
        description_->setEnabled(true);
        showSelectionControls(true);
    }

    void resetAddQuantity() override
    {
        quantity_->setQuantity(0, selected_ ? selected_->unit : Unit::Count);
        add_->setEnabled(false);
    }

    void showCart(const vector<CartItem>& items, const vector<optional<int64_t>>& amounts,
                  const optional<Totals>& totals) override
    {
        auto* content = newRows(*rows_, 942);
        int y = 0;
        long double units = 0;
        long double kilograms = 0;
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
            auto* stepper = new QuantityStepper(row, {646, 25, 112, 36});
            stepper->setQuantity(item.quantity, product.unit);
            stepper->changed = [this, id = product.id](int64_t value) {
                window_.onCartQuantityChanged(id, value);
            };
            stepper->invalid = [this](const string& message) { showCartError(message); };
            const QString amount = i < amounts.size() && amounts[i] ? formattedMoney(*amounts[i]) : "—";
            label(row, {782, 30, 160, 17}, amount, 14, ink, QFont::Medium, right_);
            const int height = std::max(86, 69 + identityHeight);
            row->setGeometry(0, y, 942, height);
            rule(row, 0, height - 1, 942, "#e3e3e3");
            y += height;
            if (product.unit == Unit::Count) units += item.quantity / 1000;
            else kilograms += static_cast<long double>(item.quantity) / 1000;
        }
        if (items.empty()) {
            label(content, {0, 30, 850, 18}, "Your order is empty. Scan a product to get started.", 14, muted);
            y = 90;
        }
        finishRows(*rows_, *content, y);
        count_->setText(QString("%1 %2 · %3 %4%5").arg(items.size())
            .arg(items.size() == 1 ? "product" : "products").arg(QString::number(static_cast<double>(units), 'f', 0))
            .arg(units == 1 ? "item" : "items")
            .arg(" · " + QString::number(static_cast<double>(kilograms), 'f', 3) + " kg"));
        const Totals displayed = totals.value_or(Totals{});
        totals_[0]->setText(totals ? formattedMoney(displayed.subtotal) : "—");
        totals_[1]->setText(totals ? formattedMoney(displayed.tax) : "—");
        totals_[2]->setText(totals ? formattedMoney(displayed.discount) : "—");
        totals_[3]->setText(totals ? "PKR " + formattedMoney(displayed.payable) : "—");
        if (items.empty()) payment_->clearAmount();
        else if (totals) payment_->setAmount(displayed.payable);
        canBill_ = !items.empty() && totals.has_value();
        setBillSaving(false);
    }

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

    void showSelectionControls(bool visible)
    {
        for (auto* control : std::array<QWidget*, 6>{
                productPriceLabel_, productPrice_, quantityLabel_, quantity_, description_, add_}) {
            control->setVisible(visible);
        }
    }

    static constexpr auto right_ = Qt::AlignRight | Qt::AlignVCenter;
    PosWindow& window_;
    QScrollArea* rows_ = nullptr;
    QScrollArea* specsViewport_ = nullptr;
    QLabel* count_ = nullptr;
    CameraFeed* preview_ = nullptr;
    QLabel* previewStatus_ = nullptr;
    QLabel* scannerMessage_ = nullptr;
    QLabel* productName_ = nullptr;
    QLabel* productPriceLabel_ = nullptr;
    QLabel* productPrice_ = nullptr;
    QLabel* quantityLabel_ = nullptr;
    QLabel* specs_ = nullptr;
    QLabel* cartError_ = nullptr;
    QLabel* billError_ = nullptr;
    std::array<QLabel*, 4> totals_{};
    SvgIcon* cameraIcon_ = nullptr;
    SvgIcon* disclosure_ = nullptr;
    QLineEdit* name_ = nullptr;
    QLineEdit* contact_ = nullptr;
    QuantityStepper* quantity_ = nullptr;
    QPushButton* description_ = nullptr;
    QPushButton* add_ = nullptr;
    QPushButton* pay_ = nullptr;
    QPushButton* hold_ = nullptr;
    std::unique_ptr<PaymentFields> payment_;
    optional<Product> selected_;
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

void panelTitle(QWidget* parent, int x, const QString& title, const QString& subtitle)
{
    label(parent, {x, 23, 450, 24}, title, 20, detailInk, QFont::DemiBold);
    label(parent, {x, 53, 530, 16}, subtitle, 13, detailMuted);
}

class HistoryPage final : public PosWindow::OrderHistoryView {
public:
    explicit HistoryPage(PosWindow& window) : window_(window)
    {
        label(this, {0, 6, 600, 33}, "Order History", 28, detailInk, QFont::DemiBold);
        label(this, {0, 44, 600, 16}, "Past orders and saved bills", 13, detailMuted);
        auto* list = panel(this, {0, 73, 650, 885});
        panelTitle(list, 24, "Past orders", "Select an order to view its saved bill");
        rule(list, 24, 88, 602);
        orders_ = viewport(list, {16, 105, 618, 740});
        auto* bill = panel(this, {674, 73, 828, 885});
        bill_ = viewport(bill, {0, 0, 820, 780});
        error_ = label(bill, {28, 760, 772, 34}, {}, 14, detailMuted);
        error_->setWordWrap(true);
        archive_ = action(bill, {28, 805, 120, 42}, "Archive", Action::Secondary, true);
        pay_ = action(bill, {631, 805, 169, 42}, "Mark as Paid", Action::Primary);
        connect(pay_, &QPushButton::clicked, &window, &PosWindow::onMarkPaidClicked);
        connect(archive_, &QPushButton::clicked, &window, &PosWindow::onArchiveClicked);
        clearBill();
    }

    Result<Payment> readPayment() const override
    {
        return payment_ ? payment_->read() : Result<Payment>{false, "Select a held bill.", {}};
    }

    void showError(const string& message) override
    {
        error_->setText(text(message));
        bill_->setFixedHeight(message.empty() ? 780 : 760);
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
            label(row, {x, 43 + height, 340, 16}, dateText(order.timestamp), 13, detailMuted);
            label(row, {480, 16, 120, 14}, statusText(order.status), 12, detailMuted,
                  QFont::Normal, alignRight);
            label(row, {382, 72, 218, 17}, "PKR " + formattedMoney(order.totals.payable), 14,
                  detailInk, QFont::DemiBold, alignRight);
            const int rowHeight = std::max(112, height + 75);
            row->resize(618, rowHeight);
            connect(row, &QPushButton::clicked, this, [this, id = order.ids.orderId] {
                window_.onOrderSelected(id);
                if (auto* current = orders_->widget()) {
                    for (auto* child : current->findChildren<QPushButton*>(QString{}, Qt::FindDirectChildrenOnly))
                        markSelected(*child, child->property("orderId").toString() == text(id));
                }
            });
            row->setProperty("orderId", text(order.ids.orderId));
            y += rowHeight;
            if (&order != &orders.back()) {
                rule(rows, 0, y + 6, 602);
                y += 13;
            }
        }
        if (orders.empty()) {
            label(rows, {32, 24, 540, 21}, "No saved orders", 18, detailInk, QFont::DemiBold);
            label(rows, {32, 58, 540, 36}, "Generate a bill in Current Order to see it here.", 14, detailMuted);
            y = 100;
        }
        finishRows(*orders_, *rows, y);
    }

    void showBill(const SaleRecord& order) override
    {
        payment_.reset();
        auto* content = newRows(*bill_, 820);
        const bool held = order.status == OrderStatus::OnHold;
        const auto metadata = [content](int x, int y, int width, const QString& name, const QString& value) {
            label(content, {x, y, width, 14}, name, 12, detailMuted);
            auto* field = label(content, {x, y + 18, width, 17}, value, 14, detailInk);
            field->setToolTip(value);
            return field;
        };
        label(content, {28, 23, 500, 24}, "Saved bill", 20, detailInk, QFont::DemiBold);
        label(content, {680, 27, 120, 14}, statusText(order.status), 12, detailMuted,
              QFont::Normal, alignRight);
        metadata(28, 66, 174, "Bill ID", text(order.ids.billId));
        metadata(224, 66, 174, "Order ID", text(order.ids.orderId));
        metadata(420, 66, 344, "Date and time", dateText(order.timestamp));
        rule(content, 28, 115, 772);
        auto* customer = metadata(28, 130, 180, "Customer", text(order.customer.name));
        const int customerHeight = wrapHeight(*customer, customer->text(), 17);
        customer->setFixedHeight(customerHeight);
        auto* contact = metadata(224, 130, 535, "Contact",
            order.customer.contact.empty() ? "—" : text(order.customer.contact));
        const int contactHeight = wrapHeight(*contact, contact->text(), 17);
        contact->setFixedHeight(contactHeight);
        int y = 181 + std::max(customerHeight, contactHeight) - 17;
        rule(content, 28, y, 772);
        const QRect acronymColumn(28, 0, 268, 17);
        const QRect priceColumn(300, 0, 150, 17);
        const QRect discountColumn(470, 0, 155, 17);
        const QRect quantityColumn(643, 0, 157, 17);
        label(content, acronymColumn.translated(0, y + 16), "Acronym", 13, detailInk, QFont::Medium);
        label(content, priceColumn.translated(0, y + 16), "Unit price", 13, detailInk, QFont::Medium, alignRight);
        label(content, priceColumn.translated(0, y + 32), "(excl. tax)", 11, detailMuted, QFont::Normal, alignRight);
        label(content, discountColumn.translated(0, y + 16), "Discount / unit", 13, detailInk, QFont::Medium, alignRight);
        label(content, quantityColumn.translated(0, y + 16), "Qty", 13, detailInk, QFont::Medium, alignRight);
        y += 66;
        for (const SaleLine& line : order.lines) {
            label(content, acronymColumn.translated(0, y), text(line.acronym), 12, detailMuted);
            label(content, priceColumn.translated(0, y), formattedMoney(line.unitPrice), 14,
                  detailInk, QFont::Normal, alignRight);
            label(content, discountColumn.translated(0, y), formattedMoney(line.discountPerUnit), 14,
                  detailInk, QFont::Normal, alignRight);
            label(content, quantityColumn.translated(0, y), ordinaryQuantity(line.quantity), 14,
                  detailInk, QFont::Normal, alignRight);
            y += 55;
        }
        y += 4;
        rule(content, 28, y, 772);
        const QStringList captions{"Subtotal before tax & discount", "Total tax", "Total discount"};
        const std::array<int64_t, 3> figures{order.totals.subtotal, order.totals.tax, order.totals.discount};
        for (int i = 0; i < 3; ++i) {
            label(content, {445, y + 15 + i * 25, 190, 16}, captions[i], 13, detailMuted);
            label(content, {638, y + 15 + i * 25, 162, 16}, formattedMoney(figures[i]), 13,
                  detailInk, QFont::Normal, alignRight);
        }
        rule(content, 445, y + 91, 355);
        label(content, {445, y + 103, 170, 19}, "Total payable", 16, detailInk, QFont::DemiBold);
        label(content, {620, y + 103, 180, 19}, "PKR " + formattedMoney(order.totals.payable), 16,
              detailInk, QFont::Bold, alignRight);
        y += 139;
        rule(content, 28, y, 772);
        if (held) {
            label(content, {28, y + 19, 400, 21}, "Record payment", 18, detailInk, QFont::DemiBold);
            label(content, {28, y + 46, 772, 16},
                  "No payment recorded. Enter payment details to mark this order as paid.", 13, detailMuted);
            payment_ = std::make_unique<PaymentFields>(content, 28, y + 83, 230, 18, 276, "Merchant");
            payment_->setAmount(order.totals.payable);
            label(content, {276, y + 153, 230, 14}, "Cash / Card / Wallet", 12, detailMuted);
            label(content, {28, y + 189, 772, 14}, "Payment details are a draft until marked as paid.",
                  12, detailMuted);
            y += 211;
        } else {
            label(content, {28, y + 19, 450, 21}, "Payment recorded", 18, detailInk, QFont::DemiBold);
            label(content, {28, y + 46, 772, 16}, "Saved payment details", 13, detailMuted);
            const QString mode = order.payment.mode == PaymentMode::Cash ? "Cash" :
                order.payment.mode == PaymentMode::Card ? "Card" : "Wallet";
            field(content, 28, y + 83, 230, "Payment amount (PKR)",
                  order.payment.recorded ? formattedMoney(order.payment.amount) : "—", true);
            field(content, 276, y + 83, 230, "Payment mode", order.payment.recorded ? mode : "—", true);
            field(content, 524, y + 83, 276, "Merchant",
                  order.payment.merchant.empty() ? "—" : text(order.payment.merchant), true);
            label(content, {28, y + 189, 772, 14}, "Payment is read only.", 12, detailMuted);
            y += 211;
        }
        finishRows(*bill_, *content, y);
        pay_->setVisible(held);
        archive_->setEnabled(order.status != OrderStatus::Archived);
    }

    void clearBill() override
    {
        payment_.reset();
        auto* content = newRows(*bill_, 820);
        label(content, {28, 23, 772, 24}, "Saved bill", 20, detailInk, QFont::DemiBold);
        label(content, {52, 70, 724, 21}, "No order selected", 18, detailInk, QFont::DemiBold);
        label(content, {52, 104, 724, 17}, "Select an order to view its saved bill.", 14, detailMuted);
        finishRows(*bill_, *content, 180);
        pay_->hide();
        archive_->setEnabled(false);
    }

private:
    PosWindow& window_;
    QScrollArea* orders_ = nullptr;
    QScrollArea* bill_ = nullptr;
    QLabel* error_ = nullptr;
    QPushButton* archive_ = nullptr;
    QPushButton* pay_ = nullptr;
    std::unique_ptr<PaymentFields> payment_;
};

class ProductFields final {
public:
    ProductFields(QWidget* parent, bool editing)
    {
        if (editing) {
            id = field(parent, 28, 104, 240, "Product ID", {}, true);
            barcode = field(parent, 294, 104, 240, "Barcode value", {}, true);
            acronym = field(parent, 560, 104, 240, "Acronym (max 8 characters)");
            label(parent, {294, 172, 240, 13}, "Existing barcode value is read-only", 11, detailMuted);
            name = field(parent, 28, 201, 372, "Product name");
            makeUnit(parent, 428, 201, 372);
            price = field(parent, 28, 283, 240, "Price / item (excl. tax)", {}, false, &priceCaption);
            tax = field(parent, 294, 283, 240, "Tax per unit (PKR)", "0.00");
            discount = field(parent, 560, 283, 240, "Discount per unit (PKR)", "0.00");
            stock = field(parent, 28, 365, 240, "Available quantity (items)", {}, false, &quantityCaption);
        } else {
            id = field(parent, 28, 105, 282, "Product ID", "Assigned on save", true);
            barcode = field(parent, 340, 105, 282, "Barcode value");
            barcode->setMaxLength(6);
            label(parent, {340, 170, 282, 13}, "Unique 6 digits, first digit non-zero", 11, detailMuted);
            acronym = field(parent, 28, 203, 282, "Acronym (max 8 characters)");
            name = field(parent, 340, 203, 282, "Product name");
            makeUnit(parent, 28, 291, 282);
            stock = field(parent, 340, 291, 282, "Available quantity (items)", {}, false, &quantityCaption);
            price = field(parent, 28, 379, 282, "Price / item (excl. tax)", {}, false, &priceCaption);
            tax = field(parent, 340, 379, 282, "Tax per unit (PKR)", "0.00");
            discount = field(parent, 28, 467, 282, "Discount per unit (PKR)", "0.00");
        }
        countValidator = new QRegularExpressionValidator(quantityPattern(Unit::Count), stock);
        weightValidator = new QRegularExpressionValidator(quantityPattern(Unit::Weight), stock);
        stock->setValidator(countValidator);
        QObject::connect(stock, &QLineEdit::textChanged, stock, [this] {
            if (currentUnit() == Unit::Count && stock->validator() == weightValidator &&
                !stock->text().contains('.')) {
                stock->setValidator(countValidator);
                stock->setToolTip({});
                updateLabels();
            }
        });
        QObject::connect(unit, &QComboBox::currentIndexChanged, unit, [this] {
            if (currentUnit() == Unit::Count) {
                const auto draft = quantityInput(stock->text(), Unit::Weight);
                if (stock->text().contains('.') && draft.success && draft.value % 1000 == 0)
                    stock->setText(quantityText(draft.value, Unit::Count));
                if (stock->text().contains('.')) {
                    // Keep the draft editable until the fraction is removed or replaced.
                    stock->setValidator(weightValidator);
                    stock->setToolTip("Count requires whole items; replace the fractional stock quantity.");
                } else {
                    stock->setValidator(countValidator);
                    stock->setToolTip({});
                }
            } else {
                stock->setValidator(weightValidator);
                stock->setToolTip({});
            }
            updateLabels();
        });
        updateLabels();
    }

    Result<Product> read() const
    {
        Product product;
        product.barcode = barcode->text().trimmed().toStdString();
        product.acronym = acronym->text().trimmed().toStdString();
        product.name = name->text().trimmed().toStdString();
        product.unit = currentUnit();
        const auto unitPrice = fixedInput(price->text(), 2);
        const auto unitTax = fixedInput(tax->text(), 2);
        const auto unitDiscount = fixedInput(discount->text(), 2);
        const auto quantity = quantityInput(stock->text(), product.unit);
        if (!unitPrice.success) return {false, "Unit price: " + unitPrice.message, {}};
        if (!unitTax.success) return {false, "Tax: " + unitTax.message, {}};
        if (!unitDiscount.success) return {false, "Discount: " + unitDiscount.message, {}};
        if (!quantity.success) return {false, "Stock: " + quantity.message, {}};
        product.unitPrice = unitPrice.value;
        product.taxPerUnit = unitTax.value;
        product.discountPerUnit = unitDiscount.value;
        product.available = quantity.value;
        return {true, {}, product};
    }

    void show(const Product& product)
    {
        id->setText(text(product.id));
        barcode->setText(text(product.barcode));
        acronym->setText(text(product.acronym));
        name->setText(text(product.name));
        unit->setCurrentIndex(unit->findData(static_cast<int>(product.unit)));
        price->setText(money(product.unitPrice));
        tax->setText(money(product.taxPerUnit));
        discount->setText(money(product.discountPerUnit));
        stock->setText(quantityText(product.available, product.unit));
    }

    QLineEdit* id = nullptr;
    QLineEdit* barcode = nullptr;
    QComboBox* unit = nullptr;

private:
    void makeUnit(QWidget* parent, int x, int y, int width)
    {
        label(parent, {x, y, width, 16}, "Measurement unit", 13, detailInk);
        unit = selector(parent, {x, y + 21, width, 39}, "Measurement unit");
        unit->addItem("Count", static_cast<int>(Unit::Count));
        unit->addItem("Weight (kg)", static_cast<int>(Unit::Weight));
    }

    Unit currentUnit() const { return static_cast<Unit>(unit->currentData().toInt()); }

    void updateLabels()
    {
        const Unit selectedUnit = currentUnit();
        const QString priceLabel = "Price / " + unitShort(selectedUnit) + " (excl. tax)";
        const QString quantityLabel = selectedUnit == Unit::Count
            ? (stock->text().contains('.') ? "Stock (items): remove fraction" : "Available quantity (items)")
            : "Available quantity (kg)";
        priceCaption->setText(priceLabel);
        price->setAccessibleName(priceLabel);
        if (quantityCaption)
            quantityCaption->setText(quantityLabel);
        stock->setAccessibleName(quantityLabel);
    }

    QLineEdit* acronym = nullptr;
    QLineEdit* name = nullptr;
    QLineEdit* price = nullptr;
    QLineEdit* tax = nullptr;
    QLineEdit* discount = nullptr;
    QLineEdit* stock = nullptr;
    QLabel* priceCaption = nullptr;
    QLabel* quantityCaption = nullptr;
    QRegularExpressionValidator* countValidator = nullptr;
    QRegularExpressionValidator* weightValidator = nullptr;
};

class CatalogueTabs final : public QTabWidget {
public:
    explicit CatalogueTabs(QWidget* parent) : QTabWidget(parent)
    {
        setDocumentMode(true);
        tabBar()->hide();
        setStyleSheet("QTabWidget::pane {border:none; background:transparent; margin:0; padding:0;}");
    }
};

class CataloguePage final : public PosWindow::CatalogueView {
public:
    explicit CataloguePage(PosWindow& window) : window_(window)
    {
        label(this, {0, 6, 650, 33}, "Product Catalogue", 28, detailInk, QFont::DemiBold);
        rule(this, 0, 103, 1502);
        viewTab_ = action(this, {3, 59, 120, 32}, "View Catalogue", Action::Text);
        addTab_ = action(this, {171, 59, 96, 32}, "Add Product", Action::Text);
        indicator_ = new QWidget(this);
        indicator_->setStyleSheet("background:#252525;border-radius:1px;");
        tabs_ = new CatalogueTabs(this);
        tabs_->setGeometry(0, 124, 1502, 834);
        auto* view = new QWidget;
        auto* add = new QWidget;
        tabs_->addTab(view, "View Catalogue");
        tabs_->addTab(add, "Add Product");
        tabs_->tabBar()->hide();
        connect(viewTab_, &QPushButton::clicked, this, [this] { tabs_->setCurrentIndex(0); });
        connect(addTab_, &QPushButton::clicked, this, [this] { tabs_->setCurrentIndex(1); });
        connect(tabs_, &QTabWidget::currentChanged, this, [this](int index) {
            updateTabs(index);
            showError({});
        });
        updateTabs(0);

        auto* list = panel(view, {0, 0, 650, 834});
        panelTitle(list, 24, "Products", "Select a product to view and edit its details");
        auto* createLink = action(list, {486, 20, 140, 42}, "Add Product", Action::Primary);
        connect(createLink, &QPushButton::clicked, this, [this] { tabs_->setCurrentIndex(1); });
        rule(list, 24, 88, 602);
        label(list, {32, 105, 280, 16}, "Product", 13, detailMuted, QFont::Medium);
        label(list, {346, 105, 100, 16}, "Available", 13, detailMuted, QFont::Medium, alignRight);
        label(list, {486, 105, 132, 16}, "Unit price", 13, detailMuted, QFont::Medium, alignRight);
        products_ = viewport(list, {16, 137, 618, 650});

        details_ = panel(view, {674, 0, 828, 834});
        panelTitle(details_, 28, "Product details", "Edit product details and available quantity");
        status_ = label(details_, {680, 26, 120, 14}, {}, 12, detailMuted, QFont::Normal, alignRight);
        rule(details_, 28, 84, 772);
        edit_ = std::make_unique<ProductFields>(details_, true);
        stockHint_ = label(details_, {294, 389, 505, 17}, {}, 12, detailMuted);
        rule(details_, 28, 449, 772);
        label(details_, {28, 466, 220, 18}, "Barcode image", 15, detailInk, QFont::DemiBold);
        savedImage_ = std::make_unique<BarcodeDisplay>(details_, 28, 496);
        info_ = viewport(details_, {316, 491, 456, 166});
        rule(details_, 28, 716, 772);
        editError_ = label(details_, {28, 665, 772, 42}, {}, 14, detailMuted);
        editError_->setWordWrap(true);
        remove_ = action(details_, {28, 750, 150, 42}, "Delete Product", Action::Secondary, true);
        cancel_ = action(details_, {513, 750, 112, 42}, "Cancel", Action::Secondary, true);
        update_ = action(details_, {637, 750, 163, 42}, "Save Changes", Action::Primary);
        connect(cancel_, &QPushButton::clicked, this, [this] {
            if (selected_) {
                edit_->show(*selected_);
                showError({});
            }
        });
        connect(update_, &QPushButton::clicked, &window, &PosWindow::onUpdateProduct);
        connect(remove_, &QPushButton::clicked, &window, &PosWindow::onDeleteProduct);
        noProduct_ = label(details_, {28, 23, 772, 30},
                           "Select a product to view its details.", 14, detailMuted);

        auto* form = panel(add, {0, 0, 650, 834});
        panelTitle(form, 28, "Add product", "Enter product details and generate its barcode");
        rule(form, 28, 84, 594);
        create_ = std::make_unique<ProductFields>(form, false);
        rule(form, 28, 694, 594);
        addError_ = label(form, {28, 640, 594, 46}, {}, 14, detailMuted);
        addError_->setWordWrap(true);
        auto* preview = action(form, {28, 733, 190, 42}, "Generate Barcode", Action::Secondary, true);
        auto* save = action(form, {462, 733, 160, 42}, "Save Product", Action::Primary);
        connect(preview, &QPushButton::clicked, &window, &PosWindow::onGeneratePreview);
        connect(save, &QPushButton::clicked, &window, &PosWindow::onSaveProduct);

        auto* draft = panel(add, {674, 0, 828, 834});
        label(draft, {28, 24, 680, 24}, "Generated barcode", 20, detailInk, QFont::DemiBold);
        draftStatus_ = label(draft, {28, 54, 772, 16}, "Enter a barcode and generate a preview.", 13, detailMuted);
        rule(draft, 28, 84, 772);
        barcodeFrame_ = surface(draft, {28, 111, 772, 258}, "#fafafa", "#e7e7e7", 12);
        newImage_ = std::make_unique<BarcodeDisplay>(barcodeFrame_, 265, 65);
        barcodeFrame_->hide();
        draftInfo_ = viewport(draft, {28, 402, 772, 400});
        showMetadata(*draftInfo_, 772, {}, true);
        connect(create_->barcode, &QLineEdit::textEdited, this, [this] {
            create_->id->setText("Assigned on save");
            showBarcodePreview({});
            showMetadata(*draftInfo_, 772, {}, true);
        });
        clearProduct();
    }

    QTabWidget& tabs() const { return *tabs_; }
    string readBarcode() const override { return create_->barcode->text().trimmed().toStdString(); }
    Result<Product> readNewProduct() const override { return create_->read(); }
    Result<Product> readEditedProduct() const override { return edit_->read(); }

    void showError(const string& message) override
    {
        (tabs_->currentIndex() == 0 ? editError_ : addError_)->setText(text(message));
    }

    void showProducts(const vector<Product>& products, const string& selectedId) override
    {
        auto* rows = newRows(*products_, 618);
        int y = 0;
        for (const Product& product : products) {
            const bool selected = product.id == selectedId;
            const int x = selected ? 17 : 16;
            auto* row = selectableRow(rows, {0, y, 618, 105}, text(product.name), selected);
            auto* name = label(row, {x, 15, 280, 18}, {}, 15, detailInk);
            const int height = wrapHeight(*name, text(product.name), 18);
            name->setFixedHeight(height);
            label(row, {x, 21 + height, 280, 14}, text(product.acronym), 12, detailMuted);
            if (product.available == 0)
                label(row, {x, 47 + height, 200, 14}, "Out of stock", 12, detailMuted);
            label(row, {330, 20, 100, 17}, availability(product), 14, detailInk, QFont::Medium, alignRight);
            label(row, {456, 20, 146, 17}, "PKR " + formattedMoney(product.unitPrice), 14,
                  detailInk, QFont::Medium, alignRight);
            const int rowHeight = std::max(105, height + 68);
            row->resize(618, rowHeight);
            row->setProperty("productId", text(product.id));
            connect(row, &QPushButton::clicked, this, [this, id = product.id] {
                window_.onProductSelected(id);
                if (auto* current = products_->widget()) {
                    for (auto* child : current->findChildren<QPushButton*>(QString{}, Qt::FindDirectChildrenOnly))
                        markSelected(*child, child->property("productId").toString() == text(id));
                }
            });
            y += rowHeight;
            if (&product != &products.back()) {
                rule(rows, 16, y + 6, 586);
                y += 13;
            }
        }
        if (products.empty()) {
            label(rows, {32, 24, 550, 21}, "No products yet", 18, detailInk, QFont::DemiBold);
            label(rows, {32, 58, 550, 17}, "Use Add Product to create your first catalogue entry.", 14, detailMuted);
            y = 100;
        }
        finishRows(*products_, *rows, y);
    }

    void showProduct(const Product& product) override
    {
        selected_ = product;
        showProductDetails(true);
        edit_->show(product);
        details_->setEnabled(true);
        status_->setText(product.available == 0 ? "Out of stock" : "");
        setHeldReference(false);
        const QDir root(QString::fromUtf8(EYERIS_PROJECT_DATA_DIR));
        savedImage_->set(QImage(root.filePath(text(product.barcodeImagePath))), text(product.barcode));
        showMetadata(*info_, 456, text(product.barcodeImagePath), false);
    }

    void setHeldReference(bool held) override
    {
        edit_->unit->setEnabled(selected_ && !held);
        remove_->setEnabled(selected_ && !held);
        stockHint_->setText(held ? "Referenced by an on-hold bill" : "");
    }

    void clearProduct() override
    {
        selected_.reset();
        edit_->show({});
        details_->setEnabled(false);
        showProductDetails(false);
        savedImage_->set({});
        showMetadata(*info_, 456, {}, false);
    }

    void showBarcodePreview(const QImage& image) override
    {
        newImage_->set(image, text(readBarcode()));
        barcodeFrame_->setVisible(!image.isNull());
        draftStatus_->setText(image.isNull() ? "Enter a barcode and generate a preview."
                                              : "Barcode generated · Product not saved");
    }

    void showSavedProduct(const Product& product) override
    {
        create_->id->setText(text(product.id));
        const QDir root(QString::fromUtf8(EYERIS_PROJECT_DATA_DIR));
        const QImage barcode(root.filePath(text(product.barcodeImagePath)));
        if (!barcode.isNull()) {
            newImage_->set(barcode, text(product.barcode));
            barcodeFrame_->show();
        }
        draftStatus_->setText("Product saved · Product ID " + text(product.id));
        showMetadata(*draftInfo_, 772, text(product.barcodeImagePath), true);
    }

private:
    void showProductDetails(bool visible)
    {
        for (auto* widget : details_->findChildren<QWidget*>(QString{}, Qt::FindDirectChildrenOnly))
            widget->setVisible(widget == noProduct_ ? !visible : visible);
    }

    void updateTabs(int index)
    {
        markSelected(*viewTab_, index == 0);
        markSelected(*addTab_, index == 1);
        viewTab_->setFont(interfaceFont(14, index == 0 ? QFont::DemiBold : QFont::Normal));
        addTab_->setFont(interfaceFont(14, index == 1 ? QFont::DemiBold : QFont::Normal));
        indicator_->setGeometry(index == 0 ? 0 : 168, 101, index == 0 ? 124 : 103, 3);
    }

    static void showMetadata(QScrollArea& area, int width, const QString& path, bool draft)
    {
        auto* content = newRows(area, width);
        label(content, {0, 0, width, 16}, "Image filename", 12, detailMuted);
        auto* name = label(content, {0, 24, width, 17}, {}, 14, detailInk);
        const int nameHeight = wrapHeight(*name,
            path.isEmpty() ? (draft ? "Not saved yet" : "—") : QFileInfo(path).fileName(), 17);
        name->setFixedHeight(nameHeight);
        label(content, {0, 32 + nameHeight, width, 16}, "Image path", 12, detailMuted);
        auto* location = label(content, {0, 56 + nameHeight, width, 17}, {}, 14, detailInk);
        const int pathHeight = wrapHeight(*location,
            path.isEmpty() ? (draft ? "Available after saving" : "—") : path, 17);
        location->setFixedHeight(pathHeight);
        finishRows(area, *content, 64 + nameHeight + pathHeight);
    }

    PosWindow& window_;
    CatalogueTabs* tabs_ = nullptr;
    QPushButton* viewTab_ = nullptr;
    QPushButton* addTab_ = nullptr;
    QWidget* indicator_ = nullptr;
    QWidget* details_ = nullptr;
    QScrollArea* products_ = nullptr;
    QScrollArea* info_ = nullptr;
    QScrollArea* draftInfo_ = nullptr;
    QLabel* status_ = nullptr;
    QLabel* stockHint_ = nullptr;
    std::unique_ptr<BarcodeDisplay> savedImage_;
    std::unique_ptr<BarcodeDisplay> newImage_;
    QLabel* noProduct_ = nullptr;
    QWidget* barcodeFrame_ = nullptr;
    QLabel* draftStatus_ = nullptr;
    QLabel* editError_ = nullptr;
    QLabel* addError_ = nullptr;
    QPushButton* remove_ = nullptr;
    QPushButton* cancel_ = nullptr;
    QPushButton* update_ = nullptr;
    std::unique_ptr<ProductFields> edit_;
    std::unique_ptr<ProductFields> create_;
    optional<Product> selected_;
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

PosWindow::PosWindow(ProductService& products, BarcodeAssetService& images,
                     CartService& cart, PosScanner& scanner,
                     CheckoutService& checkout, HistoryService& history,
                     QWidget* parent)
    : QMainWindow(parent),
      products_(products),
      images_(images),
      cart_(cart),
      scanner_(scanner),
      checkout_(checkout),
      history_(history) {
    setWindowTitle("Eyeris");
    setWindowFlag(Qt::FramelessWindowHint);
    setAttribute(Qt::WA_TranslucentBackground);
    auto* shell = new Artwork(nullptr, "eyeris-app-background.png", 16);
    shell->setObjectName("eyerisCanvas");
    shell->setFixedSize(canvasWidth, canvasHeight);
    new TitleBar(shell, *this);
    auto* sidebar = new Artwork(shell, "eyeris-sidebar-background.png", 21);
    sidebar->setGeometry(24, 64, 210, 958);
    auto* wordmark = new SvgIcon(sidebar, {19, 29, 168, 48}, wordmarkSvg);
    wordmark->setObjectName("eyerisWordmark");
    wordmark->setAccessibleName("Eyeris");
    const std::array<string, 3> destinations{"Current Order", "Order History", "Product Catalogue"};
    auto* pages = new QStackedWidget(shell);
    pages->setGeometry(pageLeft, 64, pageWidth, 958);
    std::array<QPushButton*, 3> navigation{};
    for (size_t i = 0; i < destinations.size(); ++i) {
        navigation[i] = action(sidebar, {11, 99 + 52 * static_cast<int>(i), 186, 42},
                               text(destinations[i]), Action::Navigation);
        navigation[i]->setProperty("primaryNav", i == 0);
        connect(navigation[i], &QPushButton::clicked, this, [this, name = destinations[i]] {
            showPage(name);
        });
    }
    const auto markNavigation = [navigation](int active) {
        for (size_t i = 0; i < navigation.size(); ++i) {
            const bool selected = static_cast<int>(i) == active;
            markSelected(*navigation[i], selected);
            navigation[i]->setFont(interfaceFont(14, selected ? QFont::DemiBold : QFont::Normal));
        }
    };
    connect(pages, &QStackedWidget::currentChanged, this, markNavigation);
    setCentralWidget(new PosViewport(shell));
    if (const auto* screen = QGuiApplication::primaryScreen()) {
        const auto available = screen->availableGeometry().adjusted(8, 8, -8, -8);
        const QSize size(std::min(canvasWidth, available.width()),
                         std::min(canvasHeight, available.height()));
        resize(size);
        move(available.center() - QPoint(size.width() / 2, size.height() / 2));
    } else {
        resize(canvasWidth, canvasHeight);
    }
    auto* currentOrder = new CurrentOrderPage(*this);
    auto* historyPage = new HistoryPage(*this);
    auto* catalogue = new CataloguePage(*this);
    bindPages(*pages, *currentOrder, *historyPage, *catalogue, catalogue->tabs());
    markNavigation(pages->currentIndex());

    cameraTimer_.setInterval(200);
    cameraTimer_.setTimerType(Qt::CoarseTimer);
    connect(&cameraTimer_, &QTimer::timeout, this, &PosWindow::onCameraTick);
}

// gives PosWindow the real Qt pages when the interface is ready
void PosWindow::bindPages(QStackedWidget& pages, CurrentOrderView& currentOrder,
                          OrderHistoryView& history, CatalogueView& catalogue,
                          QTabWidget& catalogueTabs) {
    stopCurrentOrderCamera();
    pages_ = &pages;
    currentOrderPage_ = &currentOrder;
    historyPage_ = &history;
    cataloguePage_ = &catalogue;
    catalogueTabs_ = &catalogueTabs;

    for (QWidget* page : {currentOrderPage_, historyPage_, cataloguePage_}) {
        if (pages.indexOf(page) < 0) {
            pages.addWidget(page);
        }
    }
}

void PosWindow::onCameraTick() {                                        // called on each tick
    if (!currentOrderPage_) {
        return;
    }

    auto& view = *static_cast<CurrentOrderView*>(currentOrderPage_);
    const Result<ScanResult> scane = scanner_.poll();                   // poll asks scanner to read one frame and check for a barcode, returns a result object

    if (!scane.success) {       // stops the camera if reading the frame failed
        cameraStatus_ = {false, scane.message};
        cameraTimer_.stop();
        scanner_.stop();
        view.showCameraPreview({});
        view.showScannerMessage(cameraStatus_.message);
        return;
    }

    view.showCameraPreview(mat2QImage(scane.value.preview));
    if (!scane.value.decoded.found) {                                                        // keep showing the preview while waiting for a barcode
        view.showScannerMessage(scane.message);
        return;
    }
    if (!scane.value.codeChanged) {
        if (hasSelectedProduct_ && scane.value.decoded.code == selectedProduct_.barcode) {
            view.showScannerMessage({});
        }
        return;
    }

    const Result<Product> product = products_.findByBarcode(scane.value.decoded.code);      // looks up barcode in the catalogue. If there is no match, selection stays as it is
    if (!product.success) {
        view.showScannerMessage(product.message);
        return;
    }

    view.showScannerMessage({});
    if (hasSelectedProduct_ && selectedProduct_.id == product.value.id) {                   // preserves teh entered quantity if lookup returns product already selected
        return;
    }

    // for a different recognised product: this is the new selection now, and the addQuantity option is now set to zero again
    selectedProduct_ = product.value;
    hasSelectedProduct_ = true;
    addQuantity_ = 0;
    view.showScannedProduct(selectedProduct_);
    view.resetAddQuantity();
}

// when clicking on Add to order
void PosWindow::onAddClicked() {
    if (!currentOrderPage_ || !hasSelectedProduct_) {                                      // wait until a product is selected
        return;
    }

    auto& view = *static_cast<CurrentOrderView*>(currentOrderPage_);
    const Result<int64_t> quantity = view.readAddQuantity();
    if (!quantity.success) {
        view.showCartError(quantity.message);
        return;
    }
    addQuantity_ = quantity.value;

    const Status result = cart_.addByBarcode(selectedProduct_.barcode, addQuantity_);       // if add fails, it stops trying to add so cashier can change quantity manually. super neat right?
    if (!result.success) {
        view.showCartError(result.message);
        return;
    }

    addQuantity_ = 0;
    view.resetAddQuantity();
    view.showCartError(refreshCart());
}

// updates an existing row. zero removes the row here
void PosWindow::onCartQuantityChanged(const string& productId, int64_t quantity) {
    const Status result = cart_.setQuantity(productId, quantity);
    if (currentOrderPage_) {
        auto& view = *static_cast<CurrentOrderView*>(currentOrderPage_);
        const string refreshError = refreshCart();
        view.showCartError(result.success ? refreshError : result.message);
    }
}

// gets the actual rows and their amounts from the cart after a quantity change
string PosWindow::refreshCart() {
    auto& view = *static_cast<CurrentOrderView*>(currentOrderPage_);
    const vector<CartItem> items = cart_.items();
    vector<optional<int64_t>> amounts;
    amounts.reserve(items.size());
    string error;
    for (const CartItem& item : items) {
        const Result<Totals> line = cart_.lineTotals(item.product.id);
        if (line.success) {
            amounts.push_back(line.value.subtotal);
        } else {
            amounts.push_back(nullopt);
            if (error.empty()) {
                error = line.message;
            }
        }
    }

    const Result<Totals> totals = cart_.totals();
    view.showCart(items, amounts, totals.success ? optional<Totals>{totals.value} : nullopt);
    return totals.success || !error.empty() ? error : totals.message;
}

// generates and saves a paid bill using the entered payment details
void PosWindow::onGenerateBillClicked() {
    generateCurrentBill(OrderStatus::Paid);
}

// saves the complete bill on hold so it can be paid later
void PosWindow::onGenerateAndHoldClicked() {
    generateCurrentBill(OrderStatus::OnHold);
}

void PosWindow::generateCurrentBill(OrderStatus status) {
    if (!currentOrderPage_ || savingBill_) {
        return;
    }

    auto& view = *static_cast<CurrentOrderView*>(currentOrderPage_);
    const QScopedValueRollback<bool> saving(savingBill_, true);
    const Result<Customer> customer = view.readCustomer();
    if (!customer.success) {
        view.showBillError(customer.message);
        return;
    }

    Payment payment{};
    if (status == OrderStatus::Paid) {
        const Result<Payment> entered = view.readPayment();
        if (!entered.success) {
            view.showBillError(entered.message);
            return;
        }
        payment = entered.value;
        payment.recorded = true;
    }

    const QDateTime now = QDateTime::currentDateTime();
    const Timestamp timestamp{now.date().toString("yyyy-MM-dd").toStdString(),
                              now.time().toString("HH:mm:ss").toStdString()};
    view.showBillError({});
    view.setBillSaving(true);
    const Result<SaleRecord> result = checkout_.generateBill(cart_.items(), customer.value, payment, status, timestamp);
    if (!result.success) {
        view.showBillError(result.message);
        view.setBillSaving(false);
        return;
    }

    cart_.clear();
    addQuantity_ = 0;
    view.resetCheckoutFields();
    view.resetAddQuantity();
    view.showCartError(refreshCart());
    view.setBillSaving(false);
    displayOrder(result.value);
    showPage("Order History");
}

// loads the saved bill for the selected order id
void PosWindow::onOrderSelected(const string& orderId) {
    const Result<SaleRecord> result = history_.get(orderId);
    if (!result.success) {
        clearOrderSelection();
        if (historyPage_) {
            static_cast<OrderHistoryView*>(historyPage_)->showError(result.message);
        }
        return;
    }

    displayOrder(result.value);
}

void PosWindow::displayOrder(const SaleRecord& order) {
    selectedOrderId_ = order.ids.orderId;
    displayedBill_ = order;
    hasDisplayedBill_ = true;
    if (historyPage_) {
        auto& view = *static_cast<OrderHistoryView*>(historyPage_);
        view.showError({});
        view.showBill(displayedBill_);
    }
}

void PosWindow::clearOrderSelection() {
    selectedOrderId_.clear();
    displayedBill_ = {};
    hasDisplayedBill_ = false;
    if (historyPage_) {
        static_cast<OrderHistoryView*>(historyPage_)->clearBill();
    }
}

// records payment on the selected held order, using the existing bill
void PosWindow::onMarkPaidClicked() {
    if (!historyPage_ || !hasDisplayedBill_ || displayedBill_.status != OrderStatus::OnHold) {
        return;
    }

    auto& view = *static_cast<OrderHistoryView*>(historyPage_);
    Result<Payment> payment = view.readPayment();
    if (!payment.success) {
        view.showError(payment.message);
        return;
    }

    payment.value.recorded = true;
    const Result<SaleRecord> result = history_.markPaid(selectedOrderId_, payment.value);
    if (!result.success) {
        view.showError(result.message);
        return;
    }

    displayOrder(result.value);
    refreshHistory();
}

// archives the selected order and clears it from the bill panel state
void PosWindow::onArchiveClicked() {
    if (selectedOrderId_.empty()) {
        return;
    }

    const Status result = history_.archive(selectedOrderId_);
    if (!result.success) {
        if (historyPage_) {
            static_cast<OrderHistoryView*>(historyPage_)->showError(result.message);
        }
        return;
    }

    clearOrderSelection();
    if (historyPage_) {
        refreshHistory();
    }
}

void PosWindow::refreshHistory() {
    auto& view = *static_cast<OrderHistoryView*>(historyPage_);
    const Result<vector<SaleRecord>> orders = history_.list();
    if (!orders.success) {
        view.showError(orders.message);
        return;
    }

    view.showError({});
    view.showOrders(orders.value, selectedOrderId_);
}

// displays the details and barcode image for the selected catalogue product
void PosWindow::onProductSelected(const string& productId) {
    if (!cataloguePage_) {
        return;
    }

    auto& view = *static_cast<CatalogueView*>(cataloguePage_);
    const Result<Product> product = products_.findById(productId);
    if (!product.success) {
        selectedCatalogueProduct_.reset();
        view.clearProduct();
        view.showError(product.message);
        return;
    }

    selectedCatalogueProduct_ = product.value;
    view.showError({});
    view.showProduct(*selectedCatalogueProduct_);
    const auto orders = history_.list();
    const auto referencesProduct = [&productId](const SaleRecord& order) {
        return order.status == OrderStatus::OnHold &&
               std::any_of(order.lines.begin(), order.lines.end(), [&productId](const SaleLine& line) {
                   return line.productId == productId;
               });
    };
    view.setHeldReference(orders.success &&
                          std::any_of(orders.value.begin(), orders.value.end(), referencesProduct));
}

// generates the barcode preview in memory for the add product form
void PosWindow::onGeneratePreview() {
    if (!cataloguePage_) {
        return;
    }

    auto& view = *static_cast<CatalogueView*>(cataloguePage_);
    const QImage image = images_.preview(view.readBarcode());
    view.showBarcodePreview(image);
    view.showError(image.isNull() ? "Could not generate a preview. Check the barcode." : "");
}

// sends the new product to the product service and shows what was saved
void PosWindow::onSaveProduct() {
    if (!cataloguePage_) {
        return;
    }

    auto& view = *static_cast<CatalogueView*>(cataloguePage_);
    const Result<Product> input = view.readNewProduct();
    if (!input.success) {
        view.showError(input.message);
        return;
    }

    const Result<Product> saved = products_.create(input.value);
    if (!saved.success) {
        view.showError(saved.message);
        return;
    }

    refreshCatalogue();
    view.showSavedProduct(saved.value);
}

// sends the original product and the edited version to the product service
void PosWindow::onUpdateProduct() {
    if (!cataloguePage_ || !selectedCatalogueProduct_) {
        return;
    }

    auto& view = *static_cast<CatalogueView*>(cataloguePage_);
    const Result<Product> replacement = view.readEditedProduct();
    if (!replacement.success) {
        view.showError(replacement.message);
        return;
    }

    const Result<Product> saved = products_.update(*selectedCatalogueProduct_, replacement.value);
    if (!saved.success) {
        view.showError(saved.message);
        return;
    }

    onProductSelected(saved.value.id);
    refreshCatalogue();
}

// asks the product service to delete the selected catalogue product
void PosWindow::onDeleteProduct() {
    if (!cataloguePage_ || !selectedCatalogueProduct_) {
        return;
    }

    auto& view = *static_cast<CatalogueView*>(cataloguePage_);
    const Status removed = products_.remove(selectedCatalogueProduct_->id);
    if (!removed.success) {
        view.showError(removed.message);
        return;
    }

    selectedCatalogueProduct_.reset();
    view.clearProduct();
    refreshCatalogue();
}

void PosWindow::refreshCatalogue() {
    auto& view = *static_cast<CatalogueView*>(cataloguePage_);
    const Result<vector<Product>> products = products_.list();
    if (!products.success) {
        view.showError(products.message);
        return;
    }

    view.showError({});
    view.showProducts(products.value, selectedCatalogueProduct_ ? selectedCatalogueProduct_->id : string{});
}

// switches the page in view and starts or stops the camera when needed
void PosWindow::showPage(const string& pageName) {
    if (!pages_) {
        return;
    }

    QWidget* page = nullptr;
    if (pageName == "Current Order") {
        page = currentOrderPage_;
    } else if (pageName == "Order History") {
        page = historyPage_;
    } else if (pageName == "Product Catalogue") {
        page = cataloguePage_;
    }

    if (!page || pages_->indexOf(page) < 0) {
        return;
    }

    if (page != currentOrderPage_) {
        stopCurrentOrderCamera();
    }
    pages_->setCurrentWidget(page);

    if (page == currentOrderPage_ && !currentOrderOpened_) {
        auto& view = *static_cast<CurrentOrderView*>(currentOrderPage_);
        view.showCartError(refreshCart());
        currentOrderOpened_ = true;
        view.showCameraPreview({});
        cameraStatus_ = scanner_.start(0);
        if (cameraStatus_.success) {
            cameraTimer_.start();
        }
        view.showScannerMessage(cameraStatus_.message);
    }

    if (page == historyPage_) {
        refreshHistory();
    } else if (page == cataloguePage_) {
        refreshCatalogue();
    }
}

// switches between tabs inside the product catalogue page
void PosWindow::showCatalogueTab(const string& tabName) {
    if (!catalogueTabs_) {
        return;
    }

    const QString title = QString::fromStdString(tabName);
    for (int index = 0; index < catalogueTabs_->count(); ++index) {
        if (catalogueTabs_->tabText(index) == title) {
            catalogueTabs_->setCurrentIndex(index);
            return;
        }
    }
}

// stops the timer and releases the camera when leaving Current Order
void PosWindow::stopCurrentOrderCamera() {
    if (!currentOrderOpened_) {
        return;
    }

    cameraTimer_.stop();
    scanner_.stop();
    currentOrderOpened_ = false;
}

// closing the window has to release the camera too
void PosWindow::closeEvent(QCloseEvent* event) {
    QMainWindow::closeEvent(event);
    if (event->isAccepted()) {
        stopCurrentOrderCamera();
    }
}
