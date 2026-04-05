#pragma once

#include "generated/pricing_request_generated.h"

#include <utility>
#include <vector>

namespace range_pricer {

// Returns {price, hedge_ratio}.
std::pair<float, float> price(const RangePricer::PricingRequest& req);

// Price a batch and return a serialised BatchPricingResponse FlatBuffer.
std::vector<uint8_t> price_batch(const RangePricer::BatchPricingRequest& batch);

} // namespace range_pricer
