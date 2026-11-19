# Increment 2: Proposed System Design

## Navigation

The Qt desktop POS has a far-left sidebar for Current Order, Order History and Product Catalogue. Place Order and Current Order refer to the same page.

## Current Order

The centre panel contains the order and its bill. The right panel contains the camera preview and scanned-product details.

- Scan a barcode with the camera to select a product and display its details from `Product.txt`. Scanning alone does not add it to the order.
- Keep the selected product displayed after it leaves the camera. Repeated detections of the same barcode do not reset its details or quantity. A different recognised product replaces it.
- Show available stock, a quantity input, an inline expandable **Description** control and **Add to Order**. Place Description below the quantity controls and keep Add to Order anchored at the bottom. No animation is planned.
- Add the entered quantity only when **Add to Order** is clicked. If the product is already in the order, increase its existing row instead of adding another. Reset the input to zero and disable Add after adding.
- Check available stock. Buying exactly the remaining quantity is allowed.
- Show each product's identifying information, acronym, unit price excluding tax, discount per unit, quantity and line amount. Minus and plus controls change quantities; reducing a row to zero removes it.
- Show product and unit counts, subtotal, total tax, total discount and total payable in PKR.
- Keep the current order **in-progress** while the cashier builds it.
- **Generate Bill** for a paid sale generates and saves the complete bill, records payment details and sends the order to history as **paid**.
- **Generate Bill and Put on Hold** generates and saves the complete bill and sends the order to history as **on-hold**.
- Present the complete bill in the same centre panel, including the nine-digit Bill ID, eight-digit Order ID, a labelled customer-name field and order/payment status.
- Capture customer name, optional contact, payment amount, payment mode and merchant.

## Order History

The centre column lists past non-archived orders. Selecting an order highlights it and displays its details in the right panel.

- Show the order number, customer name, date and time, total payable and status in the list.
- Show saved customer and contact details, products, quantities, purchase prices, discounts, tax, totals, payment details and bill identification in the right panel. Use saved purchase information for historical charges.
- Keep product, quantity and pricing details read-only. For an **on-hold** order, record payment information as needed and select **Mark as Paid**. Update the existing order and bill to paid, retaining the same Order ID and Bill ID without creating duplicates.
- **Archive** marks the selected order archived and hides it from history while retaining its records.
- Show a clear empty state and a prompt when no order is selected.
- Provide no search, filters, resume, archived-orders view or restore workflow.

## Product Catalogue

The page has **View Catalogue** and **Add Product** tabs at the top. An **Add Product** button also opens the Add Product tab.

### View Catalogue

- List product names, quantities and basic information such as price in the centre column.
- Show all selected product specifications in the right panel: product ID, barcode value, acronym, name, measurement unit, unit price, tax, discount per unit, available quantity, and barcode image filename and path. Display the barcode image as well.
- Allow product details, including quantity, to be modified. A product's barcode value cannot be changed after creation.
- Keep products with zero quantity in the catalogue, marked **Out of stock**. Restock by editing the quantity.
- Provide a **Delete Product** button.

### Add Product

- Provide a form covering all product data.
- Generate the barcode image using the existing team-written six-digit barcode encoder. The barcode represents the numeric lookup value.
- Automatically generate the image filename and path, save the image and display its preview, filename and path.
- Save the product record to `Product.txt`.

## Shared storage and input

- Use `Product.txt` for products, `Order.txt` for order records, `OrderDetail.txt` for order items and `Bill.txt` for bills. Order records include date and time; orders and bills have their required IDs.
- Reuse the Increment 1 barcode reader and generator in the POS workflows.
- Error handling on input.

See the [Increment 2 requirements](Increment%202%20Requirements.md) for the specified record fields.
