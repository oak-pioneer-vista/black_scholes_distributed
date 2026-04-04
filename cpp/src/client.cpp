#include "generated/pricing_request_generated.h"
#include "logger.h"

#include <zmq.hpp>
#include <flatbuffers/flatbuffer_builder.h>
#include <spdlog/spdlog.h>

#include <string>
#include <cstring>
#include <cstdlib>

static void usage(const char* prog) {
    fprintf(stderr,
        "Usage: %s [options]\n"
        "\n"
        "  --id          <string>   request id            (default: req-001)\n"
        "  --spot        <double>   spot price             (default: 100.0)\n"
        "  --low         <double>   lower barrier          (default: 95.0)\n"
        "  --high        <double>   upper barrier          (default: 105.0)\n"
        "  --vol         <double>   annualised vol         (default: 0.20)\n"
        "  --rate        <double>   risk-free rate         (default: 0.05)\n"
        "  --expiry      <double>   time to expiry (yrs)   (default: 1.0)\n"
        "  --alpha       <double>   alpha                  (default: 0.0)\n"
        "  --beta        <double>   beta                   (default: 0.0)\n"
        "  --alpha-min   <double>   alpha lower bound      (default: 0.0)\n"
        "  --alpha-max   <double>   alpha upper bound      (default: 1.0)\n"
        "  --alpha-step  <float>    alpha discretization   (default: 0.1)\n"
        "  --beta-min    <double>   beta lower bound       (default: 0.0)\n"
        "  --beta-max    <double>   beta upper bound       (default: 1.0)\n"
        "  --beta-step   <float>    beta discretization    (default: 0.1)\n"
        "  --hash        <uint64>   request hash           (default: 0)\n"
        "  --host        <string>   server host            (default: localhost)\n"
        "  --port        <int>      server port            (default: 5555)\n"
        "  --log         <file>     log file path          (default: none)\n"
        "  --help                   show this message\n",
        prog);
}

int main(int argc, char* argv[]) {
    // defaults
    std::string id        = "req-001";
    double spot           = 100.0;
    double low            = 95.0;
    double high           = 105.0;
    double vol            = 0.20;
    double rate           = 0.05;
    double expiry         = 1.0;
    double alpha          = 0.0;
    double beta           = 0.0;
    double alpha_min      = 0.0;
    double alpha_max      = 1.0;
    float  alpha_step     = 0.1f;
    double beta_min       = 0.0;
    double beta_max       = 1.0;
    float  beta_step      = 0.1f;
    uint64_t request_hash = 0;
    std::string host      = "localhost";
    int    port           = 5555;
    std::string log_file;

    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&]() -> const char* {
            if (i + 1 >= argc) {
                spdlog::error("{} requires a value", a);
                std::exit(1);
            }
            return argv[++i];
        };
        if      (a == "--help")        { usage(argv[0]); return 0; }
        else if (a == "--id")          id           = next();
        else if (a == "--spot")        spot         = std::atof(next());
        else if (a == "--low")         low          = std::atof(next());
        else if (a == "--high")        high         = std::atof(next());
        else if (a == "--vol")         vol          = std::atof(next());
        else if (a == "--rate")        rate         = std::atof(next());
        else if (a == "--expiry")      expiry       = std::atof(next());
        else if (a == "--alpha")       alpha        = std::atof(next());
        else if (a == "--beta")        beta         = std::atof(next());
        else if (a == "--alpha-min")   alpha_min    = std::atof(next());
        else if (a == "--alpha-max")   alpha_max    = std::atof(next());
        else if (a == "--alpha-step")  alpha_step   = static_cast<float>(std::atof(next()));
        else if (a == "--beta-min")    beta_min     = std::atof(next());
        else if (a == "--beta-max")    beta_max     = std::atof(next());
        else if (a == "--beta-step")   beta_step    = static_cast<float>(std::atof(next()));
        else if (a == "--hash")        request_hash = std::stoull(next());
        else if (a == "--host")        host         = next();
        else if (a == "--port")        port         = std::atoi(next());
        else if (a == "--log")         log_file     = next();
        else { spdlog::error("unknown flag: {}", a); usage(argv[0]); return 1; }
    }

    setup_logger(log_file);

    // Build RangePricingRequest FlatBuffer
    flatbuffers::FlatBufferBuilder builder(256);

    auto rid = builder.CreateString(id);

    RangePricer::PricingRequestBuilder pr(builder);
    pr.add_request_id(rid);
    pr.add_spot(spot);
    pr.add_low(low);
    pr.add_high(high);
    pr.add_vol(vol);
    pr.add_rate(rate);
    pr.add_expiry(expiry);
    pr.add_alpha(alpha);
    pr.add_beta(beta);
    auto pricing_request = pr.Finish();

    RangePricer::RangePricingRequestBuilder rpr(builder);
    rpr.add_request(pricing_request);
    rpr.add_request_hash(request_hash);
    rpr.add_alpha_min(alpha_min);
    rpr.add_alpha_max(alpha_max);
    rpr.add_alpha_step(alpha_step);
    rpr.add_beta_min(beta_min);
    rpr.add_beta_max(beta_max);
    rpr.add_beta_step(beta_step);
    builder.Finish(rpr.Finish());

    std::string endpoint = "tcp://" + host + ":" + std::to_string(port);
    spdlog::info("sending RangePricingRequest id={} hash={} to {}", id, request_hash, endpoint);

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
