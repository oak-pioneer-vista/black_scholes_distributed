#pragma once

struct Config;

// Runs the server: binds REP on cfg.frontend, spawns N worker threads with
// CPU affinity, dispatches BatchPricingRequests via inproc PUSH/PULL, collects
// results into RangePricingResponse, and replies. Blocks until SIGINT/SIGTERM.
void run_server(const Config& cfg);
