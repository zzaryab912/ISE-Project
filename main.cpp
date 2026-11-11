#include <QApplication>
#include <QCloseEvent>
#include <QCoreApplication>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QFontDatabase>
#include <QFontMetricsF>
#include <QIcon>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QMessageBox>
#include <QPainter>
#include <QPixmap>
#include <QRegularExpression>
#include <QPushButton>
#include <QFrame>
#include <QPoint>
#include <QSize>
#include <QStringList>
#include <QRegularExpressionValidator>
#include <QStackedWidget>
#include <QString>
#include <QSvgRenderer>
#include <QTimer>
#include <QWidget>

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <string>

#include "BarcodeDecoder.h"
#include "BarcodeImageIO.h"
#include "BarcodePlotter.h"
#include "ReaderContracts.h"
#include "barcodeGenerator.h"
#include "camera-harness.h"

namespace {

constexpr int kContentWidth = 1163;
constexpr int kContentHeight = 574;
constexpr int kBrandWidth = 566;

const QString kInk = QStringLiteral("#18245c");
const QString kBody = QStringLiteral("#20305b");
const QString kMuted = QStringLiteral("#53627e");
const QString kBorder = QStringLiteral("#d1d1d1");

QString systemFontFamily()
{
    const QStringList families = QFontDatabase::families();
    for (const QString &candidate : {QStringLiteral("SF Pro Expanded"),
                                     QStringLiteral("SF Pro"),
                                     QStringLiteral("SF Pro Display"),
                                     QStringLiteral(".SF NS")}) {
        if (families.contains(candidate)) {
            return candidate;
        }
    }
    return QFontDatabase::systemFont(QFontDatabase::GeneralFont).family();
}

QFont uiFont(int pixelSize, QFont::Weight weight, qreal tracking = 0.0)
{
    QFont font(systemFontFamily());
    font.setPixelSize(pixelSize);
    font.setWeight(weight);
    font.setLetterSpacing(QFont::AbsoluteSpacing, tracking);
    return font;
}

QString assetPath(const QString &name)
{
    const QStringList candidates = {
        QDir(QCoreApplication::applicationDirPath()).filePath("assets/" + name),
        QDir(QCoreApplication::applicationDirPath()).filePath("../Resources/assets/" + name),
#ifdef EYERIS_SOURCE_ASSET_DIR
        QDir(QStringLiteral(EYERIS_SOURCE_ASSET_DIR)).filePath(name),
#endif
    };

    for (const QString &candidate : candidates) {
        if (QFileInfo::exists(candidate)) {
            return candidate;
        }
    }
    return {};
}

QPixmap svgPixmap(const QString &name, const QSize &size)
{
    QPixmap pixmap(size);
    pixmap.fill(Qt::transparent);
    QSvgRenderer renderer(assetPath(name));
    if (renderer.isValid()) {
        QPainter painter(&pixmap);
        renderer.render(&painter);
    }
    return pixmap;
}

bool isValidGuiInput(const QString &input)
{
    if (input.size() != 6 || input.at(0) < QLatin1Char('1') ||
        input.at(0) > QLatin1Char('9')) {
        return false;
    }
    for (const QChar character : input) {
        if (character < QLatin1Char('0') || character > QLatin1Char('9')) {
            return false;
        }
    }
    return isValid6DigitInput(input.toStdString());
}

std::string generateVerifiedBits(const QString &input)
{
    if (!isValidGuiInput(input)) {
        return {};
    }

    const std::string code = input.toStdString();
    const std::string bits = generateBarcodeBits(code);
    if (bits.empty() || !verifyEncoding(code, bits)) {
        return {};
    }
    return bits;
}

QImage generateBarcodeImage(const QString &input)
{
    const std::string bits = generateVerifiedBits(input);
    if (bits.empty()) {
        return {};
    }
    return codedString2Image(bits, input, true);
}

QString pngSavePath(const QString &chosenPath)
{
    if (chosenPath.isEmpty() || chosenPath.endsWith(".png", Qt::CaseInsensitive)) {
        return chosenPath;
    }
    return chosenPath + ".png";
}

DecodeResult decodePreparedInput(const InputResult &input, const BarcodeDecoder &decoder)
{
    if (input.status == ImageStatus::NoCandidate) {
        return {false, input.message.empty() ? "No barcode found." : input.message, ""};
    }
    if (input.status == ImageStatus::MultipleCandidates) {
        return {false, input.message.empty()
                           ? "Please scan only one barcode at a time."
                           : input.message,
                ""};
    }
    if (input.status != ImageStatus::Ready) {
        return {false, input.message, ""};
    }
    return decoder.decode(input.barcode);
}

QLabel *makeTextLabel(QWidget *parent, const QString &text, const QRect &geometry,
                      int pixelSize, QFont::Weight weight, const QString &color,
                      Qt::Alignment alignment = Qt::AlignLeft | Qt::AlignVCenter,
                      qreal tracking = 0.0)
{
    auto *label = new QLabel(text, parent);
    label->setGeometry(geometry);
    label->setAlignment(alignment);
    label->setFont(uiFont(pixelSize, weight, tracking));
    label->setStyleSheet(QStringLiteral("color: %1; background: transparent;").arg(color));
    label->setAttribute(Qt::WA_TransparentForMouseEvents);
    return label;
}

QPushButton *makeOptionCard(QWidget *parent, const QString &title,
                            const QString &description, const QRect &geometry)
{
    auto *button = new QPushButton(parent);
    button->setGeometry(geometry);
    button->setCursor(Qt::PointingHandCursor);
    button->setAccessibleName(title + ". " + description);
    button->setStyleSheet(QStringLiteral(
        "QPushButton { background: #ffffff;"
        " border: 2px solid #d1d1d1; border-radius: 19px; }"
        "QPushButton:hover { background: #fafafa; border-color: #bdbdbd; }"
        "QPushButton:pressed { background: #f4f4f4; }"
        "QPushButton:focus { border-color: #929292; }"));
    auto *titleLabel = makeTextLabel(button, title, QRect(21, 20, 434, 27),
                                     16, QFont::Medium, kBody);
    auto *descriptionLabel = makeTextLabel(button, description, QRect(21, 42, 434, 24),
                                           13, QFont::Medium, kMuted);
    titleLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    descriptionLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    return button;
}

QLabel *makeHeading(const QString &text, QWidget *parent, int left, int top)
{
    return makeTextLabel(parent, text, QRect(left, top, 426, 102),
                         48, QFont::Normal, kInk, Qt::AlignLeft | Qt::AlignTop, 0.48);
}

QPushButton *makeOutlinedButton(QWidget *parent, const QString &text,
                                const QRect &geometry)
{
    auto *button = new QPushButton(text, parent);
    button->setGeometry(geometry);
    button->setCursor(Qt::PointingHandCursor);
    button->setFont(uiFont(16, QFont::Medium));
    button->setStyleSheet(QStringLiteral(
        "QPushButton { background: #ffffff; color: %1;"
        " border: 2px solid #d1d1d1; border-radius: 19px; padding: 0 8px; }"
        "QPushButton:hover { background: #fafafa; border-color: #bdbdbd; }"
        "QPushButton:pressed { background: #f4f4f4; }"
        "QPushButton:focus { border-color: #929292; }"
        "QPushButton:disabled { color: #999999; }").arg(kBody));
    return button;
}

class EyerisWindow final : public QMainWindow {
public:
    EyerisWindow()
        : source_(new ImageSource), decoder_(new BarcodeDecoder)
    {
        setWindowTitle(QStringLiteral("Eyeris — Barcode Studio"));
        auto *canvas = new QWidget(this);
        canvas->setFixedSize(kContentWidth, kContentHeight);
        canvas->setStyleSheet("background: white;");
        setCentralWidget(canvas);

        brandPanel_ = new QLabel(canvas);
        brandPanel_->setGeometry(0, 0, kBrandWidth, kContentHeight);
        const QPixmap background(assetPath(QStringLiteral("eyeris-background.png")));
        if (!background.isNull()) {
            brandPanel_->setPixmap(background.scaled(brandPanel_->size(), Qt::IgnoreAspectRatio,
                                                       Qt::SmoothTransformation));
        }

        auto *brand = new QLabel(QStringLiteral("Eyeris"), canvas);
        brand->setGeometry(72, 193, 423, 153);
        brand->setContentsMargins(0, 0, 0, 0);
        brand->setAlignment(Qt::AlignCenter);
        QFont brandFont(systemFontFamily());
        brandFont.setPixelSize(128);
        brandFont.setWeight(QFont::Thin);
        brandFont.setVariableAxis(QFont::Tag("wdth"), 132.0f);
        brandFont.setVariableAxis(QFont::Tag("wght"), 120.7f);
        brandFont.setLetterSpacing(QFont::AbsoluteSpacing, 6.4);
        const QString brandText = QStringLiteral("Eyeris");
        for (int pixelSize = 128; pixelSize >= 64; --pixelSize) {
            brandFont.setPixelSize(pixelSize);
            const QFontMetricsF metrics(brandFont);
            const QRectF bounds = metrics.tightBoundingRect(brandText);
            const qreal left = std::min(0.0, bounds.left());
            const qreal right = std::max(metrics.horizontalAdvance(brandText), bounds.right());
            if (right - left <= brand->width() - 12 &&
                metrics.height() <= brand->height() - 12) {
                break;
            }
        }
        brand->setFont(brandFont);
        brand->setStyleSheet("color: white; background: transparent;");

        auto *subtitle = new QLabel(QStringLiteral("Barcode Scanner"), canvas);
        subtitle->setGeometry(151, 346, 265, 29);
        subtitle->setAlignment(Qt::AlignCenter);
        subtitle->setFont(uiFont(24, QFont::Light, 4.32));
        subtitle->setStyleSheet("color: white; background: transparent;");

        pages_ = new QStackedWidget(canvas);
        pages_->setGeometry(kBrandWidth, 0, kContentWidth - kBrandWidth, kContentHeight);
        pages_->setStyleSheet("QStackedWidget { background: white; }");

        buildHome();
        buildGenerate();
        buildScanChoice();
        buildUpload();
        buildCamera();
        buildFound();

        cameraTimer_.setInterval(200);
        cameraTimer_.setTimerType(Qt::CoarseTimer);
        connectSignals();

        pages_->setCurrentWidget(homePage_);
        setFixedSize(kContentWidth, kContentHeight);
        setStyleSheet("QMainWindow { background: white; }");
    }

