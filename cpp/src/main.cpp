#include "config.h"
#include "logger.h"
#include "worker.h"

#include <gflags/gflags.h>
#include <spdlog/spdlog.h>

#include <csignal>
#include <unistd.h>

int main(int argc, char* argv[]) {
    setup_logger("");

    gflags::SetUsageMessage("Range pricer server — multi-threaded ZMQ server");
    gflags::ParseCommandLineFlags(&argc, &argv, true);

    Config cfg = config_from_flags();

    setup_logger(cfg.log_file, cfg.log_utc, cfg.log_tid, cfg.log_cpu);
    spdlog::flush_every(std::chrono::seconds(1));

    // Block SIGINT/SIGTERM so the monitor thread can handle them via sigwait.
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGINT);
    sigaddset(&mask, SIGTERM);
    pthread_sigmask(SIG_BLOCK, &mask, nullptr);

    run_server(cfg);

    spdlog::info("shutdown complete");
    spdlog::shutdown();
    return 0;
}
