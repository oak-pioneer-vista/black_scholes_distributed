#include "worker.h"
#include "config.h"
#include "logger.h"
#include "range_pricer.h"
#include "generated/pricing_request_generated.h"

#include <zmq.hpp>
#include <flatbuffers/verifier.h>
#include <flatbuffers/flatbuffer_builder.h>
#include <spdlog/spdlog.h>

#include <string>
#include <cstring>
#include <thread>
#include <algorithm>
#include <vector>
#include <csignal>
#include <unistd.h>

#if defined(__APPLE__)
#  include <pthread.h>
#  include <mach/thread_policy.h>
#  include <mach/thread_act.h>
#elif defined(__linux__)
#  include <pthread.h>
#  include <sched.h>
#endif

// ── thread affinity ──────────────────────────────────────────────────────────

static void set_thread_affinity(int cpu_index) {
    int ncpus = static_cast<int>(std::thread::hardware_concurrency());
    if (ncpus == 0) return;
    int cpu = cpu_index % ncpus;
#if defined(__linux__)
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(cpu, &cpuset);
    pthread_setaffinity_np(pthread_self(), sizeof(cpuset), &cpuset);
#elif defined(__APPLE__)
    thread_affinity_policy_data_t policy = { cpu + 1 };
    thread_policy_set(pthread_mach_thread_np(pthread_self()),
                      THREAD_AFFINITY_POLICY,
                      reinterpret_cast<thread_policy_t>(&policy),
                      THREAD_AFFINITY_POLICY_COUNT);
#endif
}

// ── batch expansion ───────────────────────────────────────────────────────────
// Slice a RangePricingRequest's pairs into BatchPricingRequest FlatBuffers,
// each containing at most simd_lanes AlphaBetaPairs.

static std::vector<std::vector<uint8_t>>
expand_to_batches(const RangePricer::RangePricingRequest* rr, int simd_lanes) {
    const auto* base  = rr->request();
    const auto* pairs = rr->pairs();

    int total    = static_cast<int>(pairs->size());
    int chunk_sz = simd_lanes;
    int chunks   = (total + chunk_sz - 1) / chunk_sz;

    std::vector<std::vector<uint8_t>> batches;
    batches.reserve(chunks);

    for (int c = 0; c < chunks; ++c) {
        int start = c * chunk_sz;
        int end   = std::min(start + chunk_sz, total);

        flatbuffers::FlatBufferBuilder builder(512);

        std::vector<RangePricer::AlphaBetaPair> chunk_pairs;
        chunk_pairs.reserve(end - start);
        for (int i = start; i < end; ++i) {
            const auto* p = pairs->Get(i);
            chunk_pairs.push_back({p->idx(), p->alpha(), p->beta()});
        }

        auto pairs_vec = builder.CreateVectorOfStructs(chunk_pairs);

        RangePricer::PricingParamsBuilder pr(builder);
        pr.add_stock_price(base->stock_price());
        pr.add_strike_price(base->strike_price());
        pr.add_interest_rate(base->interest_rate());
        pr.add_time_to_maturity(base->time_to_maturity());
        pr.add_stock_discretization(base->stock_discretization());
        auto request = pr.Finish();

        RangePricer::BatchPricingRequestBuilder br(builder);
        br.add_batch_counter_id(rr->request_hash() * 1000 + c);
        br.add_request(request);
        br.add_pairs(pairs_vec);
        builder.Finish(br.Finish());

        auto* buf = builder.GetBufferPointer();
        batches.emplace_back(buf, buf + builder.GetSize());
    }

    return batches;
}

// ── worker thread ─────────────────────────────────────────────────────────────
// Pulls BatchPricingRequest from inproc://dispatch, prices the batch,
// pushes the serialised BatchPricingResponse to inproc://results.

static void worker_thread(zmq::context_t& ctx, int thread_index) {
    set_thread_affinity(thread_index);
    try {
        zmq::socket_t pull(ctx, zmq::socket_type::pull);
        pull.connect("inproc://dispatch");

        zmq::socket_t push(ctx, zmq::socket_type::push);
        push.connect("inproc://results");

        spdlog::debug("worker thread {} ready", thread_index);

        while (true) {
            zmq::message_t msg;
            auto result = pull.recv(msg, zmq::recv_flags::none);
            if (!result) continue;

            const auto* data = static_cast<const uint8_t*>(msg.data());
            const size_t size = msg.size();

            flatbuffers::Verifier verifier(data, size);
            auto* batch = flatbuffers::GetRoot<RangePricer::BatchPricingRequest>(data);
            if (!batch->Verify(verifier) || !batch->request() || !batch->pairs()) {
                spdlog::warn("thread {}: invalid BatchPricingRequest, skipping", thread_index);
                push.send(zmq::buffer(std::string{}), zmq::send_flags::none);
                continue;
            }

            std::vector<uint8_t> result_buf;
            try {
                result_buf = range_pricer::price_batch(*batch);
            } catch (const std::exception& e) {
                spdlog::error("thread {}: pricing failed: {}", thread_index, e.what());
                push.send(zmq::buffer(std::string{}), zmq::send_flags::none);
                continue;
            }

            const auto* batch_response = flatbuffers::GetRoot<RangePricer::BatchPricingResponse>(result_buf.data());
            if (batch_response->results()) {
                for (const auto* r : *batch_response->results()) {
                    spdlog::info("thread={} batch={} alpha={:.4f} beta={:.4f} price={:.6f} hedge_ratio={:.6f}",
                                 thread_index, batch_response->batch_id(),
                                 r->alpha(), r->beta(), r->price(), r->hedge_ratio());
                }
            }

            push.send(zmq::buffer(result_buf.data(), result_buf.size()), zmq::send_flags::none);
        }
    } catch (const zmq::error_t& e) {
        if (e.num() == ETERM)
            spdlog::debug("worker thread {}: context terminated, exiting", thread_index);
        else
            spdlog::error("worker thread {}: {}", thread_index, e.what());
    }
}

