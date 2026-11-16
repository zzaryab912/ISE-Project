#pragma once

#include <cstdint>
#include <string>
#include <vector>

using namespace std;

// -------------------------------------------------------------------------------
// enums


enum class Unit {               // Units can either be Count or Weight
    Count,
    Weight
};

enum class OrderStatus {        // every possible order status
    InProgress,
    OnHold,
    Paid,
    Archived
};

enum class PaymentMode {        // all different payment modes
    Cash,
    Card,
    Wallet
};

// -------------------------------------------------------------------------------
// Structs / Classes

struct Status {                 // used when a value is not needed
    bool success = false;
    string message {};          // the {} initializes the string as an empty string here
};

template <typename T>           // used when a value is also needed along with information
struct Result {                 // or an object in some cases. T value {}; initializes that object with default values
    bool success=false;
    string message {};
    T value {};                 // initializes value to default value depending on data type
};

struct Product {
    string id {};
    string barcode {};
    string acronym {};
    string name {};

    Unit unit = Unit::Count;        // object of Unit created, with Count as the unit
    int64_t unitPrice = 0;          // initializing the unit price
    int64_t taxPerUnit = 0;         // initializing tax per unit
    int64_t discountPerUnit = 0;
    int64_t available=0;            // every product has availability status, this represents the quantity. Two strikes with one stone.
    string barcodeImagePath{};      // every product needs a barcode image path as well. 
                                    // these values are generated in 64 bits. 
                                    // a little (very) extravagant yes, but allows for greater range 
};

struct CartItem {                   // product added to cart (has its own quatity) is a CartItem
    Product product {};
    int64_t quantity = 0;
};

struct Totals {                     // keep in mind, all these values are stored in paisas
    int64_t subtotal=0;             // 100 paisa equals one rupee
    int64_t tax=0;
    int64_t discount = 0;
    int64_t payable =0;             // SaleRecord contains a Totals object as a field. This keeps the amounts together in a bill
};

                                    // totals is also used as a return value, for PricingCalculator, CartService, and Result
                                    
struct Customer {
    string name {};
    string contact {};
};

struct Payment {                    // stores whether a payment has been recorded, its amount, PaymentMode (enum) and the merchant name
    bool recorded = false;
    int64_t amount = 0;
    PaymentMode mode = PaymentMode::Cash;
    string merchant {};
};
    
struct Timestamp {                  // stored in SaleRecord, used by generateBill and saleIds uses the date when generating IDs
    string date {};
    string time {};
};

struct SaleIds {
    string orderId {};
    string billId {};
};

struct SaleLine {                   // saleline is one purchased product. 
    string productId {};            // each purchased product uses the following data members
    string barcode {};
    string acronym {};
    int64_t unitPrice =0;
    int64_t discountPerUnit=0;
    int64_t quantity =0;
};

struct SaleRecord {
    Customer customer {};
    SaleIds ids {};                                 // has an orderId and a billId
    Timestamp timestamp {};                         // every sale needs a timestamp
    Payment payment {};
    OrderStatus status = OrderStatus::InProgress;
    vector<SaleLine> lines {};                      // helps accommodate lots of SaleLines (purchased products) 
    Totals totals {};
};

struct StoreData {
    vector <Product> products {};
    vector<SaleRecord> sales {};
};

// Store Data is the shared container for the two main collections: product catalogue and saved sales
// vector<Product> products is a list of all products, and each has their own data
// SaleRecord sales is for the past sales.



