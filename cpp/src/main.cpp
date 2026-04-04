#include "config.h"
#include "logger.h"
#include "range_pricer.h"
#include "generated/pricing_request_generated.h"

#include <zmq.hpp>
#include <flatbuffers/verifier.h>
#include <spdlog/spdlog.h>

#include <string>
#include <cstring>
#include <thread>
#include <vector>
#include <csignal>
#include <unistd.h>
#include <sys/wait.h>

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

// ── worker thread ────────────────────────────────────────────────────────────

static void worker_thread(zmq::context_t& ctx, const std::string& backend, int thread_index) {
    set_thread_affinity(thread_index);

    zmq::socket_t sock(ctx, zmq::socket_type::rep);
    sock.connect(backend);
    spdlog::debug("worker thread {} connected to {}", thread_index, backend);

    while (true) {
        zmq::message_t msg;
        auto result = sock.recv(msg, zmq::recv_flags::none);
        if (!result) continue;

        const auto* data = static_cast<const uint8_t*>(msg.data());
        const size_t size = msg.size();

        flatbuffers::Verifier verifier(data, size);
        if (!RangePricer::VerifyRangePricingRequestBuffer(verifier)) {
            const char* err = "ERROR: invalid FlatBuffer";
            spdlog::warn("thread {}: received invalid FlatBuffer ({} bytes)", thread_index, size);
            sock.send(zmq::buffer(err, std::strlen(err)), zmq::send_flags::none);
            continue;
        }

        const auto* range_req = RangePricer::GetRangePricingRequest(data);
        const auto* req       = range_req->request();

        double pv = range_pricer::price(*req);

        spdlog::info("hash={} id={} spot={:.4f} range=[{:.4f},{:.4f}]"
                     " alpha={:.4f} beta={:.4f}"
                     " alpha_bounds=[{:.4f},{:.4f}] step={:.4f}"
                     " beta_bounds=[{:.4f},{:.4f}] step={:.4f}"
                     " pv={:.6f}",
                     range_req->request_hash(),
                     req->request_id()->c_str(),
                     req->spot(), req->low(), req->high(),
                     req->alpha(), req->beta(),
                     range_req->alpha_min(), range_req->alpha_max(), range_req->alpha_step(),
                     range_req->beta_min(),  range_req->beta_max(),  range_req->beta_step(),
                     pv);

        std::string reply =
            "hash="    + std::to_string(range_req->request_hash()) +
            " id="     + std::string(req->request_id()->c_str()) +
            " spot="   + std::to_string(req->spot()) +
            " range=[" + std::to_string(req->low()) + "," + std::to_string(req->high()) + "]" +
            " pv="     + std::to_string(pv);

        sock.send(zmq::buffer(reply), zmq::send_flags::none);
    }
}

// ── worker process ───────────────────────────────────────────────────────────

static void run_worker_process(const Config& cfg) {
    setup_logger(cfg.log_file, cfg.log_utc, cfg.log_tid, cfg.log_cpu);

    zmq::context_t ctx(1);

    std::vector<std::thread> threads;
    threads.reserve(cfg.threads);
    for (int i = 0; i < cfg.threads; ++i)
        threads.emplace_back(worker_thread, std::ref(ctx), cfg.backend, i);

    spdlog::info("worker process pid={} threads={} backend={}", getpid(), cfg.threads, cfg.backend);

    for (auto& t : threads) t.join();
}

// ── broker (parent) ──────────────────────────────────────────────────────────

static void run_broker(const Config& cfg) {
    zmq::context_t ctx(1);

    zmq::socket_t frontend(ctx, zmq::socket_type::router);
    zmq::socket_t backend (ctx, zmq::socket_type::dealer);

    frontend.bind(cfg.frontend);
    backend .bind(cfg.backend);

    spdlog::info("broker pid={} frontend={} backend={} processes={} threads={}",
                 getpid(), cfg.frontend, cfg.backend, cfg.processes, cfg.threads);

    zmq::proxy(frontend, backend);
}

// ── signal handling ──────────────────────────────────────────────────────────

static volatile sig_atomic_t g_stop = 0;
static void on_signal(int sig) {
    g_stop = 1;
    spdlog::warn("caught signal {}, shutting down", sig);
}

// ── main ─────────────────────────────────────────────────────────────────────

int main(int argc, char* argv[]) {
    // Bootstrap logger (console-only) so config errors are visible
    setup_logger("");

    Config cfg;
    if (!parse_args(argc, argv, cfg)) return 1;

    // Reinitialise with full settings now that config is loaded
    setup_logger(cfg.log_file, cfg.log_utc, cfg.log_tid, cfg.log_cpu);

    signal(SIGINT,  on_signal);
    signal(SIGTERM, on_signal);
    signal(SIGCHLD, SIG_IGN);

    std::vector<pid_t> children;
    for (int i = 0; i < cfg.processes; ++i) {
        pid_t pid = fork();
        if (pid < 0) { spdlog::critical("fork failed: {}", strerror(errno)); return 1; }
        if (pid == 0) {
            signal(SIGINT,  SIG_DFL);
            signal(SIGTERM, SIG_DFL);
            run_worker_process(cfg);
            return 0;
        }
        children.push_back(pid);
        spdlog::debug("forked worker pid={}", pid);
    }

    run_broker(cfg);

    for (pid_t pid : children) {
        kill(pid, SIGTERM);
        waitpid(pid, nullptr, 0);
        spdlog::debug("reaped worker pid={}", pid);
    }

    spdlog::info("shutdown complete");
    spdlog::shutdown();
    return 0;
}