// ── shutdown monitor thread ──────────────────────────────────────────────────

static void shutdown_monitor(zmq::context_t& ctx) {
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGTERM);
    sigaddset(&mask, SIGINT);
    int sig = 0;
    sigwait(&mask, &sig);
    spdlog::info("signal {}, shutting down", sig);
    ctx.shutdown();
}

// ── server (public) ──────────────────────────────────────────────────────────

void run_server(const Config& cfg) {
    zmq::context_t ctx(1);

    zmq::socket_t dispatch(ctx, zmq::socket_type::push);
    dispatch.bind("inproc://dispatch");

    zmq::socket_t results(ctx, zmq::socket_type::pull);
    results.bind("inproc://results");

    std::vector<std::thread> threads;
    threads.reserve(cfg.threads);
    for (int i = 0; i < cfg.threads; ++i)
        threads.emplace_back(worker_thread, std::ref(ctx), i);

    std::thread monitor(shutdown_monitor, std::ref(ctx));

    spdlog::info("server pid={} threads={} frontend={}",
                 getpid(), cfg.threads, cfg.frontend);

    try {
        zmq::socket_t frontend(ctx, zmq::socket_type::rep);
        frontend.bind(cfg.frontend);

        while (true) {
            zmq::message_t msg;
            auto r = frontend.recv(msg, zmq::recv_flags::none);
            if (!r) continue;

            const auto* data = static_cast<const uint8_t*>(msg.data());
            const size_t size = msg.size();

            flatbuffers::Verifier verifier(data, size);
            if (!RangePricer::VerifyRangePricingRequestBuffer(verifier)) {
                spdlog::warn("invalid RangePricingRequest ({} bytes)", size);
                flatbuffers::FlatBufferBuilder err_builder(64);
                err_builder.Finish(RangePricer::CreateRangePricingResponse(err_builder));
                frontend.send(zmq::buffer(err_builder.GetBufferPointer(), err_builder.GetSize()),
                              zmq::send_flags::none);
                continue;
            }

            const auto* rr = RangePricer::GetRangePricingRequest(data);
            spdlog::info("hash={} pairs={}", rr->request_hash(), rr->pairs()->size());

            auto batches = expand_to_batches(rr, cfg.simd_lanes);

            for (auto& batch_buf : batches)
                dispatch.send(zmq::buffer(batch_buf), zmq::send_flags::none);

            // Collect per-thread BatchPricingResponse buffers, sort by idx, flatten
            struct Result { uint32_t idx; float alpha, beta, price, hedge_ratio; };
            std::vector<Result> all_results;

            for (size_t i = 0; i < batches.size(); ++i) {
                zmq::message_t result_msg;
                auto res = results.recv(result_msg, zmq::recv_flags::none);
                if (!res) continue;

                if (result_msg.size() == 0) continue;

                flatbuffers::Verifier v(static_cast<const uint8_t*>(result_msg.data()), result_msg.size());
                const auto* br = flatbuffers::GetRoot<RangePricer::BatchPricingResponse>(result_msg.data());
                if (!br->Verify(v) || !br->results()) continue;

                for (const auto* r : *br->results())
                    all_results.push_back({r->idx(), r->alpha(), r->beta(), r->price(), r->hedge_ratio()});
            }

            std::sort(all_results.begin(), all_results.end(),
                      [](const Result& a, const Result& b) { return a.idx < b.idx; });

            flatbuffers::FlatBufferBuilder reply_builder(1024);
            std::vector<flatbuffers::Offset<RangePricer::PricingResponse>> result_offsets;
            result_offsets.reserve(all_results.size());
            for (const auto& r : all_results) {
                result_offsets.push_back(RangePricer::CreatePricingResponse(
                    reply_builder, r.idx, r.alpha, r.beta, r.price, r.hedge_ratio));
            }

            auto results_vec = reply_builder.CreateVector(result_offsets);
            reply_builder.Finish(RangePricer::CreateRangePricingResponse(
                reply_builder, rr->request_hash(), results_vec));

            frontend.send(zmq::buffer(reply_builder.GetBufferPointer(), reply_builder.GetSize()),
                          zmq::send_flags::none);
        }
    } catch (const zmq::error_t& e) {
        if (e.num() != ETERM)
            spdlog::error("server: zmq error: {}", e.what());
    }

    for (auto& t : threads) t.join();
    if (monitor.joinable()) monitor.join();
    spdlog::debug("all threads joined");
}
