# Increment 3 — team implementation proposal

**Draft for team review.** This contract builds on Increment 2 at `10de6a6c66d8926daf110a7f20066b145ff6872e`. It specifies what each teammate owns, the shared types and service calls, the text files, and the tests needed before integration.

References are the [project decisions](../Decisions.md#12-increment-3), the [Increment 2 contract and cashier revision](../Increment%202/Increment2proposal.md#cashier-flow-revision), and the [existing Eyeris Figma file](https://www.figma.com/design/eKMDVbIiBG4LTtJkC5r72U/Untitled?node-id=1-2). The Increment 3 UI designs are being prepared on their own Figma page.

## 1. Requirements and ownership

| Req. | Deliverable | Owner and dependency |
| --- | --- | --- |
| 1–2 | Manager creates categories before product barcode generation. `PCategory.txt` stores category ID, name and acronym. The product record stores its category ID and the barcode starts with it. | P2 Nosher owns catalogue and barcodes, P3 Khurram storage, P1 Zaryab Qt. |
| 3 | Cashier can reopen holds and draft a paid quantity edit in History. Manager can edit and issue refunds using that same History. Approval is local and the seven-day limit is checked at save. | P5 Rayan Dar owns billing and authorization enforcement, P1 PIN and UI, P4 Rayyan repricing, P2 stock. |
| 4–6 | Manager lists and edits products and inventory. Holds reserve stock, payment deducts it, and approved corrections apply the quantity difference. | P2 stock rules, P5 transaction coordination, P3 save, P1 Qt. |
| 7 | Either role opens a saved bill PDF. Paid checkout generates it and revisions replace it. Archive removes it and unarchive makes generation available again. | P5 PDF and lifecycle, P3 saved path, P1 buttons. |
| 8–9 | `Bill.txt` has the prescribed summary fields and `BillDetail.txt` holds the required line fields. | P3 schemas and joins, P5 checkout, P4 pricing. |
| 10 | Keep scan-to-cart, inline quantity, one-click hold and quick resume. | P1 Qt, P4 cart. P4's camera-recognition work remains separate. |
| 11 | Manager generates tabular incomplete, completed, cash, all-sales, Top Sold and stock reports. Add payment-method mix, most/least stock and refund summaries. | P3 report queries, P1 controls and tables, P2 inventory values, P5 settlements. |
| 12 | Launch with Eyeris and a Cashier/Manager role picker. Manager uses PIN access. Both roles have order history. | P1 Qt, PIN and composition, P5 paid-edit enforcement. |
| 13–15 | Manager configures one enabled season and a special discount in **Products & Discounts**. Checkout applies eligible discounts automatically and saves their amounts. | P4 discount service and calculator, P3 persistence, P1 Qt. |

| Person | Implementation ownership |
| --- | --- |
| **P1 — Zaryab** | `src/pos/PosTypes.h`, `src/app/main.cpp`, `src/app/ManagerPinService.h/.cpp`, `src/ui/PosWindow.h/.cpp`, `src/ui/ManagerCataloguePage.h/.cpp`, `src/ui/ReportsPage.h/.cpp`, and `CMakeLists.txt`. Own role navigation, UI wiring, Qt dependencies and integration. |
| **P2 — Nosher** | `src/pos/ProductService.h/.cpp` and `src/barcode/BarcodeAssetService.h/.cpp`. Own categories, catalogue, barcode assets and reusable stock rules. |
| **P3 — Khurram** | `src/storage/IPosStore.h`, `src/storage/PosStorage.h/.cpp`, `src/storage/IdGenerator.h/.cpp` and `src/pos/ReportService.h/.cpp`. Own records, file I/O, ID allocation, recovery and report aggregation. |
| **P4 — Rayyan** | `src/pos/CartService.h/.cpp`, `src/pos/PricingCalculator.h/.cpp` and `src/pos/DiscountService.h/.cpp`. Own cart, pricing and promotion policy. Scanner recognition continues from `increment_2` on Rayyan's separate branch. |
| **P5 — Rayan Dar** | `src/pos/CheckoutService.h/.cpp`, `src/pos/HistoryService.h/.cpp` and `src/pos/BillPdfService.h/.cpp`. Own payments, paid edits, History state, archives and PDF generation. |

A `.h/.cpp` entry means both exact paths with that basename in the listed directory. Each owner changes their files. P1 integrates the app. P3 owns all POS text-file writes, including the path recorded for a PDF. P2 owns barcode image bytes and P5 owns generated PDF bytes.

### Status of interpretations

- **Confirmed** decisions are in [Decisions.md](../Decisions.md#12-increment-3). The latest update replaces the separate Discounts page with **Products & Discounts**, gives managers History, and distinguishes **Modified** and **Complete Refund** from an ordinary paid bill.
- **Proposed defaults used by this draft** include returning only sellable or never-dispatched quantities to stock, settling differences using the original payment method, recording refunds on their issue dates, using the shop's local calendar with Monday–Sunday weeks, including both ends of a season or custom date range, rounding half a rupee upward, capping a combined percentage at 100%, and using a locally initialized manager PIN. Review these defaults before freezing the shared headers.
- **Instructor-dependent** items are quoting spaces and keeping a companion metadata file, whether the all-sales report must include archived paid orders, ranking by Count and Weight in separate tables, whether earlier billing files must be imported for grading, and whether seven days means 168 hours. The proposal records the team's current interpretation for each.

## 2. Shared types and invariants

P1 defines the following in `src/pos/PosTypes.h` using `<cstdint>`, `<optional>`, `<string>`, `<utility>` and `<vector>`. P3 owns the `IPosStore` interface. Business records use C++17 and the standard library. Qt classes stay in application, UI or asset headers. Public records use the field order shown. Use the existing global type names. Place `using namespace std;` after the includes in project headers and source files, and use standard-library names without a namespace prefix in the declarations below.

| Convention | Meaning |
| --- | --- |
| Money | `int64_t` paisa, so `10000` is PKR 100.00. Use exact integer arithmetic with overflow checks. Format money with two decimal places. The final bill payable is a multiple of 100 paisa. |
| Quantity | `int64_t` thousandths. Count is a multiple of 1000. Weight is kg with three decimal places. Stock, committed quantities and absolute restock quantities are nonnegative. |
| Rates | `int32_t` basis points. `1000` is 10.00%. Tax and seasonal rates are `0..10000` and special is `0..800`. Store original applied rates on paid sales. |
| Time | Shop-local date and time use `YYYY-MM-DD` and `HH:mm:ss`. Epoch seconds refer to the same instant. The app supplies one timestamp per action. The eligibility window uses epoch seconds and reports use saved local dates. |
| Identifiers | Decimal strings preserve leading zeros. Catalogued product IDs are stable numeric IDs. Barcodes are six digits with a `10`–`99` prefix. An order ID uses six sequence digits and a two-digit creation day. Bill IDs are unique nine-digit strings. |
| Defaults | Strings and vectors empty, optionals empty, integers zero, bools false, enums first value unless specified. Services use `Status` or `Result<T>` with a useful message on failure. Inspect a result value only on success. |

| Enum | Exact values in declaration order |
| --- | --- |
| `Unit` | `Count`, `Weight` |
| `OrderStatus` | `InProgress`, `OnHold`, `Paid` |
| `BillChange` | `Unchanged`, `Modified`, `CompleteRefund` |
| `PaymentMode` | `Cash`, `Card`, `Wallet` |
| `Role` | `Cashier`, `Manager`. The launch picker precedes either role. |
| `ReportKind` | `Incomplete`, `Completed`, `CashSales`, `AllSales`, `PaymentMix`, `TopSold`, `HighestStock`, `LowestStock` |
| `ReportPeriod` | `Day`, `Week`, `Month`, `Year`, `AllTime`, `Custom` |

Archiving is a separate `bool archived` on the sale. A restored paid bill remains paid, and a restored unpaid bill returns to held status. A complete refund is a historical paid transaction with zero payable and zero remaining quantities, and has a distinct bill change state. Only **Modified** and **Complete Refund** get badges. Existing `OrderStatus::Archived` call sites change to the archived flag as part of integration.

| Record | Fields in order with exact C++ types |
| --- | --- |
| `Status` | `bool success`, `string message` |
| `Result<T>` | `bool success`, `string message`, `T value` |
| `Timestamp` | `string date`, `string time`, `int64_t epochSeconds` |
| `Category` | `string id`, `string name`, `string acronym` |
| `Product` | `string id`, `string barcode`, `string categoryId`, `string acronym`, `string name`, `Unit unit`, `int64_t unitPrice`, `int32_t taxRate`, `int64_t discountPerUnit`, `int64_t onHand`, `string barcodeImagePath` |
| `StockPosition` | `int64_t onHand`, `int64_t reserved`, `int64_t available` |
| `ProductStock` | `Product product`, `StockPosition stock` |
| `StockShortage` | `string productId`, `string name`, `Unit unit`, `int64_t required`, `int64_t available` |
| `Season` | `string startDate`, `string endDate`, `int32_t rate`, `bool active` |
| `SpecialDiscount` | `int32_t rate`, `bool active` |
| `PricingTerms` | `int32_t seasonalRate`, `int32_t specialRate` |
| `CartItem` | `Product product`, `int64_t quantity` |
| `Customer` | `string name`, `string contact` |
| `Payment` | `bool recorded`, `int64_t amountReceived`, `int64_t changeReturned`, `PaymentMode mode`, `string merchant`. Retains first payment values. |
| `SaleIds` | `string orderId`, `string billId` |
| `SaleLine` | `string productId`, `string barcode`, `string acronym`, `string name`, `Unit unit`, `int64_t unitPrice`, `int32_t taxRate`, `int64_t discountPerUnit`, `int64_t quantity`, `int64_t lineProductDiscount`, `int64_t lineTax` |
| `Totals` | `int64_t subtotal`, `int64_t productDiscount`, `int64_t seasonalDiscount`, `int64_t specialDiscount`, `int64_t tax`, `int64_t roundingAdjustment`, `int64_t payable` |
| `PricingResult` | `vector<SaleLine> lines`, `vector<int64_t> lineSubtotals`, `PricingTerms terms`, `Totals totals`. Row subtotals use the same order as lines. |
| `QuantityChange` | `string productId`, `int64_t quantity`, `int64_t restockQuantity`. Expose restock only when reducing a paid quantity. |
| `LineAdjustment` | `string productId`, `int64_t previousQuantity`, `int64_t quantity`, `int64_t restockQuantity` |
| `BillAdjustment` | `uint32_t revision`, `Timestamp at`, `int64_t amountDelta`, `int64_t productDiscountDelta`, `int64_t seasonalDiscountDelta`, `int64_t specialDiscountDelta`, `vector<LineAdjustment> lines` |
| `SaleRecord` | `SaleIds ids`, `Timestamp createdAt`, `optional<Timestamp> paidAt`, `Customer customer`, `Payment payment`, `OrderStatus status`, `bool archived`, `BillChange billChange`, `uint32_t revision`, `vector<SaleLine> lines`, `PricingTerms terms`, `Totals totals`, `vector<BillAdjustment> adjustments`, `string pdfPath` |
| `HeldCheckout` | `SaleRecord sale`, `vector<CartItem> items` |
| `PaidEditDraft` | `string orderId`, `uint32_t revision`, `vector<QuantityChange> changes`, `Totals before`, `Totals after`, `int64_t amountDelta` |
| `StoreData` | `vector<Category> categories`, `vector<Product> products`, `vector<Season> seasons`, `SpecialDiscount specialDiscount`, `vector<string> issuedBarcodes`, `vector<SaleRecord> sales` |

`ManagerApproval` is a class in `PosTypes.h` with a public default constructor and private `optional<PaidEditDraft> draft_`. Forward-declare `ManagerPinService` and `CheckoutService` and make them friends. A default approval is invalid. Only the PIN service sets `draft_`, and checkout verifies the complete bound edit against the current saved revision and its re-quote. This keeps a UI role flag from authorizing a settlement. A successful save increments the revision, so replaying the same approval fails.

**Record rules.** `onHand` counts physical stock, including pieces reserved on active holds. `available = onHand − reserved`. Reservations are derived from unarchived `OnHold` lines keyed by product ID. Archived and paid orders reserve nothing. Cart drafts do not reserve extra stock until saved. For an active resumed hold the cart can spend its own reservation as well as globally available stock. A paid edit can change only quantities of saved lines. Keep a zero line in the stored pricing snapshot so its historic product and rates remain available. Quantity-zero lines contribute zero tax, value, sales units and reservation. A full refund is final in this draft. Preserve original payment time, IDs and first tender. Signed adjustments let History, PDF and reports reconstruct collections and refunds.

## 3. P2 — categories, products and stock

**Files:** `src/pos/ProductService.h/.cpp`, `src/barcode/BarcodeAssetService.h/.cpp`. Private `ProductService` members remain `IPosStore& store_`, `BarcodeAssetService& images_`, `const IdGenerator& ids_`. `BarcodeAssetService` keeps `filesystem::path dataRoot_`.

| Method | Parameters in order | Return and behavior |
| --- | --- | --- |
| `ProductService` constructor | `IPosStore& store`, `BarcodeAssetService& images`, `const IdGenerator& ids` | Keep service references. |
| `list() const`, `listCategories() const` | None | `Result<vector<Product>>` and `Result<vector<Category>>` respectively. Empty is successful. |
| `findById() const`, `findByBarcode() const` | `const string& productId` or `const string& barcode` | `Result<Product>`. Only the live barcode resolves for scanning. |
| `listWithStock() const` | None | `Result<vector<ProductStock>>`. Read the catalogue and compute stock figures from the same snapshot. |
| `findWithStock() const`, `findWithStockByBarcode() const` | `const string& productId` or `const string& barcode`, `const string& excludeHeldOrderId` | `Result<ProductStock>`. Resolve live identity and compute stock from the same snapshot, allowing that hold's own reservation when applicable. |
| `saveCategory` | `const optional<Category>& original`, `const Category& input` | `Result<Category>`. Empty original creates a new ID. Edit requires a matching saved original and keeps its ID. Reject duplicate IDs, duplicate names or acronyms ignoring case, empty values and invalid IDs. |
| `removeCategory` | `const string& categoryId` | `Status`. Requires an existing category with no issued barcode and no referring products. |
| `previewBarcode() const` | `const string& categoryId` | `Result<QImage>`. Choose a candidate using the same allocator as save, return an image. Preview does not issue the barcode. Display the final saved label returned by create or update. |
| `create` | `const Product& input` | `Result<Product>`. Validate category, money, tax rate and unit. Assign IDs and barcode, create PNG, save catalogue and issued code together. Return the saved product. |
| `update` | `const Product& original`, `const Product& replacement` | `Result<Product>`. Check the original snapshot against the current stored row. Edit fields, preserve product ID and allocate a new barcode only on category reassignment. Reject on-hand stock below reservations and a unit change for a product with an active hold. |
| `remove` | `const string& productId` | `Status`. Refuse while an active hold refers to the product. Preserve issued barcodes and all saved bill lines. |
| `stockPosition() const` | `const StoreData& data`, `const string& productId`, `const string& excludeHeldOrderId` | `Result<StockPosition>`. Compute physical, reserved and available values from one snapshot. Empty exclusion gives store-wide figures. A matching active hold is excluded so its own reservation can be used. |
| `shortages() const` | `const StoreData& data`, `const vector<SaleLine>& requested`, `const string& excludeHeldOrderId` | `Result<vector<StockShortage>>`. One entry per unavailable quantity. A missing product or changed unit produces a named shortage with zero available. Check the whole list before mutation. |
| `deductStock() const` | `StoreData& data`, `const vector<SaleLine>& lines`, `const string& excludeHeldOrderId` | `Status`. On a valid request, deduct physical stock in the supplied snapshot. A failed validation leaves it unchanged. P5 saves it in the same commit as payment. |
| `applyPaidStockDifference() const` | `StoreData& data`, `const vector<SaleLine>& before`, `const vector<QuantityChange>& changes` | `Status`. An increase needs sellable stock and the saved unit. A decrease may restock at most the decrease and requires a live matching-unit item for positive restock. Validate the entire operation before changing data. |

Category IDs use `10`–`99`. New barcode suffixes use the lowest never-issued `0001`–`9999` under the prefix. Keep prior codes in `issuedBarcodes`, including codes retired by deletion or relabeling. Legacy six-digit labels remain valid. Persist the code only after creating its image and successfully saving the product. On storage failure remove the unreferenced new PNG. A crash may leave an unused PNG. Ignore it during lookup and clean it only after verifying it is unreferenced. A failed relabel leaves the old code and image usable. Once saved, the new image is canonical and old saved bills retain their original barcode strings.

`BarcodeAssetService(filesystem::path dataRoot)` stores one data root. `QImage preview(const string& barcode) const` returns a null image on error. `Result<string> save(const string& barcode) const` writes `barcodes/<barcode>.png` and returns its relative path. `filesystem::path imagePath(const string& barcode) const` remains a private helper accessible to `ProductService`. Reuse the current team encoder, renderer and decode contract.

## 4. P4 — cart, promotion configuration and pricing

**Files:** `src/pos/CartService.h/.cpp`, `src/pos/PricingCalculator.h/.cpp` and `src/pos/DiscountService.h/.cpp`. Cart has `ProductService& products_`, `vector<CartItem> items_` and `optional<HeldCheckout> held_`. Pricing has no members. Discounts keeps `IPosStore& store_`. P5 obtains quotes from pricing for both the UI and transaction save.

| Method | Parameters in order | Return and behavior |
| --- | --- | --- |
| `CartService` constructor | `ProductService& products` | Keep catalogue reference. |
| `addByBarcode` | `const string& barcode`, `int64_t addition` | `Status`. Use `findWithStockByBarcode` to check live identity, unit and availability including own reservation. Add to an existing line or append a new line. Preserve the quantity draft and existing cart on failure. |
| `setQuantity` | `const string& productId`, `int64_t quantity` | `Status`. Replace the total quantity after checking `findWithStock`. Zero removes a line from an unpaid cart. |
| `items() const` | None | `vector<CartItem>`. Return snapshots in cart order. |
| `restoreHeldItems`, `clear` | `const HeldCheckout& held` for restore, none for clear | `void`. Restore from a validated P5 response. Clear items and held context on successful save or deliberate clear. |
| `PricingCalculator` constructor | None | Defaulted. |
| `calculate() const` | `const vector<SaleLine>& lines`, `const PricingTerms& terms` | `Result<PricingResult>`. Pure calculation. Return gross row subtotals, updated line discounts and taxes, effective terms and totals. Ignore prior computed amounts. |
| `DiscountService` constructor | `IPosStore& store` | Keep storage reference. |
| `listSeasons() const`, `getSpecial() const` | None | `Result<vector<Season>>` and `Result<SpecialDiscount>` respectively. |
| `saveSeason` | `const optional<Season>& original`, `const Season& replacement` | `Status`. Empty original adds. An exact saved original updates. Validate real dates, rate bounds and unique date pairs. Reject enabling a second season until the manager disables the enabled one. |
| `saveSpecial` | `const SpecialDiscount& special` | `Status`. Validate the 8% cap and persist the rate and enabled state. |
| `termsFor() const` | `const StoreData& data`, `const Timestamp& now` | `PricingTerms`. Use the enabled season on inclusive dates, and the special rate while enabled. Return zero for an inactive offer. |

Existing `CartService::totals` and `lineTotals` callers move to the quote interface. One shared quote returns line subtotal inputs, discounts and tax as well as the bill totals. Keep the scanner's `start(int cameraIndex) -> Status`, `poll() -> Result<ScanResult>` and `stop() -> void` handoff. Integrate the recognition improvement from `increment_2`. Cart quantity rules stay outside that adapter.

**Calculation order.** Multiply unit price and product discount by quantity in thousandths, round each money result half up to paisa, and subtract the line product discount. The sum of these lines is the base for seasonal and special rates. Cap the combined rate at 100% by trimming the special rate if necessary. Multiply the base by the combined rate once and round that discount to paisa. Split the rounded amount between seasonal and special in proportion to effective rates, assigning any remaining paisa to the larger remainder and giving seasonal priority on a tie. For example, on PKR 0.05, 10% seasonal plus 8% special gives a PKR 0.01 combined discount. Save the effective rates and the two amounts. Allocate the combined discount across lines proportionally to their base amounts, with ties in saved line order, so allocated paisa add to the bill discount. Apply each line's tax percentage to its discounted amount and sum rounded line taxes. The exact payable is gross subtotal less the three discounts plus tax. Round it to whole PKR, with `.50` upward as the draft default. The signed rounding difference is visible when nonzero. Zero base or zero combined rate yields zero flat discounts. Reject invalid rates, fractional Count, invalid amounts or overflow before returning a quote.

For a new order, keep Increment 2's captured base prices, tax rates and item discounts on rows added to the active cart, and check identity, unit and stock live. Resolve the current bill-level promotions at quote and payment. For a resumed unpaid order, refresh all base prices, taxes and discounts from the live catalogue and promotion settings on resume and on checkout recheck. For a paid correction, use its saved unit prices, tax rates, product discounts, and seasonal and special rates. The pricing code sees explicit inputs in all three cases.

## 5. P3 — storage, IDs and reports

### Storage contracts

**Files:** `src/storage/IPosStore.h`, `src/storage/PosStorage.h/.cpp`, `src/storage/IdGenerator.h/.cpp`. `IPosStore` includes `PosTypes.h` and declares `virtual Result<StoreData> read() const = 0`, `virtual Status save(const StoreData& data) = 0`, and a public defaulted virtual destructor. `PosStorage` publicly inherits it. Its members are `filesystem::path dataRoot_`, `StoreData data_` and `bool loaded_ = false`. Construct it with `filesystem::path dataRoot`. `Status load()` recovers an interrupted save, validates files, joins all rows and opens one complete snapshot. `Status importIncrement2Catalogue(const filesystem::path& sourceRoot)` validates a separate legacy catalogue and imports it into an initialized empty destination. `read() const` and `save()` override the interface.

One app instance holds a `QLockFile` in the data root for its lifetime. The serverless desktop app performs store read/modify/save on the GUI thread. Background or asynchronous PIN completion returns to that thread and obtains fresh state for the requested action. `read` returns a copy. `save` validates the whole candidate before writing anything, installs the file set together from the application's viewpoint, and updates `data_` only after success. No service writes one POS data file directly.

Keep the existing staged write/backup approach with explicit restart recovery. Under `data/.writing/`, prepare complete `before/` and `after/` sets, including explicit absence for first creation. Close and verify the staging writes, then write a `prepared` marker before changing live files. Install the new set, validate it, then write a `committed` marker. Remove the staging directory only after success or successful rollback. Recovery with `prepared` alone restores the verified old set. Recovery with `committed` restores the verified new set. An interrupted unprepared stage can be discarded only if the live set passes validation. If rollback or recovery cannot establish one valid set, leave business data alone, keep staging evidence and block writes with an error that names the data root. This contract covers app-level write failures and process interruption. Test real platform rename, permission and locking behavior before claiming resilience to power loss.

`IdGenerator` has a defaulted constructor and no members. `Result<string> productId(const StoreData& data) const` keeps the numeric allocator based on live and historical product IDs. `Result<SaleIds> saleIds(const StoreData& data, const Timestamp& timestamp) const` retains the six-digit order sequence plus creation day and nine-digit bill sequence, scans archived records too, and fails cleanly on exhaustion. P2 owns suffix allocation and P3 persists the issued-code set.

### Exact text records

Store below the same ignored project-local `data/` root as Increment 2. Use UTF-8, LF for new writes, tolerate CRLF on read, and use spaces as delimiters outside double quotes. Use `quoted` for strings, including IDs, dates and relative paths. Escape embedded quotes and backslashes, reject newlines, and use unquoted `-` only for absent optional scalar fields. Numeric tokens, enum tokens, boolean tokens `true`/`false` and terminators are unquoted. Money and percentage fields use two decimal digits. Store Count as whole items and Weight as three decimal kg digits. Write percentage `10.00` for 1000 basis points. Keep runtime PNGs under `barcodes/` and managed PDFs under `bills/` and use relative paths with forward slashes.

| File | Field order on disk |
| --- | --- |
| `PCategory.txt` | categoryId, name, acronym, `#` |
| `Product.txt` | productId, barcode, categoryId, acronym, name, unit, unitPrice, taxRatePercent, discountPerUnit, onHand, barcodeImagePath, `#` |
| `Order.txt` | orderId, creationDate, creationTime, creationEpochSeconds, customerName, customerContact, status, archived, paymentMode, merchant, firstAmountReceived, firstChangeReturned, `#` |
| `OrderDetail.txt` | orderId, productId, barcodeAtSale, quantity |
| `Bill.txt` | billId, customerName, orderId, totalWithoutTax, totalTax, totalProductDiscount, totalPayable, amountReceived, balanceReturned, billStatus, seasonalDiscount, specialDiscount, billDate, billTime |
| `BillDetail.txt` | billId, productAcronym, unitPriceWithoutTax, quantity, totalLineProductDiscount |
| `Seasons.txt` | startDate, endDate, percentage, isActive, `#` |
| `PosMeta.txt` | Tagged rows with the exact forms below. Keeps supporting data outside the instructor's prescribed bill and season fields. |

`Bill.txt` totalWithoutTax is the gross merchandise subtotal, with the three discounts saved separately. Keep `Order.txt` status as `on-hold` or `paid`, using the separate archive bit. In `Bill.txt` emit `archived` if archived, `refunded` for an unarchived complete refund, otherwise `on-hold` or `paid`. Bill date/time is first payment if paid and creation time if held. It is immutable once paid. `amountReceived` is first tender plus positive paid adjustments, while `balanceReturned` is change from initial tender. Keep refunds as negative adjustments in metadata. The displayed and saved paid net is received minus change minus refunds. Held summaries have zero tender and change and a `-` payment mode. Use `cash`, `card` or `wallet` for paid modes.

| `PosMeta.txt` tag | Fields after tag, followed by `#` |
| --- | --- |
| `format` | schemaVersion. This draft uses `3`. |
| `special` | percentage, isActive. Exactly one entry. |
| `issued` | barcode. One row per issued barcode, including retired labels. |
| `bill` | billId, originalPaymentEpochSeconds or `-`, appliedSeasonalPercent, appliedSpecialPercent, roundingAdjustment, changeState, revision, pdfPath or `-` |
| `line` | billId, oneBasedOrdinal, productId, historicalProductName, unit, taxRatePercent, productDiscountPerUnit, lineTax |
| `adjust` | billId, revision, editDate, editTime, editEpochSeconds, payableDifference, productDiscountDifference, seasonalDiscountDifference, specialDiscountDifference |
| `adjust-line` | billId, revision, productId, previousQuantity, updatedQuantity, restockedQuantity |

Change states serialize as `unchanged`, `modified` and `complete-refund`. Data rows for one bill use the same saved order in OrderDetail and BillDetail. Match them through the order/bill relationship and the explicit line ordinal and product ID in metadata. Use unique IDs, one bill per order, matching quantities and statuses, and one row per product per order. Saved lines need no current product row once an order has been paid or archived. Validate active held reservations against live product IDs and units, and verify known totals and revisions before opening a store. Calculate gross receipts, refunds and change from first tender and dated adjustment rows. Validate relative paths against the appropriate asset directory before using them. A missing PDF is recoverable from the saved bill.

**Worked example.** Ali originally buys 3 kg of rice and one water, pays PKR 700 cash, and takes PKR 86 change on a PKR 614 bill. The manager approves reducing rice to 2 kg and returning 1 kg to inventory. The revised subtotal is PKR 500.00. Product discounts are 25.00, then 10% seasonal and 5% special on 475.00 yield 47.50 and 23.75. Rice's discounted line is 323.00 at 10% tax, and water is untaxed. This gives tax 32.30 and exact total 436.05. Rounding gives 436.00 and the refund is 178.00. Examples below use the revised bill.

| File | Sample row or rows |
| --- | --- |
| `PCategory.txt` | `"12" "Grocery" "GR" #` |
| `Product.txt` | `"350001" "120001" "12" "RICE" "Basmati rice" weight 200.00 10.00 10.00 18.000 "barcodes/120001.png" #`<br>`"350002" "120002" "12" "WATER" "Water 1 L" count 100.00 0.00 5.00 8 "barcodes/120002.png" #` |
| `Order.txt` | `"00000125" "2026-11-25" "09:55:00" 1795582500 "Ali Khan" "" paid false cash "" 700.00 86.00 #` |
| `OrderDetail.txt` | `"00000125" "350001" "120001" 2.000`<br>`"00000125" "350002" "120002" 1` |
| `Bill.txt` | `"000000001" "Ali Khan" "00000125" 500.00 32.30 25.00 436.00 700.00 86.00 paid 47.50 23.75 "2026-11-25" "10:00:00"` |
| `BillDetail.txt` | `"000000001" "RICE" 200.00 2.000 20.00`<br>`"000000001" "WATER" 100.00 1 5.00` |
| `Seasons.txt` | `"2026-11-23" "2026-11-30" 10.00 true #` |
| `PosMeta.txt` | `format 3 #`<br>`special 5.00 true #`<br>`issued "120001" #`<br>`issued "120002" #`<br>`bill "000000001" 1795582800 10.00 5.00 -0.05 modified 1 "bills/000000001.pdf" #`<br>`line "000000001" 1 "350001" "Basmati rice" weight 10.00 10.00 32.30 #`<br>`line "000000001" 2 "350002" "Water 1 L" count 0.00 5.00 0.00 #`<br>`adjust "000000001" 1 "2026-11-26" "10:00:00" 1795669200 -178.00 -10.00 -19.00 -9.50 #`<br>`adjust-line "000000001" 1 "350001" 3.000 2.000 1.000 #` |

**Legacy input and startup.** Initialize an empty Increment 3 store for the demo and create new bills. If the existing data root is Increment 2, `load` stops before writing and identifies the legacy data path. Preserve it in a separate backup before initializing a fresh `data/` root. The optional app argument `--import-increment2-catalogue <source-root>` asks P3 to import from that separate backup into the empty store while P1 holds the store lock. The operation refuses a populated destination or the destination as source. Parse and validate the full input, verify its PNGs and seed issued codes from the legacy catalogue and order details, including archived orders and retired labels. On an invalid row show the source file and row and keep the new store empty for retry. Copy validated PNGs as import assets and remove new copies if the import save fails.

A legacy product row has no category ID and stores tax per unit. Retain product ID, label, unit, quantity, money and PNG, assign a placeholder category by its existing prefix, and convert the pre-discount tax amount to the nearest basis point. A zero-price, zero-tax item gets a zero rate. A positive tax on a zero price or an out-of-range equivalent rate requires correction before import. Imported stock is on hand, with zero active reservations because the new transaction set starts empty.

The two old bill forms start with bill ID, customer and order ID, then repeat the line fields. The original groups are acronym/price/discount/quantity. The `rates` form adds per-unit tax and uses an unquoted `rates` marker before the groups. Both end in old totals and status and can appear together in one valid Increment 2 file. The former lacks captured line tax. Neither gives trustworthy payment time for bills originally held. Preserve the source rows, IDs, totals, timestamps and images in their existing store or backup. Start the new transaction set empty and leave existing payment times as recorded. If grading requires legacy transactions in the new History, add a separately approved read-only import with explicit missing fields and no paid-edit or reservation eligibility until those records are resolved. Malformed rows or a mixed Increment 2/Increment 3 file set raise a precise file and row error.

### Reports

**Files:** `src/pos/ReportService.h/.cpp`. Constructor `ReportService(const IPosStore& store, const ProductService& products)` keeps `const IPosStore& store_` and `const ProductService& products_`. `Result<ReportResult> generate(const ReportRequest& request) const` reads one snapshot. It returns typed rows and totals. P1 chooses columns and delegates filtering and aggregation. P1 declares these records in `PosTypes.h` with the other shared types.

| Record | Fields in order |
| --- | --- |
| `ReportRequest` | `ReportKind kind`, `ReportPeriod period`, `string anchorDate`, `string startDate`, `string endDate`, `vector<PaymentMode> modes`, `optional<Unit> unit` |
| `ReservationReportRow` | `string productId`, `string name`, `Unit unit`, `int64_t quantity`, `StockPosition stock` |
| `OrderReportRow` | `SaleIds ids`, `string customerName`, `Timestamp at`, `OrderStatus status`, `BillChange billChange`, `optional<PaymentMode> mode`, `Totals totals`, `int64_t collected`, `int64_t refunded`, `vector<ReservationReportRow> reservations` |
| `ProductReportRow` | `string productId`, `string name`, `Unit unit`, `int64_t quantitySold`, `optional<Unit> stockUnit`, `optional<StockPosition> stock` |
| `PaymentMixRow` | `PaymentMode mode`, `uint64_t count`, `int32_t shareBasisPoints` |
| `ReportResult` | `string startDate`, `string endDate`, `vector<OrderReportRow> orders`, `vector<ProductReportRow> products`, `vector<PaymentMixRow> paymentMix`, `int64_t billTotal`, `int64_t collected`, `int64_t refunded`, `int64_t netCollections`, `int64_t totalDiscount`, `uint64_t refundCount`, `uint64_t outOfStockCount` |

For reservations in `OrderReportRow`, use product identity, unit and reserved quantity from the hold and stock figures from the current catalogue and reservations. Incomplete is a current snapshot of unarchived holds and their saved unpaid bill amounts. Show each hold's reserved lines. Cash Sales fixes the method to Cash. All Sales and Completed allow empty mode filters for all modes. Payment Mix counts first paid checkouts, including a completed sale that later has a refund, across all methods in the selected period. Return zero shares when the period has no payments. For a nonempty period, use exact integer percentages to two decimals and distribute the remainder deterministically so percentages sum to 100.00%.

Match first payment to its local date. Include later charges and refunds on the edit date, using the original payment mode. Aggregate collected, refunded and net plus signed discount changes and show refund count per negative adjustment. To reconstruct original discount or paid quantities, reverse the saved adjustment deltas from the current bill. Completed rows show original payment date, latest saved totals and period movement. An adjustment-only sale can appear in cash and all-sales reports on its edit date. An archived bill is excluded from every report and total. A held bill contributes no payment, unit sales or cash total. Use inclusive dates and validate a custom range before generating. With an all-time period, date bounds are empty. For Top Sold group by product ID and saved sale unit, sum settled units by payment or edit date, subtract corrections, and rank positive totals. Show current stock in its own live unit wherever a live record exists. Highest/Lowest report current sellable stock, including zero, and show physical and reserved quantities in the current unit. Rank Count and Weight separately, limit each list to ten and break ties by product ID. If no unit is selected, return both groups in Count-then-Weight order. Empty reports succeed with empty rows and zero totals.

## 6. P5 — checkout, paid changes, history and PDF

**Files:** `src/pos/CheckoutService.h/.cpp`, `src/pos/HistoryService.h/.cpp`, `src/pos/BillPdfService.h/.cpp`.

| Class | Constructor parameters in order and private members |
| --- | --- |
| `CheckoutService` | `IPosStore& store`, `ProductService& products`, `const PricingCalculator& pricing`, `const DiscountService& discounts`, `const IdGenerator& ids`, `HistoryService& history`. Corresponding references `store_`, `products_`, `pricing_`, `discounts_`, `ids_`, `history_`. |
| `HistoryService` | `IPosStore& store`, `const ProductService& products`, `const BillPdfService& pdf`. Corresponding references `store_`, `products_`, `pdf_`. |
| `BillPdfService` | `filesystem::path dataRoot`. Stores `filesystem::path dataRoot_`. |

| `CheckoutService` method | Parameters in order | Return and behavior |
| --- | --- | --- |
| `quote() const` | `const vector<CartItem>& items`, `const string& heldOrderId`, `const Timestamp& now` | `Result<PricingResult>`. Empty ID means a new cart. Validate live identity and unit, refresh live rates for resumed holds, and apply current promotions. Use one snapshot per quote. |
| `generateBill` | `const vector<CartItem>& items`, `const Customer& customer`, `const Payment& payment`, `OrderStatus status`, `const Timestamp& now`, `const optional<int64_t>& expectedPayable` | `Result<SaleRecord>`. Accept only `Paid` or `OnHold`. Recheck stock and price, assign IDs, save once, and generate a PDF after paid save. Paid saves require a displayed payable. |
| `resumeHeldOrder() const` | `const string& orderId` | `Result<HeldCheckout>`. Load an active hold, resolve each live product by ID and retain its quantity. Opening the draft leaves the reservation intact. |
| `saveHeldOrder` | `const string& orderId`, `const vector<CartItem>& items`, `const Customer& customer`, `const Payment& payment`, `OrderStatus status`, `const Timestamp& now`, `const optional<int64_t>& expectedPayable` | `Result<SaleRecord>`. Same validation as new checkout. Allow its own reservation, update that same order/bill and preserve creation time. Set payment time on first successful pay only. |
| `previewPaidEdit() const` | `const string& orderId`, `const vector<QuantityChange>& changes`, `const Timestamp& now` | `Result<PaidEditDraft>`. Check paid-edit eligibility, compute revised totals using captured rates, and return the before/after values and signed amount. Require an actual quantity change. |
| `applyPaidEdit` | `const PaidEditDraft& draft`, `const ManagerApproval& approval`, `int64_t settledDelta`, `const Timestamp& now` | `Result<SaleRecord>`. Re-read and re-quote the current saved record, verify the bound approval, eligibility and exact settlement. Apply stock changes, increment revision, append adjustment and commit once. Rebuild PDF after save. |

Held saves require current stock sufficient for the quantities and save zero payment. A paid save requires current stock, a first tender and unchanged displayed payable. Cash may exceed payable and change is derived from the difference. Card and Wallet record exactly payable and zero change. Keep the existing optional customer contact validation. For a resumed hold allow stock already reserved by that order and preserve IDs. Paying deducts stock and removes that reservation in the same save. On insufficient stock, changed total or failed save, keep the active draft and any saved hold. Refresh the quote for a changed total and require a new confirmation.

Paid edits check `0 <= now − paidAt <= 604800` seconds at preview and commit. Sample commit time after the asynchronous PIN check and immediately before the service call. Require paid, unarchived, valid pricing inputs and no prior complete refund. Accept unique edits for existing line product IDs. Validate Count/Weight quantities and all positive stock changes against live sellable stock. For a reduced quantity the manager records how much can return to stock. It can be zero, including for damaged returns, and never exceeds the reduction. Show increases as money due, decreases as refund due, and zero as no cash movement. Settle using the original payment method in the app's local accounting. There is no call to a payment processor. Save one signed adjustment at the new revision. On full refund set every saved line quantity to zero and show **Complete Refund**. Otherwise a revised bill shows **Modified**. Keep original tender, payment instant, ID and initial edit-window anchor. Compare exact draft changes, revision, before/after totals and settlement amount to the approved preview. A stale approval or duplicate click cannot charge or deduct twice. A save failure keeps the draft and original stored bill and inventory. If the operator already moved money, show the failed save and require reconciliation before retrying settlement.

| `HistoryService` method | Parameters in order | Return and behavior |
| --- | --- | --- |
| `list() const`, `listArchived() const` | None | `Result<vector<SaleRecord>>`. Filter by the archived flag. |
| `get() const` | `const string& orderId` | `Result<SaleRecord>`. Return the saved record and historical values. |
| `archive` | `const string& orderId` | `Status`. Move an order into Archives, releasing unpaid reservations. For a paid bill stage retirement of the canonical managed PDF, including an unreferenced stale copy, and clear the saved path. If saving fails, restore the staged copy. |
| `unarchiveShortages() const` | `const string& orderId` | `Result<vector<StockShortage>>`. For an archived hold compare every line against sellable stock. For an archived paid bill return an empty list. |
| `unarchive` | `const string& orderId` | `Status`. Recheck all shortages on a fresh snapshot. Finish any staged PDF deletion before restoring the order, even when the saved PDF path is empty. Restore held or paid state and leave the PDF reference empty. A shortage or unfinished file removal leaves the order archived. |
| `generatePdf` | `const string& orderId` | `Result<SaleRecord>`. Generate a PDF for an unarchived paid or fully refunded bill when the current PDF is missing. Reconcile any staged archive copy before writing. Persist its relative path only if writing succeeded. Return the same record if the current PDF already exists. |
| `openPdfPath() const` | `const string& orderId` | `Result<filesystem::path>`. Return a validated absolute local path for a current saved PDF and fail for archived, held or missing files. |
| `recoverPdfRemovals() const` | None | `Status`. Read the loaded snapshot and pass it to the PDF service to reconcile interrupted archive operations. Run once at startup before showing History. |

Remove `HistoryService::markPaid` and the corresponding History button call site. Paid checkout and paid edits clear any stale PDF association within the financial save, then call `generatePdf`. If PDF generation or recording its path fails, the payment or approved edit still succeeds with a warning in `Result<SaleRecord>::message`. Retrying generates the current revision. A stale file must stay hidden while `pdfPath` is empty. The UI offers **Open Bill PDF** only when the saved asset is usable. An unarchived paid bill can generate manually. An archived hold with a shortage remains archived. P1 uses `unarchiveShortages` to show the missing items in red, and the mutation checks the same rule again.

| `BillPdfService` method | Parameters in order | Return and behavior |
| --- | --- | --- |
| `write() const` | `const SaleRecord& sale` | `Result<string>`. Render the saved bill to `bills/<billId>.pdf` with `QPdfWriter` using `QSaveFile` for replacement. Return the relative file path when complete. |
| `resolve() const` | `const string& relativePath` | `Result<filesystem::path>`. Resolve a regular bill PDF within the data root to an absolute path. Reject missing files and paths outside the managed folder. |
| `stageRemoval() const` | `const string& billId` | `Status`. Move `bills/<billId>.pdf` to `bills/<billId>.pdf.archiving`. A missing asset succeeds. A file-lock error aborts archive before POS mutation. |
| `finishRemoval() const` | `const string& billId`, `bool archived` | `Status`. Remove the staged copy if archive saved, otherwise restore it. Report a cleanup failure and keep the staged file for startup recovery. |
| `reconcileRemovals() const` | `const StoreData& data` | `Status`. Resolve staged files for known bill IDs using each saved archive flag. Preserve unknown staged entries and report them. |

Print the saved bill ID, order ID, first bill date/time, customer, status and applicable adjustment badge. Print product names and acronyms, units, quantities, prices before tax, line product discounts and tax. Show subtotal, sums of product, seasonal and special discounts, bill tax, rounding when nonzero, total payable, total amount received, change and any adjustment collections or refunds. Zero rows may be identified by quantity zero. Preserve saved facts even after relabeling or deleting catalogue entries. The printed document omits its filename and path. Escape customer and item text if using rich-text rendering. Repeated generation and multi-page item lists need readable layout on both supported systems.

## 7. P1 — Qt, roles and application wiring

**Files:** `src/app/main.cpp`, `src/app/ManagerPinService.h/.cpp`, `src/ui/PosWindow.h/.cpp`, `src/ui/ManagerCataloguePage.h/.cpp`, `src/ui/ReportsPage.h/.cpp` and `CMakeLists.txt`. Build one Qt application with the existing OpenCV and Qt dependencies, plus QtKeychain using the operating system's credential backend. Reuse the existing Eyeris backgrounds and design system across the role-specific pages. Source assets and data root are shared by the existing POS and barcode components.

### PIN service and window composition

`ManagerPinService` keeps `QObject* parent_ = nullptr` and uses asynchronous QtKeychain jobs parented to the supplied owner. Constructor `explicit ManagerPinService(QObject* parent = nullptr)`. No PIN is logged or persisted in application text files. A missing credential allows first-PIN setup. An inaccessible credential store yields an error and leaves Manager and paid edit authorization locked. Confirm the first PIN and replacements in the UI. Validate exactly four decimal digits in the service, including any leading zeros. Persist the replacement only after checking the current PIN. Keep the previous credential on a failed write. Disable action buttons while a PIN job is outstanding.

| `ManagerPinService` method | Parameters in order | Return and behavior |
| --- | --- | --- |
| `hasPin` | `function<void(Result<bool>)> done` | `void`. Distinguish missing credential from read failure. |
| `initializePin` | `const string& pin`, `function<void(Status)> done` | `void`. Create the first credential only when none exists. |
| `verifyPin` | `const string& pin`, `function<void(Status)> done` | `void`. On success the window may enter Manager. |
| `changePin` | `const string& currentPin`, `const string& newPin`, `function<void(Status)> done` | `void`. Compare current PIN, validate and save replacement. |
| `approvePaidEdit` | `const string& pin`, `const PaidEditDraft& draft`, `function<void(Result<ManagerApproval>)> done` | `void`. After successful verification return approval bound to that exact draft. |

Declare `PosWindow(ProductService& products, DiscountService& discounts, CartService& cart, PosScanner& scanner, CheckoutService& checkout, HistoryService& history, const ReportService& reports, ManagerPinService& access, QWidget* parent = nullptr)`. Service references are `products_`, `discounts_`, `cart_`, `scanner_`, `checkout_`, `history_`, `reports_` and `access_`, with the constructor's types. Keep `optional<Role> role_`, `QStackedWidget* pages_ = nullptr`, `QWidget* launchPage_ = nullptr`, `CurrentOrderView* currentOrderPage_ = nullptr`, `OrderHistoryView* historyPage_ = nullptr`, `ManagerCataloguePage* cataloguePage_ = nullptr`, `ReportsPage* reportsPage_ = nullptr`, `QWidget* settingsPage_ = nullptr`, `QTimer cameraTimer_`, `string selectedOrderId_`, `string resumedOrderId_`, `optional<PricingResult> currentQuote_`, `optional<PaidEditDraft> paidEditDraft_`, `optional<ManagerApproval> paidEditApproval_`, `optional<Product> pendingProduct_`, and `bool busy_ = false`. Inner view widgets are Qt-owned children. Keep the existing camera timer lifecycle, quantity field state, scanned-product display and save guards.

| `PosWindow` action | Parameters in order | Behavior, all return `void` |
| --- | --- | --- |
| `onRoleSelected` | `Role role` | Cashier opens Current Order. Manager checks or initializes PIN, then opens Products & Discounts. Keep the launch page visible on failure. |
| `onChangePin` | `const string& currentPin`, `const string& newPin`, `const string& confirmation` | Check confirmation, invoke P1 PIN service, report result in Manager Settings. |
| `onCameraTick`, `onHeldOrdersClicked` | None | Poll scanner and render preview, or show active holds. Scan defaults to 1 item or 1.000 kg. Each accepted presentation adds once. |
| `onCartQuantityChanged` | `const string& productId`, `int64_t quantity` | Commit Enter replacement, preserve steppers, refresh quote. Keep attention on the invalid field when needed. |
| `onGenerateBillClicked`, `onGenerateAndHoldClicked` | None | Use current inputs and quote. Clear the cart only after successful domain save. Hold returns to clean Current Order in one click. Paid shows saved bill and any PDF warning. |
| `onOrderSelected`, `onResumeOrder` | `const string& orderId` | Show saved details or reopen the active hold. Finish or hold an existing cart before replacing it. Reopening restores customer, quantities and current prices. |
| `onPreviewPaidEdit` | `const string& orderId`, `const vector<QuantityChange>& changes` | Keep the form in History and present old/new totals and money direction. Obtain a fresh preview after every change. |
| `onApprovePaidEdit` | `const string& pin` | Verify the manager's PIN against the exact displayed draft. When the job returns, keep the approval only if that draft is still selected and unchanged. Enable settlement confirmation. |
| `onCommitPaidEdit` | `int64_t settledDelta` | After manager approval and settlement confirmation, sample a fresh timestamp and call checkout with the bound draft and approval. On success clear the approval and repaint badges, details and PDF state. |
| `onArchiveOrder`, `onUnarchiveOrder`, `onGeneratePdf`, `onOpenPdf` | `const string& orderId` | Call History. On an unarchive shortage, name each affected item in red with required and available quantities. Open validated PDFs through `QDesktopServices`. |
| `onGenerateReport` | `const ReportRequest& request` | Pass report filters to P3, then present the returned rows and summary. |

The manager can initiate a paid edit directly from History. The cashier can draft one in their History and a manager approves and commits it locally using the PIN on that screen. Both roles can view and print the eligible saved bill and access their History archives. Keep archive/unarchive available in History for both roles as a draft default. A successful paid correction never returns to the scan/cart screen. The approval summary must show quantities, restock amounts, current and revised totals and the exact settlement amount. Once approved, the cashier or manager handles the approved money difference and the manager confirms settlement. Cancel, navigations and any change clear the bound approval. Check the cutoff again at commit even if the verification job started earlier.

Keep the existing nested `CurrentOrderView` and `OrderHistoryView` with Qt-owned widgets. Adapt the following presentation methods. Other existing camera preview, inline quantity, draft and History selection methods retain their signatures. Remove History's payment input, since resumed payment belongs to Current Order.

| View method | Exact parameters and return | Use |
| --- | --- | --- |
| `CurrentOrderView::readPayment() const` | None, `Result<Payment>` | Read the original tender, mode and merchant. Checkout derives and validates change from the approved payable. |
| `CurrentOrderView::showScannedProduct` | `const ProductStock& item`, `void` | Render the live product and quantity available to the current cart. |
| `CurrentOrderView::showCart` | `const vector<CartItem>& items`, `const vector<optional<int64_t>>& amounts`, `const optional<Totals>& totals`, `void` | Display row subtotals and totals supplied by the quote. When a quote fails, show an error and keep the draft intact. |
| `OrderHistoryView::showBill` | `const SaleRecord& order`, `void` | Display current saved value, historical rates, any modification badge and adjustments. |
| `OrderHistoryView::showPaidEdit` | `const PaidEditDraft& draft`, `void` | Show quantity changes and the settlement preview in the selected bill. |
| `OrderHistoryView::setPdfActions` | `bool canGenerate`, `bool canOpen`, `void` | Enable generation only for a ready-to-generate paid bill. Disable generation once a usable PDF is saved. |
| `OrderHistoryView::showUnarchiveShortages` | `const vector<StockShortage>& shortages`, `void` | Show required and available quantities with red shortage feedback. |

**Products & Discounts.** `ManagerCataloguePage(ProductService& products, DiscountService& discounts, QWidget* parent = nullptr)` keeps `ProductService& products_`, `DiscountService& discounts_`, `optional<Product> selectedProduct_` and `optional<Season> selectedSeason_`. Public `void refresh()` uses `listWithStock` and the category and promotion queries to reload. Private handlers `void saveProduct(const Product& input)`, `void previewBarcode(const string& categoryId)`, `void saveCategory(const optional<Category>& original, const Category& input)`, `void saveSeason(const optional<Season>& original, const Season& replacement)` and `void saveSpecial(const SpecialDiscount& special)` call the named services and show inline validation. The Products section starts with **Flat Discounts**, with seasonal add/edit and enable controls, and a special discount field and enable control. Product add/edit includes category, price, tax rate, item discount, unit and on-hand quantity. Preview requires a valid saved category. The manager can inspect and update stock with on-hand, reserved and available figures. Use the existing team barcode generator and display the canonical image after saving.

**Reports.** `ReportsPage(QWidget* parent = nullptr)` keeps `ReportRequest request_` and `optional<ReportResult> displayed_`. Provide `Result<ReportRequest> readRequest() const`, `void showResult(const ReportResult& report)` and `void showError(const string& message)`. `PosWindow` connects Generate Report to `onGenerateReport` and calls `showResult` on success. Show report-specific periods and payment controls before generation. Sales accept day, week, month, year, all time and custom dates. Sold rankings accept day, month, year and all time. Show payment mix separately with counts and percentages independent of the sales payment filter. Show reservations, highest and lowest stock, out-of-stock counts, refunds and net collections with their applicable report. Avoid letting an empty or invalid date control submit an ambiguous period.

## 8. Integration and shared baseline

The application holds the data-root lock, loads or imports the store, then constructs barcode assets and IDs, catalogue, cart, pricing and discount services, PDF and History, checkout and reports, and finally PIN and window. Run `recoverPdfRemovals` after constructing History and before opening the window. Report a recovery problem without offering affected PDF or archive operations until it is resolved.

| Header | Direct includes and collaborator declarations |
| --- | --- |
| `PosTypes.h`, `IPosStore.h` | Standard headers in Section 2, then `IPosStore.h` includes `PosTypes.h`. |
| `ProductService.h`, `BarcodeAssetService.h` | `PosTypes.h` and `<QImage>`. Forward-declare `IPosStore`, `IdGenerator` and `BarcodeAssetService` where referenced. The asset header also includes `<filesystem>`. |
| `PosStorage.h`, `IdGenerator.h` | Storage includes `IPosStore.h` and `<filesystem>`. IDs include `PosTypes.h`. |
| `CartService.h`, `PricingCalculator.h`, `DiscountService.h`, `ReportService.h` | `PosTypes.h`. Forward-declare `ProductService` and `IPosStore` where referenced. |
| `CheckoutService.h`, `HistoryService.h`, `BillPdfService.h` | `PosTypes.h`. History and PDF also include `<filesystem>`. Forward-declare referenced services and `IPosStore`. |
| `ManagerPinService.h` | `PosTypes.h` and `<functional>`. Forward-declare `QObject`. QtKeychain implementation headers belong in the source file. |
| Qt page and window headers | `PosTypes.h` and the Qt base-widget headers. Window additionally includes `<QTimer>`. Forward-declare service and page collaborators where held by reference or pointer. |

Each source file includes its own header and the complete headers for services it calls. Add `src/app` to the target include directories if the window includes the PIN service by basename.

1. P1 and P3 review this proposal and create the shared declarations in `src/pos/PosTypes.h` and `src/storage/IPosStore.h`. Check same names, field order, defaults and C++17 includes, and compile headers individually and together. If changing public data breaks old callers, land minimal transitional build fixes directly on the shared integration baseline under P1 ownership, or use a clearly labeled header-first checkpoint before the normal application build gate. Publish one agreed baseline on `increment_3` and record its SHA so everyone branches from the same point. Each owner creates their service headers and implementations on their own branch after this handoff.
2. P3 makes empty store, sample fixtures, save/load and recovery usable first. P2 integrates category-to-product creation, generated images and inventory checks. P4 integrates quantities and pricing, and verifies saved discount terms have one authority.
3. P5 integrates quote, hold, payment, settlement, History and PDF against that store. P1 wires routes to the service contracts and verifies role and error states on both operating systems. Integrate manager reports against the same saved demo records.
4. Review Rayyan's recognition changes separately on `increment_2`, then merge updated `increment_2` into `increment_3`. Re-verify barcode generation and cashier scan handoff after the merge. Recognition speed and physical test evidence remain Rayyan's responsibility until the team verifies it.
5. Before the November 26 demo, run the matrix below with fresh Increment 3 data on the demo machine, then rehearse a physical scan, hold, pay with discounts, edit/refund, PDF open, archive/unarchive and report generation. At release merge `increment_3` into `main`. Preserve `c32482d3746162efd1aa1946f49db7ae6a6f500f` as the original Increment 2 grading reference.

### Header handoff checklist

- [ ] P1 and P3 agree the shared types above, including report and approval records, and keep the two baseline headers free of Qt and OpenCV.
- [ ] Owners check exact declarations and cross-owner parameters in Sections 3–7 and agree any changes before calls are written. Only service headers required by a caller need to be available for that caller's compile verification.
- [ ] P1 verifies the MSVC and macOS builds, QtKeychain linkage, data root and login setup. No secrets or demo sales data enter Git.
- [ ] P3 supplies a single consistent set of text fixtures and worked figures. P1 provides a verified Figma handoff link for the Increment 3 page after the design thread completes.
- [ ] P1 checks demo readiness and records whether instructor-dependent points were clarified. Keep the team's chosen behavior explicit where the instructor leaves it open.

## 9. Verification matrix

| Test | Expected evidence | Lead |
| --- | --- | --- |
| Launch, PIN and role routes | Cashier and Manager open their correct pages. Wrong PIN keeps Manager and paid edit locked. Current PIN is required to change it. Restart retains the new PIN. Test both macOS and Windows. | P1 |
| Category and catalogue | Require a category before preview/save. Prefix matches. Save, reopen, regenerate and scan a label. Duplicate and retired barcode never resolves to a new product. Reclassification preserves historic bill identity. | P2/P3 |
| Inventory and reservations | Reserve the last item on a hold. A competing cart can't sell it. Resume can use its reservation. Re-hold replaces quantities. Pay deducts once. Archive releases. Unarchive with shortages names them in red and leaves the order archived. | P2/P5/P1 |
| Cashier speed and scan errors | Count and Weight defaults, Enter replaces, low-stock draft survives another scan, held selection remains fast, no repeated adds for stationary barcode. Physical camera test records device and result. | P1/P4 |
| Promotion rules and paid math | Date bounds, only one enabled season, special cap, additive percentages, mixed tax rates, fractional kg, exactly half rupee, zero subtotal and overflow. Reproduce the worked bill and persisted totals. | P4/P3 |
| Resume at new price | Change catalogue price and tax and promotions while held. Resume shows new prices, and History uses Resume Order. A changed amount before pay is shown before payment. | P4/P5/P1 |
| Paid edit and complete refund | Try edit just before, exactly at and after 168 hours. Begin PIN verification before cutoff and finish after it. Decline or change an approved draft. Verify same bill ID and original rates, exact settlement and stock delta, no double save on repeated clicks, and a complete refund with all quantities zero. | P5/P2/P1 |
| PDF lifecycle and failure | Pay generates a correct PDF. Edit replaces it. Archive removes managed copy. Unarchive enables manual generation. Interrupt PDF retirement and confirm recovery before unarchive. Render failure leaves the bill paid and allows retry. Print all required fields with no filename or path, including multipage bills. | P5/P1 |
| Files and recovery | Independently parse the eight file examples, cross-check joins, then round-trip. Test apostrophes/spaces/quotes, malformed input, missing assets, two app instances, failed writes and interrupted stage recovery. Check both legacy bill variants are preserved outside the fresh demo dataset. | P3 |
| Reports | Test complementary payment filters and method share, date endpoints, order totals, archives excluded, held reservations visible, dated refunds, ties and separate Count/Weight ranking. Try empty report and invalid custom range. | P3/P1 |
| Full integration smoke test | Build from the shared baseline and merged branch, run the end-to-end rehearsal on the demo machine, and record any platform or camera test still pending. | P1 and one independent teammate |
