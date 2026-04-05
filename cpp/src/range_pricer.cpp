#include "range_pricer.h"

#include <flatbuffers/flatbuffer_builder.h>

#include <cmath>
#include <stdexcept>

// Highway: must define HWY_TARGET_INCLUDE before including highway.h
#undef HWY_TARGET_INCLUDE
#define HWY_TARGET_INCLUDE "range_pricer.cpp"
#include <hwy/foreach_target.h>
#include <hwy/highway.h>
#include <hwy/contrib/math/math-inl.h>

HWY_BEFORE_NAMESPACE();
namespace range_pricer {
namespace HWY_NAMESPACE {

namespace hn = hwy::HWY_NAMESPACE;

void vectorized_price(
    float S, float K, float r, float T, float ds,
    const float* HWY_RESTRICT alphas,
    const float* HWY_RESTRICT betas,
    float* HWY_RESTRICT prices,
    float* HWY_RESTRICT hedge_ratios,
    size_t count) {

    const hn::ScalableTag<float> d;
    const size_t N = hn::Lanes(d);

    // Broadcast scalar params to vectors
    const auto v_r = hn::Set(d, r);
    const auto v_T = hn::Set(d, T);
    const auto v_S = hn::Set(d, S);
    const auto v_K = hn::Set(d, K);
    const auto v_ds = hn::Set(d, ds);

    // TODO: implement full pricing model using alpha/beta per lane
    // For now: pv = exp(-r * T), hedge_ratio = 0
    const auto v_neg_rT = hn::Neg(hn::Mul(v_r, v_T));
    const auto v_pv = hn::Exp(d, v_neg_rT);
    const auto v_hr = hn::Zero(d);

    size_t i = 0;
    for (; i + N <= count; i += N) {
        hn::Store(v_pv, d, prices + i);
        hn::Store(v_hr, d, hedge_ratios + i);
    }

    // Scalar tail
    if (i < count) {
        float pv = std::exp(-r * T);
        for (; i < count; ++i) {
            prices[i] = pv;
            hedge_ratios[i] = 0.0f;
        }
    }
}

}  // namespace HWY_NAMESPACE
}  // namespace range_pricer
HWY_AFTER_NAMESPACE();

#if HWY_ONCE

namespace range_pricer {

HWY_EXPORT(vectorized_price);

// ── price: vectorized plain C++ entry point ──────────────────────────────────

std::vector<PricingOutput> price(
    RangePricer::OptionType option_type,
    float stock_price, float strike_price, float interest_rate,
    float time_to_maturity, float stock_discretization,
    const std::vector<PricingInput>& inputs) {

    if (time_to_maturity <= 0)
        throw std::invalid_argument("time_to_maturity must be positive");
    if (stock_discretization <= 0)
        throw std::invalid_argument("stock_discretization must be positive");

    const size_t n = inputs.size();
    if (n == 0) return {};

    // Extract SoA from inputs
    std::vector<float> alphas(n), betas(n), prices(n), hedge_ratios(n);
    for (size_t i = 0; i < n; ++i) {
        alphas[i] = inputs[i].alpha;
        betas[i]  = inputs[i].beta;
    }

    // Dispatch to best available SIMD implementation
    HWY_DYNAMIC_DISPATCH(vectorized_price)(
        stock_price, strike_price, interest_rate,
        time_to_maturity, stock_discretization,
        alphas.data(), betas.data(),
        prices.data(), hedge_ratios.data(), n);

    // Pack into output
    std::vector<PricingOutput> outputs(n);
    for (size_t i = 0; i < n; ++i) {
        outputs[i] = {inputs[i].idx, inputs[i].alpha, inputs[i].beta,
                      prices[i], hedge_ratios[i]};
    }
    return outputs;
}

// ── process: FlatBuffer entry point for worker threads ───────────────────────

std::vector<uint8_t> process(
    const RangePricer::PricingParams& params,
    const flatbuffers::Vector<const RangePricer::AlphaBetaPair *>& pairs,
    uint64_t batch_id) {

    if (params.time_to_maturity() <= 0)
        throw std::invalid_argument("time_to_maturity must be positive");
    if (params.stock_discretization() <= 0)
        throw std::invalid_argument("stock_discretization must be positive");

    const size_t n = pairs.size();

    // Extract SoA
    std::vector<float> alphas(n), betas(n), prices(n), hedge_ratios(n);
    for (size_t i = 0; i < n; ++i) {
        alphas[i] = pairs[i]->alpha();
        betas[i]  = pairs[i]->beta();
    }

    // Dispatch to best available SIMD implementation
    HWY_DYNAMIC_DISPATCH(vectorized_price)(
        params.stock_price(), params.strike_price(), params.interest_rate(),
        params.time_to_maturity(), params.stock_discretization(),
        alphas.data(), betas.data(),
        prices.data(), hedge_ratios.data(), n);

    // Build FlatBuffer response
    flatbuffers::FlatBufferBuilder builder(512);

    std::vector<flatbuffers::Offset<RangePricer::PricingResponse>> result_offsets;
    result_offsets.reserve(n);
    for (size_t i = 0; i < n; ++i) {
        result_offsets.push_back(RangePricer::CreatePricingResponse(
            builder,
            pairs[i]->idx(),
            alphas[i],
            betas[i],
            prices[i],
            hedge_ratios[i]
        ));
    }

    auto results_vec = builder.CreateVector(result_offsets);
    auto batch_response = RangePricer::CreateBatchPricingResponse(
        builder, batch_id, results_vec);
    builder.Finish(batch_response);

    auto* buf = builder.GetBufferPointer();
    return {buf, buf + builder.GetSize()};
}

}  // namespace range_pricer

#endif  // HWY_ONCE
