#include "range_pricer.h"

#include <flatbuffers/flatbuffer_builder.h>

#include <cmath>
#include <stdexcept>

namespace range_pricer {

std::pair<double, double> price(const RangePricer::PricingRequest& req) {
    double S   = req.spot();
    double L   = req.low();
    double H   = req.high();
    double vol = req.vol();
    double r   = req.rate();
    double T   = req.expiry();

    if (L >= H)    throw std::invalid_argument("low must be less than high");
    if (vol <= 0)  throw std::invalid_argument("vol must be positive");
    if (T <= 0)    throw std::invalid_argument("expiry must be positive");

    double sqrt_T  = std::sqrt(T);
    double vol_rt  = vol * sqrt_T;
    double drift   = (r - 0.5 * vol * vol) * T;

    double d_low  = (std::log(S / L) + drift) / vol_rt;
    double d_high = (std::log(S / H) + drift) / vol_rt;

    auto N      = [](double x) { return 0.5 * std::erfc(-x / std::sqrt(2.0)); };
    auto Nprime = [](double x) { return std::exp(-0.5 * x * x) / std::sqrt(2.0 * M_PI); };

    double disc = std::exp(-r * T);
    double pv   = disc * (N(d_low) - N(d_high));
    double hr   = disc * (Nprime(d_low) - Nprime(d_high)) / (S * vol_rt);

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
