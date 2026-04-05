#include "config.h"

#include <gflags/gflags.h>

DEFINE_int32 (threads,   4,    "Number of worker threads");
DEFINE_int32 (simd_lanes,4,    "SIMD lanes per thread");
DEFINE_string(frontend,  "tcp://*:5555", "ZMQ frontend bind endpoint");
DEFINE_string(log_file,  "range_pricer.log", "Log file path (empty for console only)");
DEFINE_bool  (log_utc,   true,  "Use UTC timestamps (false = local time)");
DEFINE_bool  (log_tid,   true,  "Include thread ID in log lines");
DEFINE_bool  (log_cpu,   true,  "Include processor/CPU ID in log lines");

static bool validate_threads(const char*, int32_t val) { return val >= 1; }
DEFINE_validator(threads, &validate_threads);

Config config_from_flags() {
    Config cfg;
    cfg.threads    = FLAGS_threads;
    cfg.simd_lanes = FLAGS_simd_lanes;
    cfg.frontend   = FLAGS_frontend;
    cfg.log_file   = FLAGS_log_file;
    cfg.log_utc    = FLAGS_log_utc;
    cfg.log_tid    = FLAGS_log_tid;
    cfg.log_cpu    = FLAGS_log_cpu;
    return cfg;
}
