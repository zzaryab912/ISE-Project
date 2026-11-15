# Increment 2 Interface Design

Status: working design, updated 15 November 2026.

This document records the page layouts and interactions discussed for Increment 2. It is a behavioural interface design, rather than a finished visual mock-up or an implementation specification. The [Increment 2 Requirements](Increment%202%20Requirements.md) remain the source for the assignment requirements.

### Design references

- The visual direction is documented in `documentations/Agents/Design System.md`, referred to in discussion as the design document. It records the approved background, sidebar, typography and panel styling. This local file is Git-ignored and is not included in the shared repository.
- The [Increment 2 Figma design page](https://www.figma.com/design/eKMDVbIiBG4LTtJkC5r72U/Untitled?node-id=1-2) is the shared visual reference for the interface screens. The [approved Current Order frame](https://www.figma.com/design/eKMDVbIiBG4LTtJkC5r72U/Untitled?node-id=56-29) establishes their visual direction.
- Figma screens are interface designs, not implemented application functionality.

**Agreed** identifies a direction established in the discussion. **Proposed** identifies a recommendation that still needs agreement. **Open** identifies a decision that has not been settled. An assignment requirement is labelled separately where its interface has not yet been designed.

**Later decisions:** The [Proposed System Design](Proposed%20System%20Design.md) supersedes conflicting earlier proposals below for Current Order, Order History and Product Catalogue. In particular, history has no search, filters, separate bill view or resume action; Archive replaces the earlier deletion discussion. The current-order summary and in-panel saved-bill inspection are agreed directions.

## 1. Shared Application Structure

### Agreed direction

- The POS is GUI based and extends the existing Increment 1 application.
- A navigation sidebar stays on the far left.
- The sidebar provides access to Current Order, Past Orders, and Product Catalogue. Past Orders was also referred to as Order History in the discussion; the final navigation label remains open.
- Current Order and Past Orders use a centre column for the working list and a right column for contextual information.
- Product descriptions appear within the page. They do not open in separate popup windows.

### Open decisions

- Minimum supported window size and smaller-screen/Windows compatibility. Visual styling and the current design proportions follow the design references above.
- How the existing barcode generator and image reader are accessed from the expanded POS.
- Whether an unfinished order is retained when navigating to another page. Retaining it is proposed, but the persistence behaviour has not been agreed.
- What happens when a cashier starts a new order while another unfinished order exists.

## 2. Current Order

### Purpose

The cashier builds an order by scanning product barcodes through the camera, entering quantities, and adding the products to the order. The assignment requires bill generation to be available from this screen.

### Agreed layout

| Area | Contents |
| --- | --- |
| Left sidebar | Application navigation |
| Centre column | Products added to the current order, their quantities, and pricing information |
| Right column | Camera preview, scanned product information below it, available quantity, quantity input, and Add to Order |

The right column follows the cashier's working sequence: scan a product, inspect its information and stock, enter the required quantity, and add it to the order.

### Agreed scanning behaviour

1. A successful scan identifies a product by its barcode and displays its information below the camera.
2. The product remains displayed after its barcode is removed from view.
3. Repeated detections of the same barcode do not replace the displayed details or reset the entered quantity.
4. A successfully recognised different barcode replaces the currently displayed product.
5. If the cashier switches from product A to product B before adding A, the cashier must scan A again to return to it.
6. Scanning selects a product; the cashier explicitly clicks Add to Order to add a quantity to the order.
7. After adding, the quantity input returns to zero and Add to Order becomes disabled.

The assignment requires lookup in `Product.txt`, including using the stored unit price and checking available quantity before adding a product.

### Quantity and order editing

The agreed direction allows the cashier to edit quantities in the order column. Using the minus/plus quantity controls to reduce an order row to zero removes the row entirely. A product already in the order never creates a second row: clicking Add to Order increases its existing row quantity. Scanning alone only selects the product.

The following refinements are proposed:

- Reset the quantity input to zero when a different product becomes selected.
- Re-enable Add to Order when the cashier enters a valid positive quantity, allowing more of the same selected product to be added without scanning a different barcode first.
- Check the resulting total quantity of that product in the order against available stock, including previous additions.
- Recalculate displayed totals immediately after additions, quantity changes, or row removal.

The agreed design allows purchasing exactly the available quantity. The assignment text says **less than** available quantity, so this discrepancy remains for team clarification. Quantity increments and decimal precision for count-based versus weight-based products remain open.

### Product description

**Agreed:** scanned product information appears automatically in the right column. Further description uses an inline dropdown or expandable section, with no separate popup window.

**Agreed control order:** keep the name, price, and available quantity visible, then the quantity input, then the **Description** button. The user moved Description below the quantity controls to avoid shifting them when it expands. Quantity and Add to Order stay outside the expandable section. Disclosure does not require animation; the user plans a basic implementation without animated transitions.

**Current Figma layout proposal:** reserve a fixed region for the disclosure and its expanded content, with Add to Order anchored at the bottom beneath it. The draft reserves 105 logical px for description content and a 24 px gap before Add; these dimensions await screen review. Exact expanded content and overflow handling remain open, but expansion must not displace the quantity controls or primary action.

This provides an explicit Description control, as required by the assignment, alongside the automatic scan result.

It is still open whether selecting an existing order row can populate this same description section, and how that would interact with the currently scanned product and any entered quantity. A prior popup proposal has been superseded by the inline approach.

### Pricing and bill action

**Assignment requirement:** the screen must support generating and displaying the complete bill.

**Agreed placement:** the existing centre order panel also serves as the bill, keeping the order and bill information on the same page. Add Bill ID (nine digits), Order ID (eight digits), a labelled customer-name text field, and payment/order status above the item list. Product rows include the product acronym and discount per unit, including zero when none applies; explicitly label unit prices as excluding tax. The listed bill fields require total tax, so a separate per-product tax column is not needed for this design.

**Agreed:** show subtotal, total tax, total discount, total payable, and the bill actions below the order list. Keeping that summary accessible while the item list scrolls remains a proposed implementation detail.

For the next visual draft, the proposed subtotal label is “Subtotal (before tax & discounts)”, with line amounts clearly using that same basis, then total tax, total discount, and total payable. This clarifies the presentation without settling the tax/discount arithmetic or rounding policy. The zero-tax, zero-discount sample does not resolve those open questions.

The order is in-progress while being built. **Generate Bill** for a paid sale saves a complete bill, records payment details and sends the order to history as paid. **Generate Bill and Put on Hold** also saves a complete bill and sends the order to history as on-hold. The held bill can later be marked paid from Order History, using the same order and bill records. This status workflow is agreed; tax/discount calculation conventions remain open.

### States still to design

- Empty order and no product selected.
- Camera unavailable, denied permission, or stopped.
- Unreadable barcode or barcode with no matching product record.
- Invalid quantity or insufficient stock.
- Product data or output file unavailable.
- Successful addition and successful bill generation.

Inline feedback is proposed for these states; exact messages and recovery controls have not been agreed.

## 3. Past Orders / Order History

**Agreed:** The centre lists past non-archived orders with order number, customer name, date/time, total payable and status. Selecting one highlights it and displays saved customer, item, pricing, payment and bill details in the right panel. Historical charges use saved purchase information. Product, quantity and pricing details are read-only. For an on-hold order, record payment information as needed and use Mark as Paid to update the existing order and bill to paid, preserving both IDs without duplicates. Archive marks the order archived and hides it while retaining its records. There is no search, filter, resume, archived-orders view or restore workflow. Empty and no-selection states should be clear.

**Open:** Sorting, archive confirmation and placement, and archive-status synchronisation between records. See the [Proposed System Design](Proposed%20System%20Design.md) for the current scope. Earlier proposals for search, filters, a separate bill view and Resume Order are superseded.

## 4. Customer and Payment Details

The assignment requires storing customer name, optional contact, payment amount, payment mode (`cash`, `card`, or `wallet`), and payment merchant name. It also requires an order status.

**Agreed:** customer name is entered through a labelled text field in the existing centre order/bill panel.

### Agreed payment workflow

- **Generate Bill and Put on Hold** creates and saves the complete bill and order with **on-hold** status, making them available in Order History before payment.
- In Order History (also referred to as Recent Orders), the cashier selects the held order, records payment information as needed and uses **Mark as Paid**.
- Mark as Paid updates the existing order and bill to **paid**, preserving their Order ID and Bill ID without creating duplicates. Product, quantity and pricing details remain read-only.
- The existing **Generate Bill** action supports an already-paid sale, recording payment and saving the bill and order as paid.

The Order History Figma draft includes inline payment fields and Mark as Paid. The remaining Current Order payment controls still need to be placed in its visual draft; that does not reopen the agreed workflow or require a separate payment page. Error handling on input and the detailed field mechanics remain implementation work. Recording a payment mode does not establish a requirement for integration with a bank or wallet service.

## 5. Bill View

The assignment requires showing the complete order and its bill details on screen and saving the bill to `Bill.txt`.

The bill includes its 9-digit Bill ID, customer name, 8-digit Order ID, product acronyms, unit prices before tax, discounts per unit, quantities, totals before tax, total tax, total discount, final payable amount, and payment status.

**Agreed Current Order direction:** extend the existing centre panel to serve as the bill on the same page. Show bill/order identification, customer-name text input, and payment/order status above the product list. Include product acronym, discount per unit, and a clear before-tax unit-price label in the rows, alongside the existing names, quantities and amounts. The corresponding editable Figma draft is ready; its placement and styling await user review. These controls have not been implemented in the application.

Showing the saved bill within the right panel of Past Orders is agreed. Whether these views share the same component, when IDs are allocated/displayed, and which fields lock after generation remain implementation/behaviour decisions still to settle. The agreed paid and on-hold transitions are described above; no generic status editor is agreed.

Printing, PDF export, payment-gateway integration, and bill-editing controls have not been agreed or explicitly requested by the supplied requirements.

## 6. Existing Barcode Tools

Increment 1 already provides barcode generation, image reading, and camera reading. The camera reader is reused in Current Order.

The navigation placement of the existing generator and image reader remains open. No replacement design for those existing screens has been agreed.

## 7. Decisions Needed Before Implementation

- Resolve the remaining quantity behaviour and catalogue editing and deletion details.
- Finish the Current Order payment-control layout and detailed field mechanics within the agreed paid and on-hold workflow.
- Clarify the discrepancy between the agreed stock equality behavior and the assignment wording; decide stock update timing and handling of weight-based quantities.
- Agree tax/discount arithmetic and monetary rounding.
- Define the multi-product `Bill.txt` format and handling of spaces in saved values. The previously discussed repeated product-field groups are a proposal only.
- Define unique ID generation and the interpretation of the date digits in the Order ID.
- Define file-failure recovery and how a partially saved transaction is handled.

## 8. Product Catalogue

The agreed scope is recorded in the [Proposed System Design](Proposed%20System%20Design.md): View Catalogue and Add Product tabs, selected product details and barcode image, product editing including quantity, Delete Product, and adding products with generated and saved barcode images. Product barcode values cannot change after creation. Zero-stock products remain in the catalogue marked Out of stock and can be restocked by editing quantity. Edit/Save/Cancel mechanics are recommended, not yet approved. Search and sorting are not confirmed.
