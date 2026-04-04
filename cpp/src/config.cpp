#include "config.h"

#include <spdlog/spdlog.h>

#include <fstream>
#include <iostream>
#include <cstdlib>

static std::string trim(const std::string& s) {
    auto b = s.find_first_not_of(" \t\r\n");
    auto e = s.find_last_not_of(" \t\r\n");
    return (b == std::string::npos) ? "" : s.substr(b, e - b + 1);
}

void parse_config_file(const std::string& path, Config& cfg) {
    std::ifstream f(path);
    if (!f) {
        spdlog::warn("config: cannot open {}", path);
        return;
    }
    std::string line;
    while (std::getline(f, line)) {
        auto comment = line.find('#');
        if (comment != std::string::npos) line = line.substr(0, comment);
        line = trim(line);
        if (line.empty()) continue;

        auto eq = line.find('=');
        if (eq == std::string::npos) continue;

        std::string key = trim(line.substr(0, eq));
        std::string val = trim(line.substr(eq + 1));

        if      (key == "processes") cfg.processes = std::stoi(val);
        else if (key == "threads")   cfg.threads   = std::stoi(val);
        else if (key == "frontend")  cfg.frontend  = val;
        else if (key == "backend")   cfg.backend   = val;
        else if (key == "log_file")  cfg.log_file  = val;
        else if (key == "log_utc")   cfg.log_utc   = (val == "true" || val == "1");
        else if (key == "log_tid")   cfg.log_tid   = (val == "true" || val == "1");
        else if (key == "log_cpu")   cfg.log_cpu   = (val == "true" || val == "1");
        else spdlog::warn("config: unknown key '{}'", key);
    }
}

void print_usage(const char* prog) {
    std::cerr
        << "Usage: " << prog << " [options]\n"
        << "\n"
        << "  --config   <file>      config file path\n"
        << "  -m/--processes <int>   worker processes   (default: 2)\n"
        << "  -n/--threads   <int>   threads per process (default: 4)\n"
        << "  --frontend <endpoint>  ZMQ frontend bind   (default: tcp://*:5555)\n"
        << "  --backend  <endpoint>  ZMQ backend bind    (default: ipc:///tmp/range_pricer_backend)\n"
        << "  --help                 show this message\n";
}

bool parse_args(int argc, char* argv[], Config& cfg) {
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&]() -> const char* {
            if (i + 1 >= argc) {
                spdlog::error("{} requires a value", a);
                std::exit(1);
            }
            return argv[++i];
        };

        if (a == "--help" || a == "-h") {
            print_usage(argv[0]);
            std::exit(0);
        } else if (a == "--config") {
            parse_config_file(next(), cfg);  // load file, then remaining args override
        } else if (a == "-m" || a == "--processes") {
            cfg.processes = std::atoi(next());
        } else if (a == "-n" || a == "--threads") {
            cfg.threads = std::atoi(next());
        } else if (a == "--frontend") {
            cfg.frontend = next();
        } else if (a == "--backend") {
            cfg.backend = next();
        } else {
            spdlog::error("unknown flag: {}", a);
            print_usage(argv[0]);
            return false;
        }
    }

    if (cfg.processes < 1 || cfg.threads < 1) {
        spdlog::error("processes and threads must be >= 1");
        return false;
    }
    return true;
}