    ~EyerisWindow() override
    {
        stopCamera();
        delete source_;
        delete decoder_;
    }

protected:
    void closeEvent(QCloseEvent *event) override
    {
        stopCamera();
        QMainWindow::closeEvent(event);
    }

private:
    enum class Page { Home, Generate, ScanChoice, Upload, Camera, Found };

    ImageSource *source_;
    BarcodeDecoder *decoder_;
    QStackedWidget *pages_ = nullptr;
    QLabel *brandPanel_ = nullptr;
    QWidget *homePage_ = nullptr;
    QWidget *generatePage_ = nullptr;
    QWidget *scanChoicePage_ = nullptr;
    QWidget *uploadPage_ = nullptr;
    QWidget *cameraPage_ = nullptr;
    QWidget *foundPage_ = nullptr;
    QLineEdit *codeInput_ = nullptr;
    QPushButton *generateButton_ = nullptr;
    QPushButton *saveButton_ = nullptr;
    QLabel *generatedPreview_ = nullptr;
    QLabel *emptyPreview_ = nullptr;
    QLabel *generationStatus_ = nullptr;
    QFrame *generationDivider_ = nullptr;
    QFrame *uploadArea_ = nullptr;
    QLabel *uploadPreview_ = nullptr;
    QLabel *uploadStatus_ = nullptr;
    QLabel *uploadPrompt_ = nullptr;
    QLabel *uploadGuidance_ = nullptr;
    QLabel *cameraStatus_ = nullptr;
    QLabel *cameraPreview_ = nullptr;
    QLabel *cameraPlaceholder_ = nullptr;
    QLabel *foundCode_ = nullptr;
    QLabel *foundStatus_ = nullptr;
    QPushButton *cameraToggleButton_ = nullptr;
    QTimer cameraTimer_;
    QImage generatedImage_;
    QString generatedCode_;
    bool cameraRunning_ = false;

