#include "generated/pricing_request_generated.h"
#include "logger.h"

#include <gflags/gflags.h>
#include <zmq.hpp>
#include <flatbuffers/flatbuffer_builder.h>
#include <flatbuffers/verifier.h>
#include <spdlog/spdlog.h>

#include <string>
#include <cstring>

DEFINE_double(stock_price,          100.0,   "Stock price");
DEFINE_double(strike_price,         100.0,   "Strike price");
DEFINE_double(interest_rate,        0.05,    "Interest rate");
DEFINE_double(time_to_maturity,     1.0,     "Time to maturity (years)");
DEFINE_double(stock_discretization, 0.01,    "Stock discretization");
DEFINE_double(alpha_min,  0.0,         "Alpha lower bound");
DEFINE_double(alpha_max,  1.0,         "Alpha upper bound");
DEFINE_double(alpha_step, 0.1,         "Alpha discretization step");
DEFINE_double(beta_min,   0.0,         "Beta lower bound");
DEFINE_double(beta_max,   1.0,         "Beta upper bound");
DEFINE_double(beta_step,  0.1,         "Beta discretization step");
DEFINE_uint64(hash,       0,           "Request hash");
DEFINE_string(host,       "localhost", "Server host");
DEFINE_int32 (port,       5555,        "Server port");
DEFINE_string(log,        "",          "Log file path (empty for console only)");

int main(int argc, char* argv[]) {
    gflags::SetUsageMessage("Range pricer client — send a RangePricingRequest to the server");
    gflags::ParseCommandLineFlags(&argc, &argv, true);

    setup_logger(FLAGS_log);

    // Build RangePricingRequest FlatBuffer
    flatbuffers::FlatBufferBuilder builder(256);

    RangePricer::PricingRequestBuilder pr(builder);
    pr.add_stock_price(FLAGS_stock_price);
    pr.add_strike_price(FLAGS_strike_price);
    pr.add_interest_rate(FLAGS_interest_rate);
    pr.add_time_to_maturity(FLAGS_time_to_maturity);
    pr.add_stock_discretization(FLAGS_stock_discretization);
    auto pricing_request = pr.Finish();

    RangePricer::RangePricingRequestBuilder rpr(builder);
    rpr.add_request(pricing_request);
    rpr.add_request_hash(FLAGS_hash);
    rpr.add_alpha_min(FLAGS_alpha_min);
    rpr.add_alpha_max(FLAGS_alpha_max);
    rpr.add_alpha_step(FLAGS_alpha_step);
    rpr.add_beta_min(FLAGS_beta_min);
    rpr.add_beta_max(FLAGS_beta_max);
    rpr.add_beta_step(FLAGS_beta_step);
    builder.Finish(rpr.Finish());

    std::string endpoint = "tcp://" + FLAGS_host + ":" + std::to_string(FLAGS_port);
    spdlog::info("sending RangePricingRequest hash={} to {}", FLAGS_hash, endpoint);

    zmq::context_t ctx{1};
    zmq::socket_t  sock{ctx, zmq::socket_type::req};
    sock.connect(endpoint);

    sock.send(zmq::buffer(builder.GetBufferPointer(), builder.GetSize()),
              zmq::send_flags::none);


    spdlog::info("done sending RangePricingRequest hash={} to {}", FLAGS_hash, endpoint);


    zmq::message_t reply;
    [[maybe_unused]] auto _ = sock.recv(reply, zmq::recv_flags::none);

    flatbuffers::Verifier verifier(static_cast<const uint8_t*>(reply.data()), reply.size());
    const auto* range_response = flatbuffers::GetRoot<RangePricer::RangePricingResponse>(reply.data());
    if (!range_response->Verify(verifier)) {
        std::string raw(static_cast<char*>(reply.data()), reply.size());
        spdlog::error("invalid response ({} bytes): {}", reply.size(), raw);
        spdlog::shutdown();
        return 1;
    }

    spdlog::info("request_hash={}", range_response->request_hash());
    if (range_response->batches()) {
        for (const auto* batch : *range_response->batches()) {
            spdlog::info("  batch_id={}", batch->batch_id());
            if (batch->results()) {
                for (const auto* r : *batch->results()) {
                    spdlog::info("    alpha={:.4f} beta={:.4f} price={:.6f} hedge_ratio={:.6f}",
                                 r->alpha(), r->beta(), r->price(), r->hedge_ratio());
                }
            }
        }
    }

    spdlog::shutdown();
    return 0;
}
