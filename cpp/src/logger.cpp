#include "logger.h"

#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/pattern_formatter.h>

#include <memory>
#include <vector>
#include <chrono>
#include <ctime>
#include <cstdio>
#include <cstring>

#if defined(__linux__)
#  include <sched.h>
#endif

// ── custom flag: UTC timestamp (%*) ─────────────────────────────────────────
// Formats msg.time as "YYYY-MM-DD HH:MM:SS.mmm UTC"

class utc_time_formatter final : public spdlog::custom_flag_formatter {
public:
    void format(const spdlog::details::log_msg& msg,
                const std::tm&,
                spdlog::memory_buf_t& dest) override {
        using namespace std::chrono;
        auto total_ms = duration_cast<milliseconds>(msg.time.time_since_epoch()).count();
        std::time_t t = static_cast<std::time_t>(total_ms / 1000);
        long long   ms = total_ms % 1000;

        std::tm utc{};
#ifdef _WIN32
        gmtime_s(&utc, &t);
#else
        gmtime_r(&t, &utc);
#endif
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d.%03lld UTC",
            utc.tm_year + 1900, utc.tm_mon + 1, utc.tm_mday,
            utc.tm_hour, utc.tm_min, utc.tm_sec, ms);
        dest.append(buf, buf + std::strlen(buf));
    }

    std::unique_ptr<custom_flag_formatter> clone() const override {
        return std::make_unique<utc_time_formatter>();
    }
};

// ── custom flag: processor/CPU ID (%q) ───────────────────────────────────────
// Returns the logical CPU the calling thread is currently running on.
//   Linux  — sched_getcpu()
//   macOS  — no direct equivalent; emits -1 (pinning is advisory via affinity tags)

class cpu_id_formatter final : public spdlog::custom_flag_formatter {
public:
    void format(const spdlog::details::log_msg&,
                const std::tm&,
                spdlog::memory_buf_t& dest) override {
        int cpu = -1;
#if defined(__linux__)
        cpu = sched_getcpu();
#endif
        auto s = std::to_string(cpu);
        dest.append(s.data(), s.data() + s.size());
    }

    std::unique_ptr<custom_flag_formatter> clone() const override {
        return std::make_unique<cpu_id_formatter>();
    }
};

// ── formatter factory ────────────────────────────────────────────────────────
// Each sink needs its own formatter instance; always register both custom
// flags so the same factory works regardless of which flags are in the pattern.

static std::unique_ptr<spdlog::pattern_formatter>
make_formatter(const std::string& pattern) {
    auto f = std::make_unique<spdlog::pattern_formatter>();
    f->add_flag<utc_time_formatter>('*');
    f->add_flag<cpu_id_formatter>('q');
    f->set_pattern(pattern);
    return f;
}

// ── setup_logger ─────────────────────────────────────────────────────────────

void setup_logger(const std::string& log_file,
                  bool utc,
                  bool thread_id,
                  bool cpu_id) {
    // Build pattern from enabled fields
    std::string ts  = utc ? "%*" : "%Y-%m-%d %H:%M:%S.%e";
    std::string pat = "[" + ts + "] [pid:%P]";
    if (thread_id) pat += " [tid:%t]";
    if (cpu_id)    pat += " [cpu:%q]";
    pat += " [%^%l%$] %v";

    std::vector<spdlog::sink_ptr> sinks;

    // Stdout — info and above
    auto console = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    console->set_level(spdlog::level::info);
    console->set_formatter(make_formatter(pat));
    sinks.push_back(console);

    // Rotating file — debug and above, 10 MB × 5 rotations
    if (!log_file.empty()) {
        auto file = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
            log_file, 10 * 1024 * 1024, 5);
        file->set_level(spdlog::level::debug);
        file->set_formatter(make_formatter(pat));
        sinks.push_back(file);
    }

    auto logger = std::make_shared<spdlog::logger>("range_pricer", sinks.begin(), sinks.end());
    logger->set_level(spdlog::level::debug);
    logger->flush_on(spdlog::level::warn);

    spdlog::set_default_logger(logger);
    spdlog::flush_every(std::chrono::seconds(1));
}