    QWidget *newPage()
    {
        auto *page = new QWidget;
        page->setFixedSize(kContentWidth - kBrandWidth, kContentHeight);
        page->setStyleSheet("background: white;");
        pages_->addWidget(page);
        return page;
    }

    void buildHome()
    {
        homePage_ = newPage();
        makeHeading(QStringLiteral("Choose an option"), homePage_, 45, 97);

        auto *generate = makeOptionCard(
            homePage_, QStringLiteral("Generate a barcode"),
            QStringLiteral("Enter a 6 digit code to generate a barcode"),
            QRect(45, 179, 476, 80));
        auto *scan = makeOptionCard(
            homePage_, QStringLiteral("Scan a barcode"),
            QStringLiteral("Read a barcode by uploading an image or scanning with camera"),
            QRect(45, 270, 476, 80));

        connect(generate, &QPushButton::clicked, this, [this] { navigate(Page::Generate); });
        connect(scan, &QPushButton::clicked, this, [this] { navigate(Page::ScanChoice); });
    }

    QPushButton *addBackButton(QWidget *page, Page destination)
    {
        auto *back = new QPushButton(page);
        back->setGeometry(33, 110, 33, 33);
        back->setCursor(Qt::PointingHandCursor);
        back->setIcon(QIcon(svgPixmap(QStringLiteral("chevron-left.svg"), QSize(33, 33))));
        back->setIconSize(QSize(33, 33));
        back->setStyleSheet("QPushButton { background: transparent; border: 0; padding: 0; }");
        connect(back, &QPushButton::clicked, this, [this, destination] { navigate(destination); });
        return back;
    }

