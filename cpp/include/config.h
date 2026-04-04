#pragma once

#include <string>

struct Config {
    int         processes  = 2;
    int         threads    = 4;
    std::string frontend   = "tcp://*:5555";
    std::string backend    = "ipc:///tmp/range_pricer_backend";

    // logging
    std::string log_file   = "range_pricer.log";
    bool        log_utc    = true;   // UTC timestamps (false = local time)
    bool        log_tid    = true;   // include thread ID
    bool        log_cpu    = true;   // include processor/CPU ID
};

// Parse a key=value config file into cfg (ignores unknown keys).
void parse_config_file(const std::string& path, Config& cfg);

// Apply command-line overrides on top of cfg.
// Returns false and prints usage if args are invalid.
bool parse_args(int argc, char* argv[], Config& cfg);

void print_usage(const char* prog);
