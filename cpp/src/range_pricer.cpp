#include "range_pricer.h"

#include <flatbuffers/flatbuffer_builder.h>

#include <cmath>
#include <stdexcept>

namespace range_pricer {

std::pair<float, float> price(const RangePricer::PricingParams& req) {
    float S  = req.stock_price();
    float K  = req.strike_price();
    float r  = req.interest_rate();
    float T  = req.time_to_maturity();
    float ds = req.stock_discretization();

    if (T <= 0)  throw std::invalid_argument("time_to_maturity must be positive");
    if (ds <= 0) throw std::invalid_argument("stock_discretization must be positive");

    // TODO: implement pricing model
    float pv = std::exp(-r * T);
    float hr = 0.0f;

    return {pv, hr};
}

std::vector<uint8_t> price_batch(const RangePricer::BatchPricingRequest& batch) {
    flatbuffers::FlatBufferBuilder builder(512);

    const auto* req   = batch.request();
    const auto* pairs = batch.pairs();

    auto [base_pv, base_hr] = price(*req);

    std::vector<flatbuffers::Offset<RangePricer::PricingResponse>> result_offsets;
    result_offsets.reserve(pairs->size());
    for (const auto* ab : *pairs) {
        result_offsets.push_back(RangePricer::CreatePricingResponse(
            builder,
            ab->idx(),
            ab->alpha(),
            ab->beta(),
            base_pv,
            base_hr
        ));
    }

    auto results_vec = builder.CreateVector(result_offsets);
    auto batch_response = RangePricer::CreateBatchPricingResponse(
        builder, batch.batch_counter_id(), results_vec);
    builder.Finish(batch_response);

    auto* buf = builder.GetBufferPointer();
    return {buf, buf + builder.GetSize()};
}

} // namespace range_pricer
