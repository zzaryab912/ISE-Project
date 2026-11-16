# Increment 2 — team implementation proposal

**Working proposal — 15 November 2026.** This document specifies each person's responsibility, the data used, class data members, function parameters, return values and handoffs. It contains no implementation code.

References: [agreed features](Proposed%20System%20Design.md), [assignment requirements](Increment%202%20Requirements.md), [interface design](Interface%20Design.md), and [Figma](https://www.figma.com/design/eKMDVbIiBG4LTtJkC5r72U/Untitled?node-id=1-2).

## 1. Work division

| Person | Responsibility |
| --- | --- |
| **P1 — Zaryab** | Entire Qt interface, all pages/tabs, navigation, connecting functions, CMake and final application integration. |
| **P2 — Nosher** | Product catalogue operations, inventory rules, and product barcode generation/saving using the existing barcode functions. |
| **P3 — Khurram** | The four data files, relationships between their records, restoring complete saved transactions, and ID generation. |
| **P4 — Rayyan** | Current cart, quantity changes, pricing calculations and continuous camera scanning. |
| **P5 — Rayan Dar** | Bill generation, paid/on-hold transitions, payment recording, order history and archiving. |

Khurram owns the complete storage relationship: an order, its products and its bill must save and reload consistently. Each member checks their own component and supplies a small usage example before integration. Only Zaryab implements widgets.

## 2. Data shared by everyone

Use **PosTypes.h** for the business records and **IPosStore.h** for the storage interface. Zaryab (P1) prepares PosTypes.h; Khurram (P3) prepares IPosStore.h. These two headers and the agreed proposal form the shared baseline before Big Bang (Section 9). Each component owner creates their other headers and implementations on their own branch, following this proposal. Members must not create competing definitions or rename fields independently. ScanResult belongs in PosScanner.h because it uses the existing OpenCV reader contract.

### Exact C++ types

The parameter and return columns below use actual C++ types. `const T&` is a read-only reference, `T&` is a modifiable reference, and `T*` is a pointer. A type without either symbol is passed or returned by value.

| C++ type | Meaning |
| --- | --- |
| `std::string` | IDs, barcodes, names, messages, dates and stored relative paths. |
| `bool` | True or false. |
| `std::int64_t` | 64-bit integer. Money is paisa: 10000 means PKR 100.00. Quantity is thousandths: 2000 means two items or 2 kg. Count quantities must be multiples of 1000; weight is measured in kg with at most three decimal places. |
| `int` | Camera index. |
| `Unit` | Shared enum class with Count and Weight values. |
| `OrderStatus` | Shared enum class with InProgress, OnHold, Paid and Archived values. |
| `PaymentMode` | Shared enum class with Cash, Card and Wallet values. |
| `std::filesystem::path` | Application-data folder path. |
| `QImage` | Barcode image; a null image indicates generation failure. |
| `cv::Mat` | Camera pixels; returned pixels remain valid after the next frame is captured. |
| `Status` | Shared record with `bool success` and `std::string message`. |
| `Result<T>` | Shared result record with `bool success`, `std::string message` and `T value`. For example, `Result<Product>` contains a Product; `Result<std::vector<Product>>` contains a vector of Product. Use value only on success. |

These are the implementation conventions used throughout this proposal. Money inputs/files use PKR with two decimal places; quantity inputs/files use actual units with at most three decimal places. Convert to/from the integers above at the boundary. Do not reinterpret the same integer differently in another module.

### Product data members

| Member | C++ type | Meaning |
| --- | --- | --- |
| id | `std::string` | Automatically assigned product ID. |
| barcode | `std::string` | User-entered unique six-digit value, first digit nonzero; cannot change after creation. |
| acronym | `std::string` | Maximum eight characters. |
| name | `std::string` | Product name. |
| unit | `Unit` | Count or weight. |
| unitPrice | `std::int64_t` | Price per unit before tax, in paisa. |
| taxPerUnit | `std::int64_t` | Tax per unit in paisa, not a percentage. |
| discountPerUnit | `std::int64_t` | Discount per unit in paisa. |
| available | `std::int64_t` | Stock in thousandths; zero retains the product as Out of stock. |
| barcodeImagePath | `std::string` | Relative path including the saved barcode image filename. |

### Other shared records

| Record         | Data members with exact C++ types                                                                                                                                                                               |
| -------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| **CartItem**   | `Product product`; `std::int64_t quantity`. Copy Product on first addition to the row; retain its captured unit and pricing when the catalogue changes. Stock is checked against the live catalogue. |
| **Totals**     | `std::int64_t subtotal`; `std::int64_t tax`; `std::int64_t discount`; `std::int64_t payable`. All in paisa.                                                                                                     |
| **Customer**   | `std::string name`; `std::string contact`. Contact may be empty.                                                                                                                                                |
| **Payment**    | `bool recorded`; `std::int64_t amount`; `PaymentMode mode`; `std::string merchant`. Amount is paisa; held orders have recorded=false.                                                                           |
| **Timestamp**  | `std::string date`; `std::string time`, using YYYY-MM-DD and HH:mm:ss.                                                                                                                                          |
| **SaleIds**    | `std::string orderId`; `std::string billId`. Eight and nine digits respectively.                                                                                                                                |
| **SaleLine**   | `std::string productId`; `std::string barcode`; `std::string acronym`; `std::int64_t unitPrice`; `std::int64_t discountPerUnit`; `std::int64_t quantity`. These are saved purchase values.                      |
| **SaleRecord** | `SaleIds ids`; `Timestamp timestamp`; `Customer customer`; `Payment payment`; `OrderStatus status`; `std::vector<SaleLine> lines`; `Totals totals`. Represents an order and its bill together.                  |
| **StoreData**  | `std::vector<Product> products`; `std::vector<SaleRecord> sales`.                                                                                                                                               |
| **ScanResult** | `cv::Mat preview`; `DecodeResult decoded`; `bool codeChanged`. Existing DecodeResult contains `bool found`, `std::string message`, `std::string code`. Initialize unsuccessful results to false and empty code. |

Defaults: amounts and quantities zero, result/status success false, strings/lists empty, Payment recorded false. An active cart is InProgress; a saved sale is OnHold or Paid, and can later become Archived.

Parameter order follows each table from left to right. References to services are non-owning: the shared service objects must remain alive while their users exist. Qt child-widget pointers are owned through their parent widgets. Constructors have no return type; ordinary functions returning no value use `void`.

### Method qualifiers and header dependencies

These methods are `const` member functions; all other ordinary methods in the function tables are non-const. Constructors/destructors do not take this qualifier.

| Class | Const methods |
| --- | --- |
| ProductService | list, findByBarcode, findById, hasStock, deductStock |
| BarcodeAssetService | preview, save |
| IPosStore / PosStorage | read |
| IdGenerator | productId, saleIds |
| CartService | items, totals, lineTotals |
| PricingCalculator | calculate |
| HistoryService | list, get |

IPosStore declares read/save as public pure virtual methods and has a public virtual destructor defaulted in the header. PosStorage publicly inherits IPosStore and overrides both methods with the same parameter, return and const types. IdGenerator and PricingCalculator use parameterless constructors defaulted in their headers.

| Header | Includes / declarations it needs |
| --- | --- |
| PosTypes.h | `<cstdint>`, `<string>`, `<vector>`; business records/enums, Status and Result. |
| IPosStore.h | PosTypes.h; the storage interface. |
| ProductService.h | PosTypes.h, IPosStore.h; forward-declare BarcodeAssetService and IdGenerator. |
| BarcodeAssetService.h | PosTypes.h, `<QImage>`, `<filesystem>`. |
| PosStorage.h | IPosStore.h, `<filesystem>`. |
| IdGenerator.h / PricingCalculator.h | PosTypes.h. |
| CartService.h | PosTypes.h; forward-declare ProductService and PricingCalculator. |
| PosScanner.h | PosTypes.h, ReaderContracts.h; define ScanResult; forward-declare ImageSource and BarcodeDecoder. |
| CheckoutService.h / HistoryService.h | PosTypes.h; forward-declare the service types used by reference. |
| PosWindow.h | PosTypes.h, `<QMainWindow>`, `<QTimer>`; forward-declare the referenced services and child-widget pointer types. |

Each .cpp includes its own header first and the full headers of collaborators it calls. No .cpp is included inside another file.

## 3. P1 — Zaryab: interface and integration

**Proposed files:** PosWindow and page-widget files, main.cpp and CMakeLists.txt.

**Window class:** PosWindow, derived from Qt `QMainWindow`. Its child pages may organise the following state internally; the service types and function handoffs stay the same.

### State held by the window/pages

| Data member | C++ type | Purpose |
| --- | --- | --- |
| products_ | `ProductService&` | P2 catalogue service. |
| images_ | `BarcodeAssetService&` | P2 image helper. |
| cart_ | `CartService&` | P4 current cart. |
| scanner_ | `PosScanner&` | P4 scanner. |
| checkout_ | `CheckoutService&` | P5 bill generation. |
| history_ | `HistoryService&` | P5 saved-order actions. |
| selectedProduct_ | `Product` | Selected product copy. |
| hasSelectedProduct_ | `bool` | Whether a product is selected. |
| selectedOrderId_ | `std::string` | Selected history entry. |
| addQuantity_ | `std::int64_t` | Entered quantity in thousandths. |
| displayedBill_ | `SaleRecord` | Bill currently displayed. |
| hasDisplayedBill_ | `bool` | Whether a bill is displayed. |
| cameraTimer_ | `QTimer` | Camera polling timer. |
| pages_ | `QStackedWidget*` | Parent-owned page container. |
| currentOrderPage_, historyPage_, cataloguePage_ | `QWidget*` each | Parent-owned page widgets. |
| catalogueTabs_ | `QTabWidget*` | View Catalogue/Add Product tabs. |

Services are created before the pages and remain alive until the pages close. The pages display data and call functions; they do not calculate prices or open the text files.

### UI functions

Functions with no parameters read the appropriate fields from the page.

| Function | Parameters in order — exact C++ types | What it does | Return type |
| --- | --- | --- | --- |
| PosWindow — constructor | `ProductService& products`; `BarcodeAssetService& images`; `CartService& cart`; `PosScanner& scanner`; `CheckoutService& checkout`; `HistoryService& history`; `QWidget* parent` (default `nullptr`) | Store service references and create parent-owned page widgets. | No return type — constructor. |
| onCameraTick | None | Keep displaying the live preview and look up a changed barcode. No detection or a repeated product leaves the selection/quantity untouched and does not stop the camera. | `void` |
| onAddClicked | None | Pass barcode and quantity to cart addByBarcode. On success refresh items/totals, reset input to zero and disable Add; on failure show its message inline and keep the cart/input unchanged. | `void` |
| onCartQuantityChanged | `const std::string& productId`; `std::int64_t quantity` | Set the absolute row quantity through the cart. Zero removes it. | `void` |
| onGenerateBillClicked | None | Generate/save a Paid bill, then open Order History with that order selected. | `void` |
| onGenerateAndHoldClicked | None | Generate/save an OnHold bill with unrecorded payment, then open Order History with that order selected. | `void` |
| onOrderSelected | `const std::string& orderId` | Display the saved bill contents and the order date/time in the right column. | `void` |
| onMarkPaidClicked | None | Pass selected order ID/payment to markPaid. | `void` |
| onArchiveClicked | None | Archive selected order and refresh list. | `void` |
| onProductSelected | `const std::string& productId` | Display specifications, barcode image, filename and path. | `void` |
| onGeneratePreview | None | Generate the add-form barcode image. | `void` |
| onSaveProduct | None | Send completed Product to create. | `void` |
| onUpdateProduct | None | Send original Product and edited replacement to update. | `void` |
| onDeleteProduct | None | Pass selected product ID to remove. | `void` |
| showPage | `const std::string& pageName` | Switch sidebar page; start the camera/timer on entering Current Order and stop them on leaving it. | `void` |
| showCatalogueTab | `const std::string& tabName` | Switch View Catalogue/Add Product tab. | `void` |

Keep Description inline below quantity controls and Add anchored below it. Current Order has two bill buttons: **Generate Bill** saves the order as Paid; **Generate Bill and Put on Hold** saves it as OnHold. Both generate the complete bill and send the cashier to Order History with the saved order selected and its bill visible. Disable both buttons while saving. Read current cart items when a button is handled and clear only after success; failure keeps the cart available. **Keep the camera running for the entire time Current Order is open**, including waiting for a barcode, entering quantities, adding items and correcting input. Start it when the page first opens or is revisited; normal scan/add outcomes never stop it. Stop on leaving the page or closing the app; an actual camera/processing failure is reported inline. No animations or bill popup are needed.

## 4. P2 — Nosher: products and inventory

**Proposed files:** ProductService.h/.cpp and BarcodeAssetService.h/.cpp.

### ProductService

| Data member | C++ type | Purpose |
| --- | --- | --- |
| store_ | `IPosStore&` | Shared storage. |
| images_ | `BarcodeAssetService&` | Barcode image generation/saving. |
| ids_ | `const IdGenerator&` | ID generation. |

| Function | Parameters in order — exact C++ types | Description | Return type |
| --- | --- | --- | --- |
| ProductService — constructor | `IPosStore& store`; `BarcodeAssetService& images`; `const IdGenerator& ids` | Keep references to the supplied shared objects. | No return type — constructor. |
| list | None | Retrieve current catalogue products. | `Result<std::vector<Product>>` |
| findByBarcode | `const std::string& barcode` | Find matching product; fail if absent. | `Result<Product>` |
| findById | `const std::string& productId` | Find product by internal ID. | `Result<Product>` |
| create | `const Product& input` | Validate fields/barcode; assign ID; generate/save image; save product. Ignore supplied ID/path. Return saved ID/path with product. | `Result<Product>` |
| update | `const Product& original`; `const Product& replacement` | Check original still matches storage; save edits. Keep ID, barcode and path unchanged. | `Result<Product>` |
| remove | `const std::string& productId` | Reject deletion if an OnHold sale references the product; otherwise remove the catalogue entry, retaining past bills. | `Status` |
| hasStock | `const Product& product`; `std::int64_t requested` | Check positive, unit-valid quantity no greater than available stock. | `bool` |
| deductStock | `std::vector<Product>& products`; `const std::vector<SaleLine>& lines` | Validate all lines, then deduct in supplied list copy. No file writes; leave copy unchanged on failure. | `Status` |

Zero-stock products remain available for restocking by quantity edit. **Block deletion and measurement-unit changes while any OnHold sale references the product.** Return failure without changing data. These restrictions end when no held sale references it because those sales have been paid or archived. Other catalogue edits remain available; they do not rewrite existing cart rows or saved bills. Updated pricing applies to newly added rows and future orders. Barcode and ID remain fixed after creation.

### BarcodeAssetService

**Data member:** `dataRoot_` has type `std::filesystem::path` and stores the application-data folder.

| Function | Parameters in order — exact C++ types | Description | Return type |
| --- | --- | --- | --- |
| BarcodeAssetService — constructor | `std::filesystem::path dataRoot` | Store the folder path. | No return type — constructor. |
| preview | `const std::string& barcode` | Reuse encoder/renderer; return image in memory, null on failure. | `QImage` |
| save | `const std::string& barcode` | Generate and save PNG in managed folder; return relative path in value. | `Result<std::string>` |

Use existing generateBarcodeBits, codedString2Image, mat2QImage and saveBarcodeImage. The current renderer returns cv::Mat, then mat2QImage converts it for Qt. The barcode encodes its six-digit lookup value, not the product specifications.

## 5. P3 — Khurram: storage and IDs

**Proposed files:** IPosStore.h, PosStorage.h/.cpp and IdGenerator.h/.cpp.

### Storage interface and PosStorage

All services share one storage object through **IPosStore**. This interface has no data members and defines read and save. PosStorage implements it using the four text files; other members do not implement their own file handling.

| PosStorage data member | C++ type | Purpose |
| --- | --- | --- |
| dataRoot_ | `std::filesystem::path` | Application-data folder. |
| data_ | `StoreData` | Loaded records. |
| loaded_ | `bool` | Whether load succeeded; initially false. |

| Function | Parameters in order — exact C++ types | Description | Return type |
| --- | --- | --- | --- |
| PosStorage — constructor | `std::filesystem::path dataRoot` | Store folder path; set loaded_ false. | No return type — constructor. |
| load | None | Read four files and join order/detail/bill records. New empty folder is valid; malformed existing data fails. | `Status` |
| read | None | Return loaded data copy; also defined by IPosStore. | `Result<StoreData>` |
| save | `const StoreData& data` | Save products and associated transaction records; update loaded data on success. Also defined by IPosStore. | `Status` |

Saving a paid sale includes both stock and transaction changes. Updating a held sale preserves its IDs and lines. Reading after restart must restore those same relationships and amounts. Do not report success unless all required writes succeed; detailed crash recovery is outside this increment.

| File | What P3 saves/loads |
| --- | --- |
| Product.txt | Product specifications, stock and barcode image path. |
| Order.txt | IDs, date/time, customer, status and payment information. |
| OrderDetail.txt | Order ID, product ID, barcode and quantity for each purchased product. |
| Bill.txt | Bill/order IDs, customer, saved item pricing/quantities, totals and status. |

### File-format contract

Use UTF-8 and one record per physical line. **Enclose every string field in double quotation marks; spaces separate fields only outside those quotes.** This includes IDs, barcodes, acronyms, names, dates/times, contact, merchant and image paths. For example, "Black tea 250 g" is one field. Use `std::quoted` when writing and reading, including escaping quotes/backslashes; do not split rows at every space. Reject embedded line breaks. Enum tokens are unquoted lowercase: count/weight, on-hold/paid/archived and cash/card/wallet. Write money with exactly two decimals and quantities with up to three. A final # outside quotes ends Product, Order and Bill records; OrderDetail has no marker.

| File | Exact field order |
| --- | --- |
| Product.txt | id, barcode, acronym, name, unit, unitPrice, taxPerUnit, discountPerUnit, available, barcodeImagePath, # |
| Order.txt | orderId, date, time, customerName, customerContact, status, paymentAmount, paymentMode, merchant, # |
| OrderDetail.txt | orderId, productId, barcode, quantity |
| Bill.txt | billId, customerName, orderId; repeat acronym/unitPrice/discountPerUnit/quantity once per item; then subtotal, tax, discount, payable, status, # |

P3 writes both item sequences from the same SaleRecord line order and joins them by that order on reload. The chosen bill grammar has 8 + 4N fields before # for N items. Empty optional contact/merchant is an empty quoted string. Unpaid held payment is amount 0.00, mode `-`, merchant empty; `-` means not recorded and is never a paid payment mode. These are concrete implementation interpretations of the assignment's ambiguous format; retain the original requirements unchanged and check them with the instructor.

Selecting an order in History shows the contents of its saved bill, with the order date and time loaded from Order.txt. Saved acronyms, unit prices, discounts, quantities, totals and payment status provide the required bill information. Do not add historical full product names, measurement units or per-product tax solely for History. Historical prices and other bill values come from the saved transaction, never from the current catalogue; later product edits or deletion must not change old bills.

### IdGenerator

**Data members:** none. It uses the records supplied by the caller.

| Function | Parameters in order — exact C++ types | Description | Return type |
| --- | --- | --- | --- |
| productId | `const StoreData& data` | Choose next numeric product ID, checking current products and retained references. | `Result<std::string>` |
| saleIds | `const StoreData& data`; `const Timestamp& timestamp` | Generate eight-digit order and nine-digit bill IDs. Order ID ends with date's day; include archived IDs when finding next sequence. | `Result<SaleIds>` |

## 6. P4 — Rayyan: cart, pricing and scanning

**Proposed files:** CartService.h/.cpp, PricingCalculator.h/.cpp and PosScanner.h/.cpp.

### CartService

| Data member | C++ type | Purpose |
| --- | --- | --- |
| products_ | `ProductService&` | Product lookup/stock rules. |
| pricing_ | `const PricingCalculator&` | Shared calculator. |
| items_ | `std::vector<CartItem>` | Current cart entries. |

| Function | Parameters in order — exact C++ types | Description | Return type |
| --- | --- | --- | --- |
| CartService — constructor | `ProductService& products`; `const PricingCalculator& pricing` | Keep references; begin with empty items_. | No return type — constructor. |
| addByBarcode | `const std::string& barcode`; `std::int64_t addition` | Read live product/stock; check existing row quantity plus addition before merging. Capture unit/pricing only for a new row; retain an existing row's snapshot. | `Status` |
| setQuantity | `const std::string& productId`; `std::int64_t quantity` | Set absolute quantity after live stock/unit checks; retain captured pricing. Zero removes the row even if the catalogue product was deleted or its unit changed. | `Status` |
| items | None | Return cart entries by value. | `std::vector<CartItem>` |
| totals | None | Calculate current totals. | `Result<Totals>` |
| lineTotals | `const std::string& productId` | Find the cart row and calculate its subtotal, tax, discount and payable using the shared calculator. Return failure/message if the row does not exist. | `Result<Totals>` |
| clear | None | Empty cart after successful bill generation. | `void` |

lineTotals is a const method. Pass the matching CartItem as a one-item vector to PricingCalculator::calculate and return its result; do not duplicate the arithmetic. P1 calls lineTotals for each cart row and displays its subtotal in the Amount column, while totals supplies the whole-order summary. Both use the row's captured rates and the same rounding rules. This adds no data members or shared record types.

Keep each row's captured unit, unit price, tax per unit and discount per unit fixed. Later catalogue price edits do not reprice that row or prevent billing it. Repeat additions merge into the same row using its captured rates; a product not yet in the cart uses the current catalogue values when first added. Available stock is always checked live, rather than taken from the stored Product copy.

For addByBarcode, **required quantity = quantity already in the row + addition**. P4 passes that combined quantity to P2's stock check. If it exceeds current stock, return failure without changing the row or totals. Example Status message: **"Insufficient stock: 4 already in order + 2 requested exceeds 5 available."** P1 displays the warning inline and keeps the entered quantity available for correction. Format quantities in their displayed units, not the internal thousandths.

If a live product's Count/Weight unit differs from the captured cart unit, positive quantity changes and bill generation fail without altering the snapshot. Warning: **"Product measurement unit has changed. Restore the original unit or remove this item from the order."** Zero still removes the row. Never reinterpret an existing quantity as another unit. Saved and held bills remain unchanged; held-product unit edits are already blocked by P2.

### PricingCalculator

**Data members:** none. It changes neither cart nor files.

| Function | Parameters in order — exact C++ types | Description | Return type |
| --- | --- | --- | --- |
| calculate | `const std::vector<CartItem>& items` | Calculate subtotal/tax/discount/payable. Empty cart gives zero totals. | `Result<Totals>` |

Calculation rule: for each line, multiply each paisa-per-unit rate by integer quantity, divide by 1000 and round to the nearest paisa, with exactly half a paisa rounded upward. Calculate base, tax and discount separately, then sum the rounded columns. Tax is based on full quantity before discount; payable is subtotal plus tax minus discount. Rates/quantities are nonnegative and unit discount cannot exceed unit price. Reject invalid values/overflow. Use the same calculator for display and billing; keep count totals separate from kg totals.

### PosScanner

| Data member | C++ type | Purpose |
| --- | --- | --- |
| source_ | `ImageSource&` | Existing camera source. |
| decoder_ | `const BarcodeDecoder&` | Existing decoder. |
| lastCode_ | `std::string` | Last decoded value. |
| running_ | `bool` | Whether scanner is running. |

| Function | Parameters in order — exact C++ types | Description | Return type |
| --- | --- | --- | --- |
| PosScanner — constructor | `ImageSource& source`; `const BarcodeDecoder& decoder` | Keep references; initialise running_ false and lastCode_ empty. | No return type — constructor. |
| ~PosScanner — destructor | None | Call stop before destruction. | No return type — destructor. |
| start | `int cameraIndex` | Start shared camera source for the Current Order session; reset lastCode_ for a new session. | `Status` |
| poll | None | Read/decode without stopping on normal scan outcomes. Valid frames return success and preview, including frames without a readable barcode. | `Result<ScanResult>` |
| stop | None | Stop/release camera; safe to repeat. | `void` |

P1 supplies the only polling timer and keeps it active while Current Order is open. Unlike the Increment 1 result screen, successful decoding does not stop the camera. P1 replaces the selection only for a different recognised product; repeated readings leave its entered quantity untouched.

For a valid frame with no readable barcode or multiple candidates, return success=true with the preview, decoded.found=false, empty decoded.code and codeChanged=false. Guidance may be returned in the result message. Keep the camera running and leave lastCode_ unchanged so removing/re-presenting the same barcode does not reset the selection. For a decoded code, found=true; codeChanged is true only when it differs from lastCode_, which is then updated. Camera/frame-processing failures return success=false with a message; P1 shows the error inline, and the failed session is stopped rather than reported as an ordinary no-detection frame.

## 7. P5 — Rayan Dar: bills and history

**Proposed files:** CheckoutService.h/.cpp and HistoryService.h/.cpp.

### CheckoutService

| Data member | C++ type | Purpose |
| --- | --- | --- |
| store_ | `IPosStore&` | Shared storage. |
| products_ | `ProductService&` | Product/stock operations. |
| pricing_ | `const PricingCalculator&` | Shared calculator. |
| ids_ | `const IdGenerator&` | ID generation. |

| Function | Parameters in order — exact C++ types | Description | Return type |
| --- | --- | --- | --- |
| CheckoutService — constructor | `IPosStore& store`; `ProductService& products`; `const PricingCalculator& pricing`; `const IdGenerator& ids` | Keep references to shared objects. | No return type — constructor. |
| generateBill | `const std::vector<CartItem>& items`; `const Customer& customer`; `const Payment& payment`; `OrderStatus status`; `const Timestamp& timestamp` | Accept Paid/OnHold only. Validate captured item values and live product identity/unit/stock; calculate using captured rates, obtain IDs, save order/bill and return the saved record. | `Result<SaleRecord>` |

P1 passes an unmodified snapshot returned by CartService::items; widgets must not construct or replace product rates. Those captured rates are authoritative for this cart. P5 validates their numeric values and uses the shared calculator, but does not compare them with current catalogue prices or replace them when prices change. Validate live product IDs/barcodes, matching measurement units and sufficient current stock before saving; a deleted product, unit mismatch or insufficient stock returns failure with the cart unchanged.

For **Paid**, payment is recorded and stock is deducted through P2 in the same data copy saved through P3. For **OnHold**, generate/save the complete bill but do not deduct or reserve stock. A later payment uses that saved bill's values, never current catalogue prices. Payment amount is the amount applied to the bill and must equal payable; cash tender/change handling is outside this flow.

### HistoryService

| Data member | C++ type | Purpose |
| --- | --- | --- |
| store_ | `IPosStore&` | Shared storage. |
| products_ | `ProductService&` | Stock operations. |

| Function | Parameters in order — exact C++ types | Description | Return type |
| --- | --- | --- | --- |
| HistoryService — constructor | `IPosStore& store`; `ProductService& products` | Keep references. | No return type — constructor. |
| list | None | Retrieve non-archived orders/bills. | `Result<std::vector<SaleRecord>>` |
| get | `const std::string& orderId` | Retrieve the saved bill and its order date/time, without rebuilding prices from the catalogue. | `Result<SaleRecord>` |
| markPaid | `const std::string& orderId`; `const Payment& payment` | Recheck stock, deduct once, save payment/status. Preserve IDs, items and saved financial values. | `Result<SaleRecord>` |
| archive | `const std::string& orderId` | Archive/hide order, retaining records and stock. | `Status` |

Marking an already-paid order with identical payment details returns success and the existing SaleRecord without another deduction. Different payment details return failure without changes. Archived orders cannot be marked paid. Archiving an already-archived record returns success without changes. **An on-hold bill cannot be edited:** its items, quantities and saved charges stay fixed. Mark as Paid performs a fresh stock check, records payment and changes status using the same order/bill IDs; it does not reprice the bill. Archive remains available. History is otherwise read-only; no resume, search, filters or restore feature is included.

### Common boundary outcomes

| Situation | Caller receives / required effect |
| --- | --- |
| Missing product/order, invalid input or stale edited catalogue product | success=false, explanatory message; Result value is ignored. No cart/business-data mutation. |
| Catalogue prices change after a cart row was added | Keep the row's captured rates and bill using them; updated prices apply to new rows. Always recheck live stock. |
| Live product unit differs from an existing cart row's unit | success=false for positive quantity edits or billing, with the unit warning above. Keep the snapshot unchanged; zero can still remove the row. |
| Insufficient stock at add or payment | success=false; no partial quantity changes or deduction. Held bill remains held. |
| Delete or change the measurement unit of a product referenced by an OnHold sale | success=false; product and saved bill remain unchanged. |
| Successful lookup of an empty catalogue/history | success=true and empty vector, not an error. |
| Barcode preview failure | Null QImage; keep current form. |
| File/image write failure | success=false; do not show a saved/paid result or clear cart. Stop automatic retries; storage must reload/validate before another save if its state is uncertain. |

Detailed recovery and input messages can be added later; the common success flag and outcomes above are part of the function contract.

## 8. Handoffs and integration

| Operation | Calls and data passed |
| --- | --- |
| Scan product | P1 calls P4 poll; decoded barcode goes to P2 findByBarcode; returned Product fills the right panel. |
| Add/change quantity | P1 calls P4 addByBarcode/setQuantity, reads items, calls lineTotals with each product ID for row amounts, and calls totals for the whole-order summary. |
| Generate paid/held bill | P1 passes cart items, customer, payment, status and timestamp to P5. P5 uses P4 calculate, P3 IDs and P2 stock rules, then P3 save. On success P1 clears the cart and opens History with the returned SaleRecord selected and displayed. |
| Pay held bill | P1 passes order ID/payment to P5 markPaid. P5 updates stock and the existing saved records through P2/P3; no new bill IDs. |
| Manage catalogue | P1 calls P2 product/image functions. P2 uses P3 storage/IDs and returns updated data for display. |

P1 creates storage and calls load first, then creates helpers/services and finally pages. All members use that same storage instance. Keep public names, field names, parameter order and return types in this document consistent; review changes with affected members before implementing them.

**SE principles:** each class has a focused job; widgets do not contain file handling or pricing; both billing and cart reuse one calculator; barcode functions are reused. The storage interface lets P3 change file formatting without rewriting the other services.

Integrate product save/load and cart calculation early, then bill/history and camera. Each member demonstrates their public functions; another member checks the result. Test the complete paid/held/payment/archive and save/reopen flows together before the November 19 demo. Include **Error handling on input**. The numeric/file/API rules above are the implementation baseline; History uses the required saved bill information, and the held-product deletion/unit restrictions are confirmed.

## 9. Big Bang — ready for individual branches

**Big Bang** is the point when the agreed proposal, completed PosTypes.h and completed IPosStore.h have been checked and published on `increment_2`, and everyone can start their own branch from that same commit. Only these two new headers are required before Big Bang. The remaining service headers and implementations are each owner's work on their individual branch, using this proposal as the contract.

All new headers belong in the repository root, alongside the existing source files, with the exact filename casing shown. Keep types/classes in the global namespace. Shared records expose public fields; service methods are public and service data members are private. Use the exact names, C++ types, parameter order, reference qualifiers, return types and const methods specified in this proposal.

### Before Big Bang

- [ ] Agree on the function/data contracts in this proposal. Any later public-interface change must be coordinated with affected owners and reflected here.
- [ ] **Zaryab (P1):** complete PosTypes.h using the contents specified below.
- [ ] **Khurram (P3):** complete IPosStore.h using the contents specified below.
- [ ] P1 and P3 compare both headers with the proposal. Confirm each header compiles individually and both compile together under C++17; verify the existing application still builds. These two headers need only the standard library.
- [ ] P1 publishes the proposal and both headers on `increment_2` and records the starting commit. Everyone creates their branch from that commit.

### After Big Bang — headers created on individual branches

These are the remaining header responsibilities, not additional Big Bang prerequisites. Each owner implements their corresponding .cpp files on the same branch.

- [ ] **ProductService.h — Nosher (P2):** declare its constructor, list, findByBarcode, findById, create, update, remove, hasStock and deductStock. Include the store_, images_ and ids_ reference members from Section 4.
- [ ] **BarcodeAssetService.h — Nosher (P2):** declare its constructor, preview and save, with the dataRoot_ path member from Section 4.
- [ ] **PosStorage.h — Khurram (P3):** declare public inheritance from IPosStore, its constructor and load, plus read/save overrides. Include dataRoot_, data_ and loaded_ from Section 5.
- [ ] **IdGenerator.h — Khurram (P3):** provide the defaulted constructor and declare productId and saleIds from Section 5; no data members.
- [ ] **CartService.h — Rayyan (P4):** declare its constructor, addByBarcode, setQuantity, items, totals, lineTotals and clear. Include products_, pricing_ and items_ from Section 6.
- [ ] **PricingCalculator.h — Rayyan (P4):** provide the defaulted constructor and declare calculate from Section 6; no data members.
- [ ] **PosScanner.h — Rayyan (P4):** define ScanResult, reusing DecodeResult from ReaderContracts.h. Declare the constructor, destructor, start, poll and stop, with source_, decoder_, lastCode_ and running_ from Section 6.
- [ ] **CheckoutService.h — Rayan Dar (P5):** declare its constructor and generateBill, with store_, products_, pricing_ and ids_ from Section 7.
- [ ] **HistoryService.h — Rayan Dar (P5):** declare its constructor, list, get, markPaid and archive, with store_ and products_ from Section 7.

PosWindow.h and any page-widget headers remain P1's UI work from Section 3; other members do not depend on their internal layout, so they can be created on P1's branch after Big Bang. Khurram implements creation/loading of the four runtime data files in his storage work; those files are not header prerequisites. P1 supplies the same application-data root to storage and barcode-image services during integration.

Members can write their implementations against the agreed contracts before another component exists. Compiling a call to another component needs its matching header; linking the application needs its implementation. Coordinate those files when checking components together. Keep one copy of ReaderContracts.h and reuse the Increment 1 barcode/camera headers. CMake must not reference missing implementations.

### Required contents of PosTypes.h — Zaryab (P1)

**Path:** PosTypes.h in the repository root. Add an include guard or `#pragma once` and include `<cstdint>`, `<string>` and `<vector>`. Define the following enums, public data records and template in the header, in dependency order. Complete definitions must be available to everyone who includes it; no PosTypes.cpp is needed.

| Enum class | Exact values | Default for record fields |
| --- | --- | --- |
| Unit | Count, Weight | `Unit::Count` |
| OrderStatus | InProgress, OnHold, Paid, Archived | `OrderStatus::InProgress` |
| PaymentMode | Cash, Card, Wallet | `PaymentMode::Cash` |

| Record / template | Required public data members — exact C++ types |
| --- | --- |
| Status | `bool success`; `std::string message`. |
| Result<T> | `bool success`; `std::string message`; `T value`. Define it as a class template with public fields (a struct template is suitable). |
| Product | `std::string id`; `std::string barcode`; `std::string acronym`; `std::string name`; `Unit unit`; `std::int64_t unitPrice`; `std::int64_t taxPerUnit`; `std::int64_t discountPerUnit`; `std::int64_t available`; `std::string barcodeImagePath`. |
| CartItem | `Product product`; `std::int64_t quantity`. |
| Totals | `std::int64_t subtotal`; `std::int64_t tax`; `std::int64_t discount`; `std::int64_t payable`. |
| Customer | `std::string name`; `std::string contact`. |
| Payment | `bool recorded`; `std::int64_t amount`; `PaymentMode mode`; `std::string merchant`. |
| Timestamp | `std::string date`; `std::string time`. |
| SaleIds | `std::string orderId`; `std::string billId`. |
| SaleLine | `std::string productId`; `std::string barcode`; `std::string acronym`; `std::int64_t unitPrice`; `std::int64_t discountPerUnit`; `std::int64_t quantity`. |
| SaleRecord | `SaleIds ids`; `Timestamp timestamp`; `Customer customer`; `Payment payment`; `OrderStatus status`; `std::vector<SaleLine> lines`; `Totals totals`. |
| StoreData | `std::vector<Product> products`; `std::vector<SaleRecord> sales`. |

Initialise numeric fields to zero, bool fields to false, strings/vectors to empty, and enum fields to the defaults above. Value-initialise nested records and Result's value; callers must ignore value when success is false. Payment mode is ignored when recorded is false.

Money is stored as `std::int64_t` paisa. Stock and purchased quantities are `std::int64_t` thousandths: Count must be a multiple of 1000; Weight uses kg. IDs/barcodes remain strings. Timestamp date/time use YYYY-MM-DD and HH:mm:ss. These are the same conventions as Section 2.

This header contains shared data definitions. Pricing, validation, file handling, widgets and camera operations belong to the services. ScanResult belongs in PosScanner.h and reuses ReaderContracts.h; do not add Qt/OpenCV dependencies to PosTypes.h.

### Required contents of IPosStore.h — Khurram (P3)

**Path:** IPosStore.h in the repository root. Add an include guard or `#pragma once` and include PosTypes.h. Define IPosStore as an abstract class in the global namespace, with no data members and the following public members.

| Member | Parameters — exact C++ types | Return type | Qualifiers / required behaviour |
| --- | --- | --- | --- |
| read | None | `Result<StoreData>` | Const, virtual and pure virtual. Return a copy of the loaded records on success; return failure/message if storage is unavailable. |
| save | `const StoreData& data` | `Status` | Non-const, virtual and pure virtual. Accept the complete proposed store contents; report success only when the required saves succeed. |
| ~IPosStore | None | No return type — destructor. | Public, virtual and defaulted in this header, so destruction through the interface is safe. |

The interface supplies the declarations of read/save; their actual file operations are implemented later by PosStorage in Khurram's branch. PosStorage must override the same parameter, return and const types. No IPosStore.cpp, storage data members, parsing code or pricing logic is required in the interface header.

**Current status:** PosTypes.h and IPosStore.h have not yet been created or compile-checked, so Big Bang has not occurred.