    void buildGenerate()
    {
        generatePage_ = newPage();
        addBackButton(generatePage_, Page::Home);
        makeHeading(QStringLiteral("Generate Barcode"), generatePage_, 71, 100);

        makeTextLabel(generatePage_, QStringLiteral("Enter a 6-digit code (Code 128)"),
                      QRect(52, 193, 440, 24), 16, QFont::Medium, kBody);

        codeInput_ = new QLineEdit(generatePage_);
        codeInput_->setGeometry(52, 224, 506, 41);
        codeInput_->setMaxLength(6);
        codeInput_->setPlaceholderText(QStringLiteral("e.g. 123456"));
        codeInput_->setValidator(new QRegularExpressionValidator(QRegularExpression(QStringLiteral("[0-9]{0,6}")), codeInput_));
        codeInput_->setFont(uiFont(16, QFont::Medium));
        codeInput_->setStyleSheet(QStringLiteral(
            "QLineEdit { border: 1px solid %1; border-radius: 14px; padding: 0 18px;"
            " color: %2; font-size: 16px; background: white; }"
            "QLineEdit:focus { border: 1px solid #929292; }").arg(kBorder, kBody));

        generateButton_ = new QPushButton(generatePage_);
        generateButton_->setGeometry(526, 233, 24, 24);
        generateButton_->setCursor(Qt::PointingHandCursor);
        generateButton_->setAccessibleName(QStringLiteral("Generate barcode"));
        generateButton_->setIcon(QIcon(svgPixmap(QStringLiteral("arrow-right.svg"), QSize(24, 24))));
        generateButton_->setIconSize(QSize(24, 24));
        generateButton_->setStyleSheet("QPushButton { border: 0; background: transparent; padding: 0; }");

        generatedPreview_ = new QLabel(generatePage_);
        generatedPreview_->setGeometry(136, 309, 339, 138);
        generatedPreview_->setAlignment(Qt::AlignCenter);
        generatedPreview_->setStyleSheet("background: transparent;");
        generatedPreview_->setScaledContents(false);

        generationStatus_ = new QLabel(generatePage_);
        generationStatus_->setGeometry(52, 519, 506, 25);
        generationStatus_->setAlignment(Qt::AlignCenter);
        generationStatus_->setFont(uiFont(13, QFont::Medium));
        generationStatus_->setStyleSheet(QStringLiteral("color: %1; background: transparent;").arg(kMuted));

        emptyPreview_ = new QLabel(QStringLiteral("No barcode generated"), generatePage_);
        emptyPreview_->setGeometry(146, 334, 300, 28);
        emptyPreview_->setAlignment(Qt::AlignCenter);
        emptyPreview_->setFont(uiFont(16, QFont::Medium));
        emptyPreview_->setStyleSheet(QStringLiteral("color: %1; background: transparent;").arg(kMuted));

        saveButton_ = makeOutlinedButton(generatePage_, QStringLiteral("Save to disk"),
                                         QRect(221, 469, 154, 41));
        saveButton_->setEnabled(false);
    }

