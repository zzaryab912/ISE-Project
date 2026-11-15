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

Use one agreed **PosTypes.h**. P1 maintains this shared file; members must not create competing definitions or rename fields independently.

### Exact C++ types

The parameter and return columns below use actual C++ types. `const T&` is a read-only reference, `T&` is a modifiable reference, and `T*` is a pointer. A type without either symbol is passed or returned by value.

| C++ type | Meaning |
| --- | --- |
| `std::string` | IDs, barcodes, names, messages, dates and stored relative paths. |
| `bool` | True or false. |
| `std::int64_t` | 64-bit integer. Proposed money representation: paisa, so 10000 means PKR 100.00. Proposed quantity representation: thousandths, so 2000 means two items or 2 kg. Count must be whole items; confirm the kg convention together. |
| `int` | Camera index. |
| `Unit` | Shared enum class with Count and Weight values. |
| `OrderStatus` | Shared enum class with InProgress, OnHold, Paid and Archived values. |
| `PaymentMode` | Shared enum class with Cash, Card and Wallet values. |
| `std::filesystem::path` | Application-data folder path. |
| `QImage` | Barcode image; a null image indicates generation failure. |
| `cv::Mat` | Camera pixels; returned pixels remain valid after the next frame is captured. |
| `Status` | Shared record with `bool success` and `std::string message`. |
| `Result<T>` | Shared result record with `bool success`, `std::string message` and `T value`. For example, `Result<Product>` contains a Product; `Result<std::vector<Product>>` contains a vector of Product. Use value only on success. |

The numeric representation is a proposed convention; all members must use the same one. It is written explicitly as `std::int64_t` throughout the tables, rather than hidden behind Money or Quantity labels.

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
| **CartItem**   | `Product product`; `std::int64_t quantity`. Product is copied when added.                                                                                                                                       |
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
| onCameraTick | None | Call scanner poll, display preview, and look up a changed barcode. Repeated readings leave quantity untouched. | `void` |
| onAddClicked | None | Pass barcode and quantity to cart addByBarcode. Refresh items/totals; successful add resets input to zero and disables Add. | `void` |
| onCartQuantityChanged | `const std::string& productId`; `std::int64_t quantity` | Set the absolute row quantity through the cart. Zero removes it. | `void` |
| onGenerateBillClicked | None | Send current items, customer, payment and timestamp to generateBill with Paid status. | `void` |
| onGenerateAndHoldClicked | None | Same call with OnHold status and unrecorded payment. | `void` |
| onOrderSelected | `const std::string& orderId` | Display the saved order/bill in the right column. | `void` |
| onMarkPaidClicked | None | Pass selected order ID/payment to markPaid. | `void` |
| onArchiveClicked | None | Archive selected order and refresh list. | `void` |
| onProductSelected | `const std::string& productId` | Display specifications, barcode image, filename and path. | `void` |
| onGeneratePreview | None | Generate the add-form barcode image. | `void` |
| onSaveProduct | None | Send completed Product to create. | `void` |
| onUpdateProduct | None | Send original Product and edited replacement to update. | `void` |
| onDeleteProduct | None | Pass selected product ID to remove. | `void` |
| showPage | `const std::string& pageName` | Switch sidebar page. | `void` |
| showCatalogueTab | `const std::string& tabName` | Switch View Catalogue/Add Product tab. | `void` |

Keep Description inline below quantity controls and Add anchored below it. Disable bill actions while saving. Read current cart items when a button is handled, and clear only after success; keep the returned bill visible. Stop scanning when leaving its page or closing the app. No animations or bill popup are needed.

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
| remove | `const std::string& productId` | Remove catalogue entry, retaining past bills. | `Status` |
| hasStock | `const Product& product`; `std::int64_t requested` | Check positive, unit-valid quantity no greater than available stock. | `bool` |
| deductStock | `std::vector<Product>& products`; `const std::vector<SaleLine>& lines` | Validate all lines, then deduct in supplied list copy. No file writes; leave copy unchanged on failure. | `Status` |

Zero-stock products remain available for restocking by quantity edit. Proposed safeguard for team confirmation: block deletion and measurement-unit changes while an on-hold sale references the product. Barcode and ID remain fixed after creation; ID immutability is the proposed relationship convention.

### BarcodeAssetService

**Data member:** `dataRoot_` has type `std::filesystem::path` and stores the application-data folder.

| Function | Parameters in order — exact C++ types | Description | Return type |
| --- | --- | --- | --- |
| BarcodeAssetService — constructor | `std::filesystem::path dataRoot` | Store the folder path. | No return type — constructor. |
| preview | `const std::string& barcode` | Reuse encoder/renderer; return image in memory, null on failure. | `QImage` |
| save | `const std::string& barcode` | Generate and save PNG in managed folder; return relative path in value. | `Result<std::string>` |

Use existing generateBarcodeBits, codedString2Image, mat2QImage and saveBarcodeImage. The current renderer returns cv::Mat, then mat2QImage converts it for Qt. The barcode encodes its six-digit lookup value, not the product specifications.

## 5. P3 — Khurram: storage and IDs

**Proposed files:** PosStorage.h/.cpp and IdGenerator.h/.cpp.

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

