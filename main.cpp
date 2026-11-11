#include <QApplication>
#include <QFileDialog>
#include <QFileInfo>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QMessageBox>
#include <QPixmap>
#include <QPushButton>
#include <QString>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>
#include <string>
#include "barcodeGenerator.h"
#include "BarcodePlotter.h"
#include "BarcodeImageIO.h"
#include "ReaderContracts.h"
#include "BarcodeDecoder.h"
#include "camera-harness.h"

using namespace std;

bool isValidGuiInput(const QString &input)              // does the text meet the 6 digit requirement? 
{                                                       // qstring is a qtClass for holding string
    
    // first checking for the first digit being between 1 and 9
    
    if (input.size() != 6 || input[0] < QChar('1') || input[0] > QChar('9')) {
        return false;
    }
    
    // going through the rest of the string to make sure values are in range

    for (int i = 0; i < input.size(); i++) {
        if (input[i] < QChar('0') || input[i] > QChar('9')) {
            return false;
        }
    }

    return isValid6DigitInput(input.toStdString());
}

string generateVerifiedBits(const QString &input)      // person 2 accepts string. Qtinput gives us qstring in text field
{
    if (!isValidGuiInput(input)) {  // validate input
        return "";
    }

    string code = input.toStdString();                 // function to convert qstring to string
    string bits = generateBarcodeBits(code);

    if (bits.empty() || !verifyEncoding(code, bits)) {
        return "";
    }

    return bits;
}

QImage generateBarcodeImage(const QString &input)       // 
{
    string bits = generateVerifiedBits(input);          // converts the qstring input to string
                                                        // generates a binary string for the input

    if (bits.empty()) {
        return QImage();
    }

    return codedString2Image(bits, input, true);        // calls codedString2Image function to convert binary to image
}

QString pngSavePath(const QString &chosenPath)
{
    if (chosenPath.isEmpty()) {
        return "";
    }

    if (chosenPath.endsWith(".png", Qt::CaseInsensitive)) {
        return chosenPath;
    }

    return chosenPath + ".png";
}

bool saveGeneratedImage(const QImage &image, const QString &confirmedPath)
{
    return saveBarcodeImage(image, confirmedPath);
}

void chooseAndSaveBarcode(const QImage &image, QWidget *parent)
{
    if (image.isNull()) {
        QMessageBox::warning(parent, "Save barcode", "Generate a barcode first.");
        return;
    }

    QString chosenPath = QFileDialog::getSaveFileName(
        parent, "Save barcode", "", "PNG images (*.png)", nullptr,
        QFileDialog::DontConfirmOverwrite | QFileDialog::DontUseNativeDialog);

    if (chosenPath.isEmpty()) {
        return;
    }

    QString path = pngSavePath(chosenPath);

    if (QFileInfo::exists(path)) {
        QMessageBox::StandardButton answer = QMessageBox::question(
            parent, "Replace file", "This file already exists. Replace it?",
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);

        if (answer != QMessageBox::Yes) {
            return;
        }
    }

    if (saveGeneratedImage(image, path)) {
        QMessageBox::information(parent, "Save barcode", "Barcode saved successfully.");
    } else {
        QMessageBox::warning(parent, "Save barcode", "Could not save the barcode. Please try again.");
    }
}

DecodeResult decodePreparedInput(const InputResult &input, const BarcodeDecoder &decoder)
{
    if (input.status == ImageStatus::NoCandidate) {
        return {false, "No barcode found.", ""};
    }

    if (input.status == ImageStatus::MultipleCandidates) {
        return {false, "Please scan only one barcode at a time.", ""};
    }

    if (input.status != ImageStatus::Ready) {
        return {false, input.message, ""};
    }

    return decoder.decode(input.barcode);
}