    void buildScanChoice()
    {
        scanChoicePage_ = newPage();
        addBackButton(scanChoicePage_, Page::Home);
        makeHeading(QStringLiteral("Scan a Barcode"), scanChoicePage_, 66, 97);

        auto *upload = makeOptionCard(
            scanChoicePage_, QStringLiteral("Upload an Image"),
            QStringLiteral("Scan for a barcode in an uploaded image"),
            QRect(45, 179, 476, 80));
        auto *camera = makeOptionCard(
            scanChoicePage_, QStringLiteral("Use a Camera"),
            QStringLiteral("Use your webcam to scan a barcode"),
            QRect(45, 270, 476, 80));

        connect(upload, &QPushButton::clicked, this, [this] { navigate(Page::Upload); });
        connect(camera, &QPushButton::clicked, this, [this] { navigate(Page::Camera); });
    }

    void buildUpload()
    {
        uploadPage_ = newPage();
        addBackButton(uploadPage_, Page::ScanChoice);
        makeHeading(QStringLiteral("Upload an Image"), uploadPage_, 66, 97);

        uploadArea_ = new QFrame(uploadPage_);
        uploadArea_->setGeometry(45, 184, 476, 217);
        uploadArea_->setStyleSheet(QStringLiteral(
            "QFrame { background: #ffffff; border: 2px solid #d1d1d1;"
            " border-radius: 19px; }"));

        uploadPrompt_ = makeTextLabel(uploadArea_, QStringLiteral("Choose an image to scan"),
                                      QRect(24, 46, 428, 24), 16, QFont::Medium, kBody);
        uploadGuidance_ = makeTextLabel(uploadArea_,
                                        QStringLiteral("Select a barcode image from your device"),
                                        QRect(24, 76, 428, 20), 13, QFont::Medium, kMuted);

        auto *choose = makeOutlinedButton(uploadArea_, QStringLiteral("Choose image"),
                                          QRect(149, 127, 178, 42));
        connect(choose, &QPushButton::clicked, this, [this] { chooseImage(); });

        uploadPreview_ = new QLabel(uploadPage_);
        uploadPreview_->setGeometry(45, 184, 476, 217);
        uploadPreview_->setAlignment(Qt::AlignCenter);
        uploadPreview_->setStyleSheet("background: transparent; border: 0;");
        uploadPreview_->hide();

        uploadStatus_ = new QLabel(uploadPage_);
        uploadStatus_->setGeometry(45, 419, 476, 25);
        uploadStatus_->setAlignment(Qt::AlignCenter);
        uploadStatus_->setFont(uiFont(13, QFont::Medium));
        uploadStatus_->setStyleSheet(QStringLiteral("color: %1; background: transparent;").arg(kMuted));
        uploadStatus_->setText(QStringLiteral("No image selected"));
    }