Follow the assignment's field order. Product, Order and Bill rows end in #. Agree on quoting text with spaces, the repeated product groups inside one bill row, and absent held-payment values before writing parsers. Keep Bill and OrderDetail item order consistent. Historical charges use saved acronyms/prices/quantities/totals; the listed file fields do not preserve full historical names or units after product deletion.

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
| addByBarcode | `const std::string& barcode`; `std::int64_t addition` | Look up/check stock; add quantity, merging existing row. Capture product/pricing on first addition. | `Status` |
| setQuantity | `const std::string& productId`; `std::int64_t quantity` | Set absolute quantity after stock check. Zero removes row, including deleted catalogue products. | `Status` |
| items | None | Return cart entries by value. | `std::vector<CartItem>` |
| totals | None | Calculate current totals. | `Result<Totals>` |
| clear | None | Empty cart after successful bill generation. | `void` |

### PricingCalculator

**Data members:** none. It changes neither cart nor files.

| Function | Parameters in order — exact C++ types | Description | Return type |
| --- | --- | --- | --- |
| calculate | `const std::vector<CartItem>& items` | Calculate subtotal/tax/discount/payable. Empty cart gives zero totals. | `Result<Totals>` |

Proposed arithmetic: multiply each unit rate by the actual quantity, round each line's base/tax/discount to paisa, then sum. Payable is subtotal plus tax minus discount. Apply the same rule in the live summary and bill. Reject invalid values/overflow. Display count totals separately from weight totals.

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
| start | `int cameraIndex` | Start shared camera source. | `Status` |
| poll | None | Read/decode frame; return preview and changed-code flag. No candidate/repeat keeps selection. | `Result<ScanResult>` |
| stop | None | Stop/release camera; safe to repeat. | `void` |

Keep scanning after success: the existing Increment 1 window currently stops and shows a result page. P1 supplies the only timer and verifies a changed code through P2 lookup. Multiple candidates give guidance; unsuccessful decode results have found=false and empty code. Camera failure returns a message and stops scanning.

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
| generateBill | `const std::vector<CartItem>& items`; `const Customer& customer`; `const Payment& payment`; `OrderStatus status`; `const Timestamp& timestamp` | Accept Paid/OnHold only. Validate items, stock and rates; calculate, obtain IDs, build and save order/bill; return saved record. | `Result<SaleRecord>` |

For **Paid**, payment is recorded and stock is deducted through P2 in the same data copy saved through P3. For **OnHold**, generate/save the complete bill but do not deduct or reserve stock. Return failure if a cart's stored rates no longer match the catalogue; the cashier must refresh/re-add the changed item. Proposed payment amount is the amount applied to the bill, equal to payable; cash-change handling is not included.

### HistoryService

| Data member | C++ type | Purpose |
| --- | --- | --- |
| store_ | `IPosStore&` | Shared storage. |
| products_ | `ProductService&` | Stock operations. |

| Function | Parameters in order — exact C++ types | Description | Return type |
| --- | --- | --- | --- |
| HistoryService — constructor | `IPosStore& store`; `ProductService& products` | Keep references. | No return type — constructor. |
| list | None | Retrieve non-archived orders/bills. | `Result<std::vector<SaleRecord>>` |
| get | `const std::string& orderId` | Retrieve selected saved order/bill. | `Result<SaleRecord>` |
| markPaid | `const std::string& orderId`; `const Payment& payment` | Recheck stock, deduct once, save payment/status. Preserve IDs, items and saved financial values. | `Result<SaleRecord>` |
| archive | `const std::string& orderId` | Archive/hide order, retaining records and stock. | `Status` |

Marking an already-paid order must not deduct again. A held bill is not repriced when paid. History products/pricing stay read-only; no resume, search, filters or restore feature is included.

## 8. Handoffs and integration

| Operation | Calls and data passed |
| --- | --- |
| Scan product | P1 calls P4 poll; decoded barcode goes to P2 findByBarcode; returned Product fills the right panel. |
| Add/change quantity | P1 calls P4 addByBarcode/setQuantity; items and totals return to the order panel. |
| Generate paid/held bill | P1 passes cart items, customer, payment, status and timestamp to P5. P5 uses P4 calculate, P3 IDs and P2 stock rules, then P3 save. Returned SaleRecord is displayed; P1 clears cart only on success. |
| Pay held bill | P1 passes order ID/payment to P5 markPaid. P5 updates stock and the existing saved records through P2/P3; no new bill IDs. |
| Manage catalogue | P1 calls P2 product/image functions. P2 uses P3 storage/IDs and returns updated data for display. |

P1 creates storage and calls load first, then creates helpers/services and finally pages. All members use that same storage instance. Keep public names, field names, parameter order and return types in this document consistent; review changes with affected members before implementing them.

**SE principles:** each class has a focused job; widgets do not contain file handling or pricing; both billing and cart reuse one calculator; barcode functions are reused. The storage interface lets P3 change file formatting without rewriting the other services.

Integrate product save/load and cart calculation early, then bill/history and camera. Each member demonstrates their public functions; another member checks the result. Test the complete paid/held/payment/archive and save/reopen flows together before the November 19 demo. Include **Error handling on input**. Detailed validation can be specified later; numeric/file conventions and held-product editing restrictions marked proposed above still require team agreement.
