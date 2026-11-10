#include <QApplication>
#include <QMainWindow>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);  // gives access to startup arguments as text
                                   // revisit this later
                                   // app object created, allows to work with window, keep event loop running
                                   
    QMainWindow window;            // window object created
    window.setWindowTitle("Barcode App");
    window.resize(600, 400);
    window.show();

    // above function calls modify the size, title of the window
    // Show function shows the window

    return app.exec(); // app exec runs the app, and return happens when the app finishes running
    // int return type
    // while app.exec runs, it responds to the user
}