    void buildCamera()
    {
        cameraPage_ = newPage();
        addBackButton(cameraPage_, Page::ScanChoice);
        makeHeading(QStringLiteral("Scan with Camera"), cameraPage_, 66, 97);

        cameraPreview_ = new QLabel(cameraPage_);
        cameraPreview_->setGeometry(45, 184, 476, 230);
        cameraPreview_->setAlignment(Qt::AlignCenter);
        cameraPreview_->setStyleSheet(
            "background: rgba(33,31,33,0.40); border: 2px solid rgba(181,181,181,0.40);"
            " border-radius: 19px; color: #ded6d9;");

        cameraPlaceholder_ = makeTextLabel(cameraPage_, QStringLiteral("Camera preview"),
                                           QRect(69, 277, 428, 30), 16, QFont::Medium,
                                           QStringLiteral("#ded6d9"), Qt::AlignCenter);
        cameraPlaceholder_->hide();
        cameraPreview_->setText(QStringLiteral("Camera preview"));

        cameraStatus_ = new QLabel(cameraPage_);
        cameraStatus_->setGeometry(45, 495, 476, 25);
        cameraStatus_->setAlignment(Qt::AlignCenter);
        cameraStatus_->setFont(uiFont(13, QFont::Medium));
        cameraStatus_->setStyleSheet(QStringLiteral("color: %1; background: transparent;").arg(kMuted));
        cameraStatus_->setText(QStringLiteral("Position the barcode within the camera view"));

        cameraToggleButton_ = makeOutlinedButton(cameraPage_, QStringLiteral("Start Camera"),
                                                  QRect(194, 435, 178, 42));
        cameraToggleButton_->setAccessibleName(QStringLiteral("Start Camera"));

        connect(cameraToggleButton_, &QPushButton::clicked, this, [this] {
            if (cameraRunning_) {
                stopCamera();
                cameraToggleButton_->setText(QStringLiteral("Start Camera"));
                cameraToggleButton_->setAccessibleName(QStringLiteral("Start Camera"));
                cameraStatus_->setText(QStringLiteral("Position the barcode within the camera view"));
            } else {
                startCamera();
            }
        });
    }

    void buildFound()
    {
        foundPage_ = newPage();
        addBackButton(foundPage_, Page::ScanChoice);
        makeHeading(QStringLiteral("Barcode Found"), foundPage_, 66, 97);

        auto *resultArea = new QFrame(foundPage_);
        resultArea->setGeometry(45, 184, 476, 180);
        resultArea->setStyleSheet(QStringLiteral(
            "QFrame { background: #ffffff; border: 2px solid #d1d1d1;"
            " border-radius: 19px; }"));

        makeTextLabel(resultArea, QStringLiteral("Barcode successfully read"),
                      QRect(24, 26, 428, 24), 16, QFont::Medium, kBody);

        foundCode_ = new QLabel(foundPage_);
        foundCode_->setGeometry(69, 251, 428, 75);
        foundCode_->setAlignment(Qt::AlignCenter);
        foundCode_->setFont(uiFont(40, QFont::Normal, 0.4));
        foundCode_->setStyleSheet(QStringLiteral("color: %1; background: transparent;").arg(kInk));

        foundStatus_ = new QLabel(foundPage_);
        foundStatus_->setGeometry(69, 320, 428, 24);
        foundStatus_->setAlignment(Qt::AlignCenter);
        foundStatus_->setFont(uiFont(13, QFont::Medium));
        foundStatus_->setStyleSheet(QStringLiteral("color: %1; background: transparent;").arg(kMuted));

        auto *again = makeOutlinedButton(foundPage_, QStringLiteral("Scan another"),
                                         QRect(194, 400, 178, 42));
        connect(again, &QPushButton::clicked, this, [this] { navigate(Page::ScanChoice); });
    }

    void connectSignals()
    {
        connect(codeInput_, &QLineEdit::textChanged, this, [this] {
            generatedImage_ = QImage();
            generatedCode_.clear();
            generatedPreview_->clear();
            emptyPreview_->show();
            generationStatus_->clear();
            saveButton_->setEnabled(false);
        });

        connect(generateButton_, &QPushButton::clicked, this, [this] { generate(); });
        connect(codeInput_, &QLineEdit::returnPressed, this, [this] { generate(); });
        connect(saveButton_, &QPushButton::clicked, this, [this] { saveGenerated(); });
        connect(&cameraTimer_, &QTimer::timeout, this, [this] { readCameraFrame(); });
    }

