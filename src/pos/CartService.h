#pragma once

#include "PosTypes.h"

using namespace std;

class ProductService;

class CartService {
private:
    ProductService& products_;
    vector<CartItem> items_;
    optional<HeldCheckout> held_;

public:
    CartService(ProductService& products);

    Status addByBarcode(const string& barcode, int64_t addition);
    Status setQuantity(const string& productId, int64_t quantity);
    vector<CartItem> items() const;
    void restoreHeldItems(const HeldCheckout& held);
    void clear();
};
