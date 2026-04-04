#pragma once

#include <string>

// Initialise the global spdlog logger with a stdout colour sink and an
// optional rotating file sink.
//
//   log_file  — path for rotating file sink; empty disables file logging
//   utc       — use UTC timestamps (false = local time)
//   thread_id — include OS thread ID in every log line
//   cpu_id    — include processor/CPU ID in every log line
void setup_logger(const std::string& log_file,
                  bool utc       = true,
                  bool thread_id = true,
                  bool cpu_id    = true);
