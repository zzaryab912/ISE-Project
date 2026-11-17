#pragma once

#include "PosTypes.h"

class IPosStore;
class ProductService;
class PricingCalculator;
class IdGenerator;

class CheckoutService {
public:
    CheckoutService(IPosStore& store, ProductService& products,
                    const PricingCalculator& pricing, const IdGenerator& ids);

    // Saves a paid or held bill. The caller clears the cart after success.
    Result<SaleRecord> generateBill(const std::vector<CartItem>& items,
                                   const Customer& customer,
                                   const Payment& payment,
                                   OrderStatus status,
                                   const Timestamp& timestamp);

private:
    IPosStore& store_;
    ProductService& products_;
    const PricingCalculator& pricing_;
    const IdGenerator& ids_;
};