    void navigate(Page destination)
    {
        stopCamera();
        switch (destination) {
        case Page::Home: pages_->setCurrentWidget(homePage_); break;
        case Page::Generate: pages_->setCurrentWidget(generatePage_); break;
        case Page::ScanChoice: pages_->setCurrentWidget(scanChoicePage_); break;
        case Page::Upload:
            uploadStatus_->clear();
            uploadPreview_->clear();
            uploadPreview_->hide();
            uploadArea_->show();
            pages_->setCurrentWidget(uploadPage_);
            break;
        case Page::Camera:
            cameraPreview_->setPixmap(QPixmap());
            cameraPreview_->setText(QStringLiteral("Camera preview will appear here"));
            cameraStatus_->clear();
            pages_->setCurrentWidget(cameraPage_);
            break;
        case Page::Found: pages_->setCurrentWidget(foundPage_); break;
        }
    }

    void generate()
    {
        generatedImage_ = QImage();
        generatedCode_.clear();
        generatedPreview_->clear();
        emptyPreview_->show();
        generationStatus_->clear();
        saveButton_->setEnabled(false);

        const QString input = codeInput_->text();
        if (!isValidGuiInput(input)) {
            generationStatus_->setText(QStringLiteral(
                "Enter exactly six ASCII digits, starting with 1–9."));
            return;
        }

        QImage image = generateBarcodeImage(input);
        if (image.isNull()) {
            generationStatus_->setText(QStringLiteral("Could not generate the barcode. Please try again."));
            return;
        }

        generatedImage_ = image;
        generatedCode_ = input;
        const QPixmap preview = QPixmap::fromImage(generatedImage_).scaled(
            generatedPreview_->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
        generatedPreview_->setPixmap(preview);
        emptyPreview_->hide();
        generationStatus_->clear();
        saveButton_->setEnabled(true);
    }

    void saveGenerated()
    {
        if (generatedImage_.isNull() || generatedCode_ != codeInput_->text()) {
            generatedImage_ = QImage();
            generatedCode_.clear();
            generatedPreview_->clear();
            emptyPreview_->show();
            generationStatus_->setText(QStringLiteral("Generate the current code before saving."));
            saveButton_->setEnabled(false);
            return;
        }

        const QString chosenPath = QFileDialog::getSaveFileName(
            this, QStringLiteral("Save barcode"),
            QStringLiteral("barcode-%1.png").arg(generatedCode_),
            QStringLiteral("PNG images (*.png)"), nullptr,
            QFileDialog::DontConfirmOverwrite);
        if (chosenPath.isEmpty()) {
            return;
        }

        const QString path = pngSavePath(chosenPath);
        if (QFileInfo::exists(path)) {
            const auto answer = QMessageBox::question(
                this, QStringLiteral("Replace file"),
                QStringLiteral("This file already exists. Replace it?"),
                QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
            if (answer != QMessageBox::Yes) {
                return;
            }
        }

        if (saveBarcodeImage(generatedImage_, path)) {
            generationStatus_->setText(QStringLiteral("Barcode saved successfully."));
        } else {
            generationStatus_->setText(QStringLiteral("Could not save the barcode. Please try again."));
        }
    }

    void chooseImage()
    {
        stopCamera();
        const QString path = QFileDialog::getOpenFileName(
            this, QStringLiteral("Open barcode image"), QString(),
            QStringLiteral("Images (*.png *.jpg *.jpeg)"));
        if (path.isEmpty()) {
            return;
        }

        const QImage originalImage(path);
        if (!originalImage.isNull()) {
            uploadPreview_->setPixmap(QPixmap::fromImage(originalImage).scaled(
                uploadPreview_->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
            uploadPreview_->setText(QString());
        } else {
            uploadPreview_->setPixmap(QPixmap());
            uploadPreview_->setText(QStringLiteral("Image preview unavailable"));
        }
        uploadArea_->hide();
        uploadPreview_->show();

        const InputResult input = source_->loadFile(path);
        const DecodeResult result = decodePreparedInput(input, *decoder_);
        if (result.found) {
            showDecodedCode(result);
            return;
        }
        uploadStatus_->setText(QString::fromStdString(result.message));
    }

    void startCamera()
    {
        if (cameraRunning_) {
            return;
        }
        cameraStatus_->clear();
        cameraPreview_->setText(QStringLiteral("Opening camera…"));

        std::string message;
        if (!source_->startCamera(0, message)) {
            source_->stopCamera();
            cameraPreview_->setText(QStringLiteral("Camera preview will appear here"));
            cameraStatus_->setText(QString::fromStdString(message));
            return;
        }

        cameraRunning_ = true;
        cameraToggleButton_->setText(QStringLiteral("Stop Camera"));
        cameraToggleButton_->setAccessibleName(QStringLiteral("Stop Camera"));
        cameraStatus_->setText(QStringLiteral("Scanning…"));
        cameraTimer_.start();
    }

    void stopCamera()
    {
        cameraTimer_.stop();
        if (source_) {
            source_->stopCamera();
        }
        cameraRunning_ = false;
        if (cameraToggleButton_) {
            cameraToggleButton_->setText(QStringLiteral("Start Camera"));
            cameraToggleButton_->setAccessibleName(QStringLiteral("Start Camera"));
        }
    }

    void readCameraFrame()
    {
        if (!cameraRunning_) {
            cameraTimer_.stop();
            return;
        }

        const InputResult input = source_->nextFrame();
        if (!input.preview.empty() && input.preview.depth() == CV_8U &&
            (input.preview.channels() == 1 || input.preview.channels() == 3 ||
             input.preview.channels() == 4)) {
            cv::Mat bgr;
            if (input.preview.channels() == 3) {
                bgr = input.preview;
            } else if (input.preview.channels() == 1) {
                cv::cvtColor(input.preview, bgr, cv::COLOR_GRAY2BGR);
            } else {
                cv::cvtColor(input.preview, bgr, cv::COLOR_BGRA2BGR);
            }
            QImage frame(bgr.data, bgr.cols, bgr.rows,
                         static_cast<int>(bgr.step), QImage::Format_BGR888);
            cameraPreview_->setText(QString());
            cameraPreview_->setPixmap(QPixmap::fromImage(frame.copy()).scaled(
                cameraPreview_->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
        }

        if (input.status == ImageStatus::CameraError ||
            input.status == ImageStatus::ProcessingError) {
            stopCamera();
            cameraStatus_->setText(QString::fromStdString(input.message));
            return;
        }
        if (input.status == ImageStatus::MultipleCandidates) {
            cameraStatus_->setText(input.message.empty()
                                       ? QStringLiteral("Please scan only one barcode at a time.")
                                       : QString::fromStdString(input.message));
            return;
        }
        if (input.status == ImageStatus::NoCandidate) {
            cameraStatus_->setText(input.message.empty()
                                       ? QStringLiteral("No barcode found.")
                                       : QString::fromStdString(input.message));
            return;
        }
        if (input.status != ImageStatus::Ready) {
            if (!input.message.empty()) {
                cameraStatus_->setText(QString::fromStdString(input.message));
            }
            return;
        }

        const DecodeResult result = decodePreparedInput(input, *decoder_);
        if (result.found) {
            showDecodedCode(result);
        } else {
            cameraStatus_->setText(QString::fromStdString(result.message));
        }
    }

    void showDecodedCode(const DecodeResult &result)
    {
        stopCamera();
        foundCode_->setText(QString::fromStdString(result.code));
        foundStatus_->setText(QString::fromStdString(result.message));
        pages_->setCurrentWidget(foundPage_);
    }
};

} // namespace

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("Eyeris"));
    QApplication::setOrganizationName(QStringLiteral("Eyeris"));

    EyerisWindow window;
    window.show();
    return app.exec();
}
