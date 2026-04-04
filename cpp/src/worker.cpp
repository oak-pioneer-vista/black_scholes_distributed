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
#include <vector>
#include <cmath>
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
// Expand a RangePricingRequest into one BatchPricingRequest FlatBuffer per
// thread by walking the alpha × beta grid and slicing into equal chunks.

static std::vector<std::vector<uint8_t>>
expand_to_batches(const RangePricer::RangePricingRequest* rr, int n_threads) {
    const auto* base = rr->request();

    auto grid_values = [](double lo, double hi, float step) {
        std::vector<double> v;
        if (step <= 0.f) { v.push_back(lo); return v; }
        for (double x = lo; x <= hi + 1e-9; x += step)
            v.push_back(x);
        return v;
    };

    auto alphas = grid_values(rr->alpha_min(), rr->alpha_max(), rr->alpha_step());
    auto betas  = grid_values(rr->beta_min(),  rr->beta_max(),  rr->beta_step());

    struct Point { double alpha, beta; };
    std::vector<Point> grid;
    grid.reserve(alphas.size() * betas.size());
    for (double a : alphas)
        for (double b : betas)
            grid.push_back({a, b});

    if (grid.empty()) grid.push_back({base->alpha(), base->beta()});

    int total    = static_cast<int>(grid.size());
    int chunks   = std::min(n_threads, total);
    int chunk_sz = (total + chunks - 1) / chunks;

    std::vector<std::vector<uint8_t>> batches;
    batches.reserve(chunks);

    for (int c = 0; c < chunks; ++c) {
        int start = c * chunk_sz;
        int end   = std::min(start + chunk_sz, total);

        flatbuffers::FlatBufferBuilder builder(512);

        std::vector<flatbuffers::Offset<RangePricer::PricingRequest>> reqs;
        reqs.reserve(end - start);

        for (int i = start; i < end; ++i) {
            auto rid = builder.CreateString(base->request_id()->c_str());
            RangePricer::PricingRequestBuilder pr(builder);
            pr.add_request_id(rid);
            pr.add_spot(base->spot());
            pr.add_low(base->low());
            pr.add_high(base->high());
            pr.add_vol(base->vol());
            pr.add_rate(base->rate());
            pr.add_expiry(base->expiry());
            pr.add_alpha(grid[i].alpha);
            pr.add_beta(grid[i].beta);
            reqs.push_back(pr.Finish());
        }

        auto requests_vec = builder.CreateVector(reqs);
        RangePricer::BatchPricingRequestBuilder br(builder);
        br.add_batch_counter_id(rr->request_hash() * 1000 + c);
        br.add_requests(requests_vec);
        builder.Finish(br.Finish());

        auto* buf = builder.GetBufferPointer();
        batches.emplace_back(buf, buf + builder.GetSize());
    }

    return batches;
}

// ── worker thread ─────────────────────────────────────────────────────────────
// Pulls BatchPricingRequest from inproc://dispatch, prices all requests,
// pushes a result string to inproc://results.

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
            if (!batch->Verify(verifier) || !batch->requests()) {
                spdlog::warn("thread {}: invalid BatchPricingRequest, skipping", thread_index);
                push.send(zmq::buffer(std::string{}), zmq::send_flags::none);
                continue;
            }

            auto pvs = range_pricer::price_batch(*batch);

            std::string results;
            results.reserve(128 * pvs.size());

            for (const auto& r : pvs) {
                results += "id="    + r.request_id +
                           " alpha=" + std::to_string(r.alpha) +
                           " beta="  + std::to_string(r.beta) +
                           " pv="    + std::to_string(r.pv) + "\n";
                spdlog::info("thread={} batch={} id={} alpha={:.4f} beta={:.4f} pv={:.6f}",
                             thread_index, batch->batch_counter_id(),
                             r.request_id, r.alpha, r.beta, r.pv);
            }

            push.send(zmq::buffer(results), zmq::send_flags::none);
        }
    } catch (const zmq::error_t& e) {
        if (e.num() == ETERM)
            spdlog::debug("worker thread {}: context terminated, exiting", thread_index);
        else
            spdlog::error("worker thread {}: {}", thread_index, e.what());
    }
}

// ── shutdown monitor thread ───────────────────────────────────────────────────

static void shutdown_monitor(zmq::context_t& ctx) {
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGTERM);
    sigaddset(&mask, SIGINT);
    int sig = 0;
    sigwait(&mask, &sig);
    spdlog::info("worker pid={}: signal {}, shutting down", getpid(), sig);
    ctx.shutdown();
}

// ── worker process (public) ──────────────────────────────────────────────────

void run_worker_process(const Config& cfg) {
    setup_logger(cfg.log_file, cfg.log_utc, cfg.log_tid, cfg.log_cpu);
    spdlog::flush_every(std::chrono::seconds(1));

    // SIGINT/SIGTERM already blocked (inherited from parent before fork).
    // shutdown_monitor handles them via sigwait.

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

    spdlog::info("worker process pid={} threads={} backend={}",
                 getpid(), cfg.threads, cfg.backend);

    try {
        zmq::socket_t broker(ctx, zmq::socket_type::rep);
        broker.connect(cfg.backend);

        while (true) {
            zmq::message_t msg;
            auto r = broker.recv(msg, zmq::recv_flags::none);
            if (!r) continue;

            const auto* data = static_cast<const uint8_t*>(msg.data());
            const size_t size = msg.size();

            flatbuffers::Verifier verifier(data, size);
            if (!RangePricer::VerifyRangePricingRequestBuffer(verifier)) {
                spdlog::warn("process {}: invalid RangePricingRequest ({} bytes)", getpid(), size);
                const char* err = "ERROR: invalid RangePricingRequest";
                broker.send(zmq::buffer(err, std::strlen(err)), zmq::send_flags::none);
                continue;
            }

            const auto* rr = RangePricer::GetRangePricingRequest(data);
            spdlog::info("process={} hash={} id={} alpha=[{:.3f},{:.3f}] beta=[{:.3f},{:.3f}]",
                         getpid(), rr->request_hash(),
                         rr->request()->request_id()->c_str(),
                         rr->alpha_min(), rr->alpha_max(),
                         rr->beta_min(),  rr->beta_max());

            auto batches = expand_to_batches(rr, cfg.threads);

            for (auto& batch_buf : batches)
                dispatch.send(zmq::buffer(batch_buf), zmq::send_flags::none);

            std::string reply;
            for (size_t i = 0; i < batches.size(); ++i) {
                zmq::message_t result_msg;
                auto res = results.recv(result_msg, zmq::recv_flags::none);
                if (!res) continue;
                reply.append(static_cast<char*>(result_msg.data()), result_msg.size());
            }

            broker.send(zmq::buffer(reply), zmq::send_flags::none);
        }
    } catch (const zmq::error_t& e) {
        if (e.num() != ETERM)
            spdlog::error("process {}: zmq error: {}", getpid(), e.what());
    }

    for (auto& t : threads) t.join();
    if (monitor.joinable()) monitor.join();
    spdlog::debug("worker pid={}: all threads joined", getpid());
}
