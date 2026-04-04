#pragma once

#include "generated/pricing_request_generated.h"

#include <string>
#include <vector>

namespace range_pricer {

struct PricingResult {
    std::string request_id;
    double alpha;
    double beta;
    double pv;
};

double price(double spot, double low, double high, double vol, double rate, double expiry);

double price(const RangePricer::PricingRequest& req);

std::vector<PricingResult> price_batch(const RangePricer::BatchPricingRequest& batch);

} // namespace range_pricer
