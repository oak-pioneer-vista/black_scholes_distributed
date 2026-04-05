#include "range_pricer.h"

#include <flatbuffers/flatbuffer_builder.h>

#include <cmath>
#include <stdexcept>

namespace range_pricer {

// ── core pricing logic (shared by both entry points) ─────────────────────────

static std::pair<float, float> compute_price(
    RangePricer::OptionType option_type,
    float S, float K, float r, float T, float ds) {

    if (T <= 0)  throw std::invalid_argument("time_to_maturity must be positive");
    if (ds <= 0) throw std::invalid_argument("stock_discretization must be positive");

    // TODO: implement pricing model
    float pv = std::exp(-r * T);
    float hr = 0.0f;

    return {pv, hr};
}

// ── price: plain C++ entry point ────────────────────────────────────���────────

std::vector<PricingOutput> price(
    RangePricer::OptionType option_type,
    float stock_price, float strike_price, float interest_rate,
    float time_to_maturity, float stock_discretization,
    const std::vector<PricingInput>& inputs) {

    auto [pv, hr] = compute_price(
        option_type, stock_price, strike_price, interest_rate,
        time_to_maturity, stock_discretization);

    std::vector<PricingOutput> outputs;
    outputs.reserve(inputs.size());
    for (const auto& in : inputs) {
        outputs.push_back({in.idx, in.alpha, in.beta, pv, hr});
    }
    return outputs;
}

// ── process: FlatBuffer entry point for worker threads ───────────────────────

std::vector<uint8_t> process(
    const RangePricer::PricingParams& params,
    const flatbuffers::Vector<const RangePricer::AlphaBetaPair *>& pairs,
    uint64_t batch_id) {

    auto [pv, hr] = compute_price(
        params.option_type(),
        params.stock_price(), params.strike_price(), params.interest_rate(),
        params.time_to_maturity(), params.stock_discretization());

    flatbuffers::FlatBufferBuilder builder(512);

    std::vector<flatbuffers::Offset<RangePricer::PricingResponse>> result_offsets;
    result_offsets.reserve(pairs.size());
    for (const auto* ab : pairs) {
        result_offsets.push_back(RangePricer::CreatePricingResponse(
            builder,
            ab->idx(),
            ab->alpha(),
            ab->beta(),
            pv,
            hr
        ));
    }

    auto results_vec = builder.CreateVector(result_offsets);
    auto batch_response = RangePricer::CreateBatchPricingResponse(
        builder, batch_id, results_vec);
    builder.Finish(batch_response);

    auto* buf = builder.GetBufferPointer();
    return {buf, buf + builder.GetSize()};
}

} // namespace range_pricer
