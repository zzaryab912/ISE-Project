#include <QApplication>
#include <QImage>
#include <QMainWindow>
#include <QPushButton>
#include <QString>
#include <QVBoxLayout>
#include <QWidget>
#include <string>
#include "barcodeGenerator.h"
#include "BarcodePlotter.h"

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
    layout->addWidget(generateButton);
    layout->addWidget(readButton);
    layout->addStretch();
    window.setCentralWidget(content);                           // add content to the main window. 
    
    window.show();

    // above function calls modify the size, title of the window
    // Show function shows the window

    return app.exec(); // app exec runs the app, and return happens when the app finishes running
    // int return type
    // while app.exec runs, it responds to the user
}
