#include "generated/pricing_request_generated.h"
#include "logger.h"

#include <gflags/gflags.h>
#include <zmq.hpp>
#include <flatbuffers/flatbuffer_builder.h>
#include <spdlog/spdlog.h>

#include <string>
#include <cstring>

DEFINE_string(id,         "req-001",   "Request ID");
DEFINE_double(spot,       100.0,       "Spot price");
DEFINE_double(low,        95.0,        "Lower barrier");
DEFINE_double(high,       105.0,       "Upper barrier");
DEFINE_double(vol,        0.20,        "Annualised volatility");
DEFINE_double(rate,       0.05,        "Risk-free rate");
DEFINE_double(expiry,     1.0,         "Time to expiry (years)");
DEFINE_double(alpha,      0.0,         "Alpha");
DEFINE_double(beta,       0.0,         "Beta");
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

    auto rid = builder.CreateString(FLAGS_id);

    RangePricer::PricingRequestBuilder pr(builder);
    pr.add_request_id(rid);
    pr.add_spot(FLAGS_spot);
    pr.add_low(FLAGS_low);
    pr.add_high(FLAGS_high);
    pr.add_vol(FLAGS_vol);
    pr.add_rate(FLAGS_rate);
    pr.add_expiry(FLAGS_expiry);
    pr.add_alpha(FLAGS_alpha);
    pr.add_beta(FLAGS_beta);
    auto pricing_request = pr.Finish();

    RangePricer::RangePricingRequestBuilder rpr(builder);
    rpr.add_request(pricing_request);
    rpr.add_request_hash(FLAGS_hash);
    rpr.add_alpha_min(FLAGS_alpha_min);
    rpr.add_alpha_max(FLAGS_alpha_max);
    rpr.add_alpha_step(static_cast<float>(FLAGS_alpha_step));
    rpr.add_beta_min(FLAGS_beta_min);
    rpr.add_beta_max(FLAGS_beta_max);
    rpr.add_beta_step(static_cast<float>(FLAGS_beta_step));
    builder.Finish(rpr.Finish());

    std::string endpoint = "tcp://" + FLAGS_host + ":" + std::to_string(FLAGS_port);
    spdlog::info("sending RangePricingRequest id={} hash={} to {}", FLAGS_id, FLAGS_hash, endpoint);

    zmq::context_t ctx{1};
    zmq::socket_t  sock{ctx, zmq::socket_type::req};
    sock.connect(endpoint);

    sock.send(zmq::buffer(builder.GetBufferPointer(), builder.GetSize()),
              zmq::send_flags::none);

    zmq::message_t reply;
    [[maybe_unused]] auto _ = sock.recv(reply, zmq::recv_flags::none);

    std::string response(static_cast<char*>(reply.data()), reply.size());
    spdlog::info("response: {}", response);

    spdlog::shutdown();
    return 0;
}
