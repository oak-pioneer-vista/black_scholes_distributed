#pragma once

#include <string>

struct Config {
    int         threads    = 4;
    int         simd_lanes = 4;
    std::string frontend   = "tcp://*:5555";

    // logging
    std::string log_file   = "range_pricer.log";
    bool        log_utc    = true;   // UTC timestamps (false = local time)
    bool        log_tid    = true;   // include thread ID
    bool        log_cpu    = true;   // include processor/CPU ID
};

// Populate cfg from gflags FLAGS_* values (call after gflags::ParseCommandLineFlags).
Config config_from_flags();
