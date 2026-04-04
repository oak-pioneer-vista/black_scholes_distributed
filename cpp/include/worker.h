#pragma once

struct Config;

// Runs a worker process: receives RangePricingRequest from the broker,
// expands the alpha×beta grid into BatchPricingRequests, distributes them
// to N worker threads via inproc PUSH/PULL, collects results, replies.
// Blocks until SIGTERM/SIGINT.
void run_worker_process(const Config& cfg);
