# Increment 2 Requirements

Demo of increment 2 will be held in class on November 19.

The barcode reader and generator have been implemented in increment 1. Increment 2 related requirements are as follows:

1. Barcode reader should be camera based.
2. The whole POS should be GUI based.
3. On reading the barcode, the system shall show the product specifications stored in a file named `Product.txt`. Contents of the file shall be provided in the next point.
4. `Product.txt` file shall have one row for each product. Each row shall have a `#` as an end of line marker. Each line shall have following information about the product in the exact order, separated by a space:
   - product id
   - product barcode
   - product acronym (max 8 characters)
   - product name
   - product measurement unit (count, weight)
   - product unit price
   - tax on product (amount in PKR)
   - discount on one unit of product
   - available quantity
   - barcode file name and path
5. The system shall allow the cashier to record an order and generate a bill using the camera based reading of the bar code. When the bar code is scanned while recording an order, the system shall read the unit price from the `Product.txt` file. The product shall be added to bill if the required quantity is less than the available quantity.
6. A cashier or a manager can view product description by scanning the bar code. A cashier can also view the description by pressing Description button while recording an order.
7. When a bill is generated an order is saved in the file named `Order.txt`.
8. The order file has one order record in one line that has a `#` as an end of line marker.
9. Each line in the `Order.txt` has the following information in the specified sequence separated by a space:
   - an 8 digit Order Id, where last two digits indicate the date or order
   - order date
   - order time
   - customer name
   - customer contact (optional)
   - order status (in-progress, on-hold, paid, archived)
   - payment amount
   - payment mode (cash, card, wallet)
   - payment merchant name (e.g. ABL, JazzCash etc.)
10. Against each other there shall be order detail records in `OrderDetail.txt` file. Each row shall record the stored products and their quantity against the specified order id:
    - order id from the `Order.txt`
    - product id
    - product bar code
    - quantity
11. The cashier shall be able to generate bill from the screen that enables recording of an order. The bill is shown on the screen with the complete order and its details. The bill is saved in the file named `Bill.txt` which contains one bill in each line. Each line has a `#` as the end of line marker. Each line has the following space separated information:
    - Bill Id (9 digit id)
    - customer name
    - Order Id (from `Order.txt` file)
    - product acronym
    - product unit price without tax
    - discount per unit (if applicable)
    - product quantity
    - total bill without tax
    - total tax
    - total discount
    - total payable bill
    - bill payment status (order status from `Order.txt`)

## Note

There will be marks for following SE principles especially modularity (SRP), AoC (OCP) and focusing on relevant qualities especially quality and reusability of code.
