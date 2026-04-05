import struct
import flatbuffers
import zmq

import RangePricer.PricingParams as PP
import RangePricer.RangePricingRequest as RRQ
import RangePricer.RangePricingResponse as RRP
import RangePricer.AlphaBetaPair as ABP


def _hash_combine(seed, v):
    """Boost-style hash_combine matching the C++ client."""
    h = struct.unpack('I', struct.pack('f', v))[0]
    return (seed ^ (h + 0x9e3779b9 + (seed << 6) + (seed >> 2))) & 0xFFFFFFFFFFFFFFFF


def compute_request_hash(stock_price, strike_price, interest_rate,
                         time_to_maturity, stock_discretization):
    h = 0
    for v in [stock_price, strike_price, interest_rate,
              time_to_maturity, stock_discretization]:
        h = _hash_combine(h, v)
    return h


def grid_values(lo, hi, step):
    if step <= 0:
        return [lo]
    vals = []
    x = lo
    while x <= hi + 1e-6:
        vals.append(x)
        x += step
    return vals


def build_request(option_type, stock_price, strike_price, interest_rate,
                  time_to_maturity, stock_discretization,
                  alpha_min, alpha_max, alpha_step,
                  beta_min, beta_max, beta_step):
    """Build a RangePricingRequest FlatBuffer, return (bytes, request_hash, n_pairs)."""
    builder = flatbuffers.Builder(512)

    # Generate alpha x beta grid
    alphas = grid_values(alpha_min, alpha_max, alpha_step)
    betas  = grid_values(beta_min,  beta_max,  beta_step)

    pairs = []
    idx = 0
    for a in alphas:
        for b in betas:
            pairs.append((idx, a, b))
            idx += 1

    # Create pairs vector (must be built in reverse)
    RRQ.StartPairsVector(builder, len(pairs))
    for i in reversed(range(len(pairs))):
        ABP.CreateAlphaBetaPair(builder, pairs[i][0], pairs[i][1], pairs[i][2])
    pairs_vec = builder.EndVector()

    # Create PricingParams
    PP.Start(builder)
    PP.AddOptionType(builder, option_type)
    PP.AddStockPrice(builder, stock_price)
    PP.AddStrikePrice(builder, strike_price)
    PP.AddInterestRate(builder, interest_rate)
    PP.AddTimeToMaturity(builder, time_to_maturity)
    PP.AddStockDiscretization(builder, stock_discretization)
    params = PP.End(builder)

    # Compute hash
    request_hash = compute_request_hash(
        stock_price, strike_price, interest_rate,
        time_to_maturity, stock_discretization)

    # Create RangePricingRequest
    RRQ.Start(builder)
    RRQ.AddRequest(builder, params)
    RRQ.AddRequestHash(builder, request_hash)
    RRQ.AddPairs(builder, pairs_vec)
    root = RRQ.End(builder)
    builder.Finish(root)

    return bytes(builder.Output()), request_hash, len(pairs)


def send_request(endpoint, buf):
    """Send a RangePricingRequest and return the raw response bytes."""
    ctx = zmq.Context()
    sock = ctx.socket(zmq.REQ)
    sock.connect(endpoint)
    sock.send(buf)
    reply = sock.recv()
    sock.close()
    ctx.term()
    return reply


def read_response(buf):
    """Deserialise bytes into a RangePricingResponse, return (request_hash, results)."""
    resp = RRP.RangePricingResponse.GetRootAs(buf, 0)
    results = []
    for i in range(resp.ResultsLength()):
        r = resp.Results(i)
        results.append({
            'idx': r.Idx(),
            'alpha': r.Alpha(),
            'beta': r.Beta(),
            'price': r.Price(),
            'hedge_ratio': r.HedgeRatio(),
        })
    return resp.RequestHash(), results
