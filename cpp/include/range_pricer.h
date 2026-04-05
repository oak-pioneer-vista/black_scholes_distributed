#pragma once

#include "generated/pricing_request_generated.h"

#include <vector>

namespace range_pricer {

struct PricingInput {
    uint32_t idx;
    float    alpha;
    float    beta;
};

struct PricingOutput {
    uint32_t idx;
    float    alpha;
    float    beta;
    float    price;
    float    hedge_ratio;
};

// High-level entry point: takes plain params and an array of (idx, alpha, beta),
// returns an array of (idx, alpha, beta, price, hedge_ratio).
std::vector<PricingOutput> price(
    RangePricer::OptionType option_type,
    float stock_price, float strike_price, float interest_rate,
    float time_to_maturity, float stock_discretization,
    const std::vector<PricingInput>& inputs);

// FlatBuffer entry point used by worker threads.
// Returns a serialised BatchPricingResponse FlatBuffer.
std::vector<uint8_t> process(
    const RangePricer::PricingParams& params,
    const flatbuffers::Vector<const RangePricer::AlphaBetaPair *>& pairs,
    uint64_t batch_id);

} // namespace range_pricer
