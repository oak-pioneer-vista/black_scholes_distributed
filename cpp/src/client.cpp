#include "generated/pricing_request_generated.h"
#include "logger.h"

#include <zmq.hpp>
#include <flatbuffers/flatbuffer_builder.h>
#include <spdlog/spdlog.h>

#include <string>
#include <cstring>
#include <cstdlib>

static void usage(const char* prog) {
    // Usage goes to stderr directly — it is not a log event
    fprintf(stderr,
        "Usage: %s [options]\n"
        "\n"
        "  --id      <string>   request id           (default: req-001)\n"
        "  --spot    <double>   spot price            (default: 100.0)\n"
        "  --low     <double>   lower barrier         (default: 95.0)\n"
        "  --high    <double>   upper barrier         (default: 105.0)\n"
        "  --vol     <double>   annualised vol        (default: 0.20)\n"
        "  --rate    <double>   risk-free rate        (default: 0.05)\n"
        "  --expiry  <double>   time to expiry (yrs)  (default: 1.0)\n"
        "  --host    <string>   server host           (default: localhost)\n"
        "  --port    <int>      server port           (default: 5555)\n"
        "  --log     <file>     log file path         (default: none)\n"
        "  --help               show this message\n",
        prog);
}

int main(int argc, char* argv[]) {
    std::string id       = "req-001";
    double spot          = 100.0;
    double low           = 95.0;
    double high          = 105.0;
    double vol           = 0.20;
    double rate          = 0.05;
    double expiry        = 1.0;
    std::string host     = "localhost";
    int    port          = 5555;
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
        if      (a == "--help")   { usage(argv[0]); return 0; }
        else if (a == "--id")     id       = next();
        else if (a == "--spot")   spot     = std::atof(next());
        else if (a == "--low")    low      = std::atof(next());
        else if (a == "--high")   high     = std::atof(next());
        else if (a == "--vol")    vol      = std::atof(next());
        else if (a == "--rate")   rate     = std::atof(next());
        else if (a == "--expiry") expiry   = std::atof(next());
        else if (a == "--host")   host     = next();
        else if (a == "--port")   port     = std::atoi(next());
        else if (a == "--log")    log_file = next();
        else { spdlog::error("unknown flag: {}", a); usage(argv[0]); return 1; }
    }

    setup_logger(log_file);

    // Build FlatBuffer
    flatbuffers::FlatBufferBuilder builder(128);
    auto rid = builder.CreateString(id);
    RangePricer::PricingRequestBuilder req(builder);
    req.add_request_id(rid);
    req.add_spot(spot);
    req.add_low(low);
    req.add_high(high);
    req.add_vol(vol);
    req.add_rate(rate);
    req.add_expiry(expiry);
    builder.Finish(req.Finish());

    std::string endpoint = "tcp://" + host + ":" + std::to_string(port);
    spdlog::info("sending request id={} to {}", id, endpoint);

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
