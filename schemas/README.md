# FlatBuffers Schemas

## Prerequisites

Install the FlatBuffers compiler:

```bash
# macOS
brew install flatbuffers

# Ubuntu / Debian
apt-get install flatbuffers-compiler

# From source
git clone https://github.com/google/flatbuffers.git
cd flatbuffers && cmake -B build && cmake --build build && sudo cmake --install build
```

Verify:

```bash
flatc --version
```

---

## Generate C++ headers

```bash
flatc --cpp -o ../cpp/include/generated pricing_request.fbs
```

Output: `cpp/include/generated/pricing_request_generated.h`

Include in your code:

```cpp
#include "generated/pricing_request_generated.h"
```

Add the generated directory to CMake (already covered by the existing `target_include_directories`):

```cmake
target_include_directories(range_pricer_lib PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/include/generated
)
```

---

## Generate Python module

```bash
flatc --python -o ../python/generated pricing_request.fbs
```

Output: `python/generated/RangePricer/` package with one file per table/enum.

Usage:

```python
import flatbuffers
from RangePricer import PricingRequestBatch, PricingRequest, RangeBarrier

builder = flatbuffers.Builder(256)

# barrier
RangeBarrier.Start(builder)
RangeBarrier.AddLow(builder, 95.0)
RangeBarrier.AddHigh(builder, 105.0)
barrier = RangeBarrier.End(builder)

req_id      = builder.CreateString("req-001")
underlying  = builder.CreateString("AAPL")

PricingRequest.Start(builder)
PricingRequest.AddRequestId(builder, req_id)
PricingRequest.AddUnderlyingId(builder, underlying)
PricingRequest.AddSpot(builder, 100.0)
PricingRequest.AddVol(builder, 0.20)
PricingRequest.AddRate(builder, 0.05)
PricingRequest.AddExpiry(builder, 1.0)
PricingRequest.AddNotional(builder, 1.0)
PricingRequest.AddBarrier(builder, barrier)
req = PricingRequest.End(builder)

reqs = builder.CreateNumpyVector(...)  # or manual vector
PricingRequestBatch.Start(builder)
PricingRequestBatch.AddRequests(builder, reqs)
batch = PricingRequestBatch.End(builder)

builder.Finish(batch)
buf = builder.Output()
```

Install the Python runtime:

```bash
pip install flatbuffers
```

---

## Schemas

| File | Root type | Description |
|---|---|---|
| `pricing_request.fbs` | `PricingRequestBatch` | One or more range pricer pricing requests |

---

## Regenerate all targets

```bash
# from the schemas/ directory
flatc --cpp    -o ../cpp/include/generated pricing_request.fbs
flatc --python -o ../python/generated      pricing_request.fbs
```
