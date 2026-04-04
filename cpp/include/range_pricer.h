#pragma once

#include "generated/pricing_request_generated.h"

namespace range_pricer {

double price(double spot, double low, double high, double vol, double rate, double expiry);

double price(const RangePricer::PricingRequest& req);

} // namespace range_pricer
