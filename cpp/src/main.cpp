#include "config.h"
#include "logger.h"
#include "worker.h"

#include <zmq.hpp>
#include <spdlog/spdlog.h>

#include <string>
#include <cstring>
#include <thread>
#include <vector>
#include <csignal>
#include <unistd.h>
#include <sys/wait.h>

// ── broker (parent) ───────────────────────────────────────────────────────────
// Runs ROUTER↔DEALER proxy. A monitor thread waits for SIGINT/SIGTERM via
// sigwait and calls ctx.shutdown() to unblock zmq::proxy.

static void run_broker(const Config& cfg) {
    zmq::context_t ctx(1);
    zmq::socket_t frontend(ctx, zmq::socket_type::router);
    zmq::socket_t backend (ctx, zmq::socket_type::dealer);
    frontend.bind(cfg.frontend);
    backend .bind(cfg.backend);

    // Monitor thread: catches signal, shuts down context
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGINT);
    sigaddset(&mask, SIGTERM);
    std::thread monitor([&ctx, mask]() {
        int sig = 0;
        sigwait(&mask, &sig);
        spdlog::warn("broker: caught signal {}, shutting down", sig);
        ctx.shutdown();
    });

    spdlog::info("broker pid={} frontend={} backend={} processes={} threads={}",
                 getpid(), cfg.frontend, cfg.backend, cfg.processes, cfg.threads);

    try {
        zmq::proxy(frontend, backend);
    } catch (const zmq::error_t& e) {
        if (e.num() != ETERM)
            spdlog::error("broker: {}", e.what());
    }

    monitor.join();
}

// ── main ──────────────────────────────────────────────────────────────────────

int main(int argc, char* argv[]) {
    setup_logger("");

    Config cfg;
    if (!parse_args(argc, argv, cfg)) return 1;

    setup_logger(cfg.log_file, cfg.log_utc, cfg.log_tid, cfg.log_cpu);

    // Block SIGINT/SIGTERM before fork so all processes inherit the mask.
    // Each process handles signals via sigwait in its own monitor thread.
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGINT);
    sigaddset(&mask, SIGTERM);
    pthread_sigmask(SIG_BLOCK, &mask, nullptr);

    signal(SIGCHLD, SIG_IGN);

    std::vector<pid_t> children;
    for (int i = 0; i < cfg.processes; ++i) {
        pid_t pid = fork();
        if (pid < 0) { spdlog::critical("fork failed: {}", strerror(errno)); return 1; }
        if (pid == 0) {
            run_worker_process(cfg);
            _exit(0);
        }
        children.push_back(pid);
        spdlog::debug("forked worker pid={}", pid);
    }

    // Safe to start periodic flush now — all forks are done
    spdlog::flush_every(std::chrono::seconds(1));

    run_broker(cfg);  // blocks until signal shuts down broker context

    // Children also received the signal (same process group) but send
    // SIGTERM as a fallback, then wait for them to exit.
    for (pid_t pid : children) {
        kill(pid, SIGTERM);  // harmless ESRCH if already exited
        waitpid(pid, nullptr, 0);
        spdlog::debug("reaped worker pid={}", pid);
    }

    spdlog::info("shutdown complete");
    spdlog::shutdown();
    return 0;
}
