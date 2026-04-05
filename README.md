# Range Pricer

High-performance range option pricer with a multi-process/multi-thread C++ server, ZeroMQ transport, and FlatBuffers serialization.

Given a spot price and barrier range `[low, high]`, the server computes the price and hedge ratio (delta) of a range accrual option across a discretized alpha x beta parameter grid.

## Architecture

```
Client (REQ)
    │
    ▼
┌────────────────────────────────┐
│  Broker (ROUTER ─ proxy ─ DEALER) │
└────────────────────────────────┘
    │  forks M worker processes
    ▼
┌────────────────────────────────┐
│  Worker Process (REP)          │
│                                │
│  RangePricingRequest ──expand──│
│  ──► N BatchPricingRequests    │
│                                │
│  PUSH ──► Thread 0 ──► PULL   │
│  PUSH ──► Thread 1 ──► PULL   │
│  ...      (pinned to CPU)      │
│                                │
│  Merge ──► BatchPricingResponse│
└────────────────────────────────┘
```

- **Broker** (parent process): ROUTER-DEALER proxy, load-balances across worker processes.
- **Worker processes** (`M`, default 2): Each expands the alpha x beta grid from a `RangePricingRequest`, slices it into chunks, and distributes to threads via `inproc://` PUSH/PULL.
- **Worker threads** (`N` per process, default 4): Pin to CPU, price a batch of alpha-beta pairs, return serialized `BatchPricingResponse`.
- **Signal handling**: `SIGINT`/`SIGTERM` blocked before fork; each process uses `sigwait` in a monitor thread for clean shutdown.

## Building

### Prerequisites

- C++17 compiler (Clang or GCC)
- [ZeroMQ](https://zeromq.org/) (`libzmq`)
- [gflags](https://github.com/gflags/gflags)
- [FlatBuffers](https://flatbuffers.dev/) compiler (`flatc`, for schema changes only)
- CMake >= 3.20

On macOS:
```bash
brew install zeromq gflags flatbuffers cmake
```

### Build

```bash
cd cpp
mkdir -p build && cd build
cmake ..
make -j$(nproc)
```

Produces `range_pricer_server` and `range_pricer_client`.

## Usage

### Server

```bash
./range_pricer_server [flags]
```

| Flag | Default | Description |
|------|---------|-------------|
| `--processes` | `2` | Worker processes |
| `--threads` | `4` | Threads per process |
| `--frontend` | `tcp://*:5555` | Client-facing endpoint |
| `--backend` | `ipc:///tmp/range_pricer_backend` | Internal worker endpoint |
| `--log_file` | `range_pricer.log` | Log file (empty = console only) |
| `--log_utc` | `true` | UTC timestamps |
| `--log_tid` | `true` | Include thread ID in logs |
| `--log_cpu` | `true` | Include CPU ID in logs |
| `--flagfile` | | Load flags from file |

### Client

```bash
./range_pricer_client [flags]
```

| Flag | Default | Description |
|------|---------|-------------|
| `--spot` | `100.0` | Spot price |
| `--low` | `95.0` | Lower barrier |
| `--high` | `105.0` | Upper barrier |
| `--vol` | `0.20` | Annualized volatility |
| `--rate` | `0.05` | Risk-free rate |
| `--expiry` | `1.0` | Time to expiry (years) |
| `--alpha` | `0.0` | Alpha parameter |
| `--beta` | `0.0` | Beta parameter |
| `--alpha_min` | `0.0` | Alpha grid lower bound |
| `--alpha_max` | `1.0` | Alpha grid upper bound |
| `--alpha_step` | `0.1` | Alpha grid step |
| `--beta_min` | `0.0` | Beta grid lower bound |
| `--beta_max` | `1.0` | Beta grid upper bound |
| `--beta_step` | `0.1` | Beta grid step |
| `--hash` | `0` | Request hash identifier |
| `--host` | `localhost` | Server host |
| `--port` | `5555` | Server port |

### Example

```bash
# Terminal 1: start the server
./range_pricer_server --processes 2 --threads 4

# Terminal 2: price a 5x5 alpha-beta grid
./range_pricer_client \
  --spot 100 --low 95 --high 105 --vol 0.2 --rate 0.05 --expiry 1.0 \
  --alpha_min 0.0 --alpha_max 1.0 --alpha_step 0.25 \
  --beta_min 0.0 --beta_max 1.0 --beta_step 0.25 \
  --hash 42
```

## Pricing Model

The range option price is computed as:

```
d_low  = (ln(S/L) + (r - 0.5 * vol^2) * T) / (vol * sqrt(T))
d_high = (ln(S/H) + (r - 0.5 * vol^2) * T) / (vol * sqrt(T))

price       = exp(-r*T) * (N(d_low) - N(d_high))
hedge_ratio = exp(-r*T) * (N'(d_low) - N'(d_high)) / (S * vol * sqrt(T))
```

Where `N` is the cumulative standard normal and `N'` is the standard normal PDF.

## FlatBuffers Schema

The wire protocol is defined in `schemas/pricing_request.fbs`:

```
PricingRequest        spot, low, high, vol, rate, expiry, alpha, beta
RangePricingRequest   request, request_hash, alpha/beta bounds + steps
AlphaBetaPair         alpha, beta (struct)
BatchPricingRequest   batch_counter_id, request, [AlphaBetaPair]
PricingResponse       alpha, beta, price, hedge_ratio
BatchPricingResponse  batch_id, [PricingResponse]
```

Regenerate bindings after schema changes:

```bash
flatc --cpp -o cpp/include/generated schemas/pricing_request.fbs
flatc --python -o python/generated schemas/pricing_request.fbs
```

## Python

An interactive Jupyter notebook is available at `python/range_pricer.ipynb` with ipywidgets sliders for spot, barriers, vol, rate, and expiry.

```bash
cd python
pip install -r requirements.txt
jupyter notebook range_pricer.ipynb
```

## Project Structure

```
range_pricer/
├── schemas/
│   └── pricing_request.fbs          # FlatBuffers schema
├── cpp/
│   ├── CMakeLists.txt
│   ├── include/
│   │   ├── range_pricer.h            # Pricing library interface
│   │   ├── worker.h                  # Worker process entry point
│   │   ├── config.h                  # Config struct + gflags
│   │   ├── logger.h                  # spdlog setup
│   │   ├── generated/                # FlatBuffers generated headers
│   │   ├── flatbuffers/              # Vendored (v25.12.19)
│   │   ├── spdlog/                   # Vendored (v1.17.0)
│   │   └── zmq.hpp                   # Vendored cppzmq (v4.10.0)
│   └── src/
│       ├── main.cpp                  # Broker: fork + ROUTER-DEALER proxy
│       ├── worker.cpp                # Worker process + thread pool
│       ├── range_pricer.cpp          # Pricing math
│       ├── client.cpp                # CLI client
│       ├── config.cpp                # gflags definitions
│       └── logger.cpp                # spdlog custom formatters
├── python/
│   ├── range_pricer.ipynb            # Interactive notebook
│   ├── requirements.txt
│   └── generated/                    # FlatBuffers Python bindings
└── range_pricer.conf                 # Example config (use --flagfile)
```

## Dependencies

| Library | Version | Bundling |
|---------|---------|----------|
| FlatBuffers | 25.12.19 | Vendored headers |
| spdlog | 1.17.0 | Vendored headers |
| cppzmq | 4.10.0 | Vendored header |
| ZeroMQ (libzmq) | 4.3+ | System |
| gflags | 2.3+ | System |
