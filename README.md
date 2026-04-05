# Range Pricer

High-performance option pricer with SIMD-vectorized Black-Scholes pricing, a multi-threaded C++ server, ZeroMQ transport, and FlatBuffers serialization.

The server prices an alpha x beta parameter grid for a given set of pricing parameters (stock price, strike, rate, time to maturity) using [Google Highway](https://github.com/google/highway) for portable SIMD vectorization. The pricing model is a Black-Scholes stub — the vectorized infrastructure is in place and the model implementation is pending.

## Architecture

```
Client (REQ)
    │
    ▼
┌──────────────────────────────────┐
│  Server (REP)        pid=main    │
│                                  │
│  RangePricingRequest             │
│  ──► slice into BatchPricingReqs │
│      (simd_lanes pairs each)     │
│                                  │
│  PUSH ──► Thread 0 ──► PULL      │
│  PUSH ──► Thread 1 ──► PULL      │
│  ...      (pinned to CPU)        │
│                                  │
│  Collect + sort by idx           │
│  ──► RangePricingResponse        │
└──────────────────────────────────┘
```

- **Server**: Single process, binds REP on frontend. Receives `RangePricingRequest`, slices the `[AlphaBetaPair]` into chunks of `simd_lanes`, dispatches to worker threads via `inproc://` PUSH/PULL.
- **Worker threads** (`N`, default 4): Pinned to CPU, each prices a batch using SIMD-vectorized `process()`, returns serialized `BatchPricingResponse`.
- **SIMD**: Pricing uses Highway's `HWY_DYNAMIC_DISPATCH` for runtime target selection (NEON on ARM, AVX/SSE on x86). Pairs are processed in native SIMD-width chunks.
- **Signal handling**: `SIGINT`/`SIGTERM` handled via `sigwait` in a monitor thread for clean shutdown.

## Building

### Prerequisites

- C++17 compiler (Clang or GCC)
- [ZeroMQ](https://zeromq.org/) (`libzmq`)
- [Google Highway](https://github.com/google/highway) (`libhwy`)
- [gflags](https://github.com/gflags/gflags)
- [FlatBuffers](https://flatbuffers.dev/) compiler (`flatc`, for schema changes only)
- CMake >= 3.20

On macOS:
```bash
brew install zeromq highway gflags flatbuffers cmake
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
| `--threads` | `4` | Worker threads |
| `--simd_lanes` | `4` | Max alpha-beta pairs per batch chunk |
| `--frontend` | `tcp://*:5555` | Client-facing endpoint |
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
| `--stock_price` | `100.0` | Stock price |
| `--strike_price` | `100.0` | Strike price |
| `--interest_rate` | `0.05` | Interest rate |
| `--time_to_maturity` | `1.0` | Time to maturity (years) |
| `--stock_discretization` | `0.01` | Stock discretization |
| `--alpha_min` | `0.0` | Alpha grid lower bound |
| `--alpha_max` | `1.0` | Alpha grid upper bound |
| `--alpha_step` | `0.1` | Alpha grid step |
| `--beta_min` | `0.0` | Beta grid lower bound |
| `--beta_max` | `1.0` | Beta grid upper bound |
| `--beta_step` | `0.1` | Beta grid step |
| `--host` | `localhost` | Server host |
| `--port` | `5555` | Server port |

### Example

```bash
# Terminal 1: start the server
./range_pricer_server --threads 4 --simd_lanes 4

# Terminal 2: price an 11x11 alpha-beta grid
./range_pricer_client \
  --stock_price 100 --strike_price 100 --interest_rate 0.05 --time_to_maturity 1.0 \
  --stock_discretization 0.01 \
  --alpha_min 0.0 --alpha_max 1.0 --alpha_step 0.1 \
  --beta_min 0.0 --beta_max 1.0 --beta_step 0.1
```

## Pricing Model

The pricing engine uses SIMD-vectorized Black-Scholes via Google Highway. The vectorized kernel (`vectorized_price`) processes alpha-beta pairs in native SIMD-width lanes, with Highway selecting the best instruction set at runtime.

The current implementation is a stub that computes `price = exp(-r * T)` and `hedge_ratio = 0` for each pair. The full Black-Scholes model (with CDF, PDF, and per-pair alpha/beta dependence) is pending implementation within the same vectorized framework.

### API

Two entry points share the same vectorized kernel:

- **`price()`** -- plain C++ interface. Takes `OptionType`, scalar params, and `vector<PricingInput>` (idx, alpha, beta). Returns `vector<PricingOutput>` (idx, alpha, beta, price, hedge_ratio).
- **`process()`** -- FlatBuffer interface used by worker threads. Takes `PricingParams`, `[AlphaBetaPair]`, `batch_id`. Returns serialized `BatchPricingResponse`.

## FlatBuffers Schema

The wire protocol is defined in `schemas/pricing_request.fbs`:

```
OptionType            Call | Put (enum)
PricingParams         option_type, stock_price, strike_price, interest_rate,
                      time_to_maturity, stock_discretization
AlphaBetaPair         idx, alpha, beta (struct)
RangePricingRequest   request (PricingParams), request_hash, [AlphaBetaPair]
BatchPricingRequest   batch_counter_id, request (PricingParams), [AlphaBetaPair]
PricingResponse       idx, alpha, beta, price, hedge_ratio
BatchPricingResponse  batch_id, [PricingResponse]
RangePricingResponse  request_hash, [PricingResponse]
```

Regenerate bindings after schema changes (or just `make` -- CMake auto-runs `flatc`):

```bash
flatc --cpp -o cpp/include/generated schemas/pricing_request.fbs
flatc --python -o python/generated schemas/pricing_request.fbs
```

## Python

A Python client module and Jupyter notebook are available:

```bash
cd python
pip install -r requirements.txt
jupyter notebook range_pricer.ipynb
```

The notebook imports `range_pricer_client.py` which provides `build_request()`, `send_request()`, and `read_response()` for building FlatBuffers, communicating with the server over ZMQ, and deserializing results.

## Project Structure

```
range_pricer/
├── schemas/
│   └── pricing_request.fbs          # FlatBuffers schema
├── cpp/
│   ├── CMakeLists.txt
│   ├── include/
│   │   ├── range_pricer.h            # Pricing library interface
│   │   ├── worker.h                  # Server entry point
│   │   ├── config.h                  # Config struct + gflags
│   │   ├── logger.h                  # spdlog setup
│   │   ├── generated/                # FlatBuffers generated headers (auto)
│   │   ├── flatbuffers/              # Vendored (v25.12.19)
│   │   ├── spdlog/                   # Vendored (v1.17.0)
│   │   └── zmq.hpp                   # Vendored cppzmq (v4.10.0)
│   └── src/
│       ├── main.cpp                  # Server entry: parse flags, run server
│       ├── worker.cpp                # Server loop + thread pool dispatch
│       ├── range_pricer.cpp          # SIMD-vectorized pricing (Highway)
│       ├── client.cpp                # CLI client
│       ├── config.cpp                # gflags definitions
│       └── logger.cpp                # spdlog custom formatters
├── python/
│   ├── range_pricer_client.py        # Client module (build/send/read)
│   ├── range_pricer.ipynb            # Notebook
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
| Google Highway | 1.3+ | System |
| gflags | 2.3+ | System |
