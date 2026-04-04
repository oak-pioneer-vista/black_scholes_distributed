#include "range_pricer.h"

#include <cmath>
#include <stdexcept>

namespace range_pricer {

double price(double spot, double low, double high, double vol, double rate, double expiry) {
    if (low >= high)   throw std::invalid_argument("low must be less than high");
    if (vol <= 0.0)    throw std::invalid_argument("vol must be positive");
    if (expiry <= 0.0) throw std::invalid_argument("expiry must be positive");

    double d_low  = (std::log(spot / low)  + (rate - 0.5 * vol * vol) * expiry) / (vol * std::sqrt(expiry));
    double d_high = (std::log(spot / high) + (rate - 0.5 * vol * vol) * expiry) / (vol * std::sqrt(expiry));

    auto N = [](double x) { return 0.5 * std::erfc(-x / std::sqrt(2.0)); };

    return std::exp(-rate * expiry) * (N(d_low) - N(d_high));
}

double price(const RangePricer::PricingRequest& req) {
    return price(req.spot(), req.low(), req.high(), req.vol(), req.rate(), req.expiry());
}

std::vector<PricingResult> price_batch(const RangePricer::BatchPricingRequest& batch) {
    std::vector<PricingResult> results;
    if (!batch.requests()) return results;

    results.reserve(batch.requests()->size());
    for (const auto* req : *batch.requests()) {
        results.push_back({
            req->request_id()->c_str(),
            req->alpha(),
            req->beta(),
            price(*req)
        });
    }
    return results;
}

} // namespace range_pricer
