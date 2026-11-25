#pragma once

#include "PosTypes.h"

using namespace std;

class DiscountService;
class HistoryService;
class IdGenerator;
class IPosStore;
class PricingCalculator;
class ProductService;

class CheckoutService {
public:
    CheckoutService(IPosStore& store, ProductService& products,
                    const PricingCalculator& pricing, const DiscountService& discounts,
                    const IdGenerator& ids, HistoryService& history);

    Result<PricingResult> quote(const vector<CartItem>& items, const string& heldOrderId,
                                const Timestamp& now) const;
    Result<SaleRecord> generateBill(const vector<CartItem>& items, const Customer& customer,
                                    const Payment& payment, OrderStatus status, const Timestamp& now,
                                    const optional<int64_t>& expectedPayable);
    Result<HeldCheckout> resumeHeldOrder(const string& orderId) const;
    Result<SaleRecord> saveHeldOrder(const string& orderId, const vector<CartItem>& items,
                                     const Customer& customer, const Payment& payment,
                                     OrderStatus status, const Timestamp& now,
                                     const optional<int64_t>& expectedPayable);

    Result<PaidEditDraft> previewPaidEdit(const string& orderId, const vector<QuantityChange>& changes,
                                         const Timestamp& now) const;
    Result<SaleRecord> applyPaidEdit(const PaidEditDraft& draft, const ManagerApproval& approval,
                                    int64_t settledDelta, const Timestamp& now);

private:
    Result<PricingResult> quoteFromSnapshot(const StoreData& data, const vector<CartItem>& items,
                                            const string& heldOrderId, const Timestamp& now) const;
    Result<SaleRecord> save(const vector<CartItem>& items, const Customer& customer,
                            const Payment& payment, OrderStatus status, const Timestamp& now,
                            const optional<int64_t>& expectedPayable, const string* heldOrderId);
    Result<SaleRecord> completePaidSave(SaleRecord sale, string message);

    IPosStore& store_;
    ProductService& products_;
    const PricingCalculator& pricing_;
    const DiscountService& discounts_;
    const IdGenerator& ids_;
    HistoryService& history_;
};