void uploadAndDecode(ImageSource &source, const BarcodeDecoder &decoder,
                     QWidget *parent, QLabel *status)
{
    source.stopCamera();

    QString path = QFileDialog::getOpenFileName(
        parent, "Open barcode image", "", "Images (*.png *.jpg *.jpeg)");

    if (path.isEmpty()) {
        return;
    }

    InputResult input = source.loadFile(path);
    DecodeResult result = decodePreparedInput(input, decoder);

    if (result.found) {
        status->setText("Decoded barcode: " + QString::fromStdString(result.code));
    } else {
        status->setText(QString::fromStdString(result.message));
    }
}

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);  // gives access to startup arguments as text
                                   // revisit this later
                                   // app object created, allows to work with window, keep event loop running
                                   
    QMainWindow window;            // window object created
    window.setWindowTitle("Barcode App");
    window.resize(600, 400);

    QWidget *content = new QWidget;                             // QWidget is a content box. It contains our contents to display
    QVBoxLayout *layout = new QVBoxLayout(content);             // creates a vertical layout (section)
    QPushButton *generateButton = new QPushButton("Generate barcode");
    QPushButton *readButton = new QPushButton("Read barcode");
    QLineEdit *codeInput = new QLineEdit;
    codeInput->setPlaceholderText("Enter six digits");
    QPushButton *createButton = new QPushButton("Generate");
    QPushButton *saveButton = new QPushButton("Save PNG");
    QPushButton *uploadButton = new QPushButton("Open image");
    QPushButton *startButton = new QPushButton("Start camera");
    QPushButton *stopButton = new QPushButton("Stop camera");
    QLabel *preview = new QLabel;
    QLabel *status = new QLabel;

    layout->addWidget(generateButton);
    layout->addWidget(readButton);
    layout->addWidget(codeInput);
    layout->addWidget(createButton);
    layout->addWidget(preview);
    layout->addWidget(status);
    layout->addWidget(saveButton);
    layout->addWidget(uploadButton);
    layout->addWidget(startButton);
    layout->addWidget(stopButton);
    layout->addStretch();
    window.setCentralWidget(content);                           // add content to the main window. 

    codeInput->hide();
    createButton->hide();
    preview->hide();
    status->hide();
    saveButton->hide();
    uploadButton->hide();
    startButton->hide();
    stopButton->hide();
    saveButton->setEnabled(false);
    stopButton->setEnabled(false);

    QImage generatedImage;
    QString generatedCode;
    ImageSource source;
    BarcodeDecoder decoder;
    QTimer cameraTimer;

    QObject::connect(generateButton, &QPushButton::clicked, &window,
                     [=, &source, &cameraTimer, &generatedImage, &generatedCode]() {
        cameraTimer.stop();
        source.stopCamera();
        startButton->setEnabled(true);
        stopButton->setEnabled(false);
        uploadButton->hide();
        startButton->hide();
        stopButton->hide();
        codeInput->show();
        createButton->show();
        preview->show();
        status->show();
        saveButton->show();
        if (generatedImage.isNull()) {
            preview->clear();
            status->clear();
        } else {
            preview->setPixmap(QPixmap::fromImage(generatedImage));
            status->setText("Barcode for " + generatedCode + " generated. Save it as PNG.");
        }
    });

    QObject::connect(readButton, &QPushButton::clicked, &window,
                     [=, &source, &cameraTimer]() {
        cameraTimer.stop();
        source.stopCamera();
        startButton->setEnabled(true);
        stopButton->setEnabled(false);
        codeInput->hide();
        createButton->hide();
        saveButton->hide();
        preview->clear();
        preview->show();
        status->setText("Barcode preparation is awaiting Person 4's implementation.");
        status->show();
        uploadButton->show();
        startButton->show();
        stopButton->show();
    });

    QObject::connect(uploadButton, &QPushButton::clicked, &window,
                     [=, &window, &source, &decoder, &cameraTimer]() {
        cameraTimer.stop();
        startButton->setEnabled(true);
        stopButton->setEnabled(false);
        uploadAndDecode(source, decoder, &window, status);
    });

    QObject::connect(startButton, &QPushButton::clicked, &window,
                     [=, &source, &cameraTimer]() {
        string message;
        if (!source.startCamera(0, message)) {
            status->setText(QString::fromStdString(message));
            return;
        }

        status->setText("Scanning...");
        startButton->setEnabled(false);
        stopButton->setEnabled(true);
        cameraTimer.start(200);
    });

    QObject::connect(stopButton, &QPushButton::clicked, &window,
                     [=, &source, &cameraTimer]() {
        cameraTimer.stop();
        source.stopCamera();
        startButton->setEnabled(true);
        stopButton->setEnabled(false);
        status->setText("Camera stopped.");
    });

    QObject::connect(&cameraTimer, &QTimer::timeout, &window,
                     [=, &source, &decoder, &cameraTimer]() {
        InputResult input = source.nextFrame();

        if (!input.preview.empty()) {
            QImage frame(input.preview.data, input.preview.cols, input.preview.rows,
                         static_cast<int>(input.preview.step), QImage::Format_BGR888);
            preview->setPixmap(QPixmap::fromImage(frame.copy()));
        }

        if (input.status == ImageStatus::CameraError ||
            input.status == ImageStatus::ProcessingError) {
            cameraTimer.stop();
            source.stopCamera();
            startButton->setEnabled(true);
            stopButton->setEnabled(false);
            status->setText(QString::fromStdString(input.message));
            return;
        }

        if (input.status == ImageStatus::MultipleCandidates) {
            status->setText("Please scan only one barcode at a time.");
            return;
        }

        if (input.status != ImageStatus::Ready) {
            return;
        }

        DecodeResult result = decodePreparedInput(input, decoder);
        if (result.found) {
            cameraTimer.stop();
            source.stopCamera();
            startButton->setEnabled(true);
            stopButton->setEnabled(false);
            status->setText("Decoded barcode: " + QString::fromStdString(result.code));
        }
    });

    QObject::connect(createButton, &QPushButton::clicked, &window,
                     [=, &generatedImage, &generatedCode]() {
        QString input = codeInput->text();
        if (!isValidGuiInput(input)) {
            status->setText("Enter exactly six digits, starting with 1-9.");
            return;
        }

        QImage image = generateBarcodeImage(input);
        if (image.isNull()) {
            status->setText("Could not generate the barcode. Please try again.");
            return;
        }

        generatedImage = image;
        generatedCode = input;
        preview->setPixmap(QPixmap::fromImage(generatedImage));
        status->setText("Barcode for " + generatedCode + " generated. Save it as PNG.");
        saveButton->setEnabled(true);
    });

    QObject::connect(saveButton, &QPushButton::clicked, &window,
                     [&window, &generatedImage]() {
        chooseAndSaveBarcode(generatedImage, &window);
    });
    
    window.show();

    // above function calls modify the size, title of the window
    // Show function shows the window

    return app.exec(); // app exec runs the app, and return happens when the app finishes running
    // int return type
    // while app.exec runs, it responds to the user
}
