#include "range_pricer.h"
#include "generated/pricing_request_generated.h"

#include <zmq.hpp>
#include <flatbuffers/verifier.h>

#include <iostream>
#include <string>
#include <cstring>

static const char* DEFAULT_ENDPOINT = "tcp://*:5555";

int main(int argc, char* argv[]) {
    const char* endpoint = (argc > 1) ? argv[1] : DEFAULT_ENDPOINT;

    zmq::context_t ctx{1};
    zmq::socket_t  sock{ctx, zmq::socket_type::rep};
    sock.bind(endpoint);

    std::cout << "range_pricer server listening on " << endpoint << "\n";

    while (true) {
        zmq::message_t msg;
        auto result = sock.recv(msg, zmq::recv_flags::none);
        if (!result) continue;

        const uint8_t* data = static_cast<const uint8_t*>(msg.data());
        const size_t   size = msg.size();

        // verify before accessing
        flatbuffers::Verifier verifier(data, size);
        if (!RangePricer::VerifyPricingRequestBuffer(verifier)) {
            const char* err = "ERROR: invalid FlatBuffer";
            sock.send(zmq::buffer(err, std::strlen(err)), zmq::send_flags::none);
            std::cerr << err << "\n";
            continue;
        }

        const auto* req = RangePricer::GetPricingRequest(data);

        double pv = range_pricer::price(*req);

        std::string reply =
            "id="     + std::string(req->request_id()->c_str()) +
            " spot="  + std::to_string(req->spot()) +
            " range=[" + std::to_string(req->low()) + "," + std::to_string(req->high()) + "]"
            " pv="    + std::to_string(pv);

        sock.send(zmq::buffer(reply), zmq::send_flags::none);
        std::cout << reply << "\n";
    }
}
