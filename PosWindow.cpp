#include "PosWindow.h"
#include "CartService.h"
#include "PosScanner.h"
#include "ProductService.h"

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
          cameraTimer_.setInterval(200);                                                // sets timer tick every 200 ms
          cameraTimer_.setTimerType(Qt::CoarseTimer);                                   // camera checked rougly five times a second
          connect(&cameraTimer_, &QTimer::timeout, this, &PosWindow::onCameraTick);     // on each camera tick, call onCameraTick
}

void PosWindow::onCameraTick() {                                        // called on each tick
    const Result<ScanResult> scane = scanner_.poll();                   // poll asks scanner to read one frame and check for a barcode, returns a result object

    if (!scane.success) {       // checks whether reading th eframe failed
        cameraTimer_.stop();
        scanner_.stop();
        return;                 // camera keeps running if theres no barcode scanned.
    }

    if (!scane.value.decoded.found || !scane.value.codeChanged) {                           // if no new barcode is found, return, keep scanning
        return;
    }

    const Result<Product> product = products_.findByBarcode(scane.value.decoded.code);      // looks up barcode in the catalogue. If there is no match, selection stays as it is
    if (!product.success) {
        return;
    }

    if (hasSelectedProduct_ && selectedProduct_.id == product.value.id) {                   // preserves teh entered quantity if lookup returns product already selected
        return;
    }

    // for a different recognised product: this is the new selection now, and the addQuantity option is now set to zero again
    selectedProduct_ = product.value;
    hasSelectedProduct_ = true;
    addQuantity_ = 0;
}

// when clicking on Add to order
void PosWindow::onAddClicked() {
    if (!hasSelectedProduct_ || addQuantity_ <= 0) {                                        // if product isnt selected, or quantity to be added is zero or smaller
        return;
    }

    const Status result = cart_.addByBarcode(selectedProduct_.barcode, addQuantity_);       // if add fails, it stops trying to add so cashier can change quantity manually. super neat right?
    if (!result.success) {
        return;
    }

    addQuantity_ = 0;
}

void PosWindow::onCartQuantityChanged(const string& productId, int64_t quantity) {
    const Status result = cart_.setQuantity(productId, quantity);
    if (!result.success) {
        return;
    }
}
