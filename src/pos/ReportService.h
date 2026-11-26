#pragma once

#include "PosTypes.h"

using namespace std;

class IPosStore;
class ProductService;

class ReportService {
private:
    const IPosStore& store_;
    const ProductService& products_;

public:
    ReportService(const IPosStore& store, const ProductService& products);

    Result<ReportResult> generate(const ReportRequest& request) const;
};
