#include "range_pricer.h"
#include "generated/pricing_request_generated.h"

#include <flatbuffers/flatbuffer_builder.h>
#include <gtest/gtest.h>

#include <cmath>
#include <vector>

using namespace range_pricer;

// ── price() tests ────────────────────────────────────────────────────────────

TEST(PriceTest, SinglePairReturnsExpMinusRT) {
    std::vector<PricingInput> inputs = {{0, 0.5f, 0.5f}};

    auto outputs = price(
        RangePricer::OptionType_Call,
        100.0f, 100.0f, 0.05f, 1.0f, 0.01f, inputs);

    ASSERT_EQ(outputs.size(), 1u);
    EXPECT_EQ(outputs[0].idx, 0u);
    EXPECT_FLOAT_EQ(outputs[0].alpha, 0.5f);
    EXPECT_FLOAT_EQ(outputs[0].beta, 0.5f);
    EXPECT_NEAR(outputs[0].price, std::exp(-0.05f), 1e-5f);
    EXPECT_FLOAT_EQ(outputs[0].hedge_ratio, 0.0f);
}

TEST(PriceTest, MultiplePairsAllGetSamePrice) {
    std::vector<PricingInput> inputs = {
        {0, 0.0f, 0.0f},
        {1, 0.5f, 0.5f},
        {2, 1.0f, 1.0f},
    };

    auto outputs = price(
        RangePricer::OptionType_Call,
        100.0f, 100.0f, 0.05f, 1.0f, 0.01f, inputs);

    ASSERT_EQ(outputs.size(), 3u);
    float expected_pv = std::exp(-0.05f);
    for (size_t i = 0; i < 3; ++i) {
        EXPECT_EQ(outputs[i].idx, inputs[i].idx);
        EXPECT_FLOAT_EQ(outputs[i].alpha, inputs[i].alpha);
        EXPECT_FLOAT_EQ(outputs[i].beta, inputs[i].beta);
        EXPECT_NEAR(outputs[i].price, expected_pv, 1e-5f);
        EXPECT_FLOAT_EQ(outputs[i].hedge_ratio, 0.0f);
    }
}

TEST(PriceTest, EmptyInputReturnsEmpty) {
    std::vector<PricingInput> inputs;
    auto outputs = price(
        RangePricer::OptionType_Call,
        100.0f, 100.0f, 0.05f, 1.0f, 0.01f, inputs);
    EXPECT_TRUE(outputs.empty());
}

TEST(PriceTest, PutOptionTypeSameAsCall) {
    std::vector<PricingInput> inputs = {{0, 0.5f, 0.5f}};

    auto call = price(RangePricer::OptionType_Call,
                      100.0f, 100.0f, 0.05f, 1.0f, 0.01f, inputs);
    auto put  = price(RangePricer::OptionType_Put,
                      100.0f, 100.0f, 0.05f, 1.0f, 0.01f, inputs);

    // Stub returns same for both — test will diverge when model is implemented
    ASSERT_EQ(call.size(), 1u);
    ASSERT_EQ(put.size(), 1u);
    EXPECT_FLOAT_EQ(call[0].price, put[0].price);
}

TEST(PriceTest, DifferentRatesProduceDifferentPrices) {
    std::vector<PricingInput> inputs = {{0, 0.5f, 0.5f}};

    auto low_rate  = price(RangePricer::OptionType_Call,
                           100.0f, 100.0f, 0.01f, 1.0f, 0.01f, inputs);
    auto high_rate = price(RangePricer::OptionType_Call,
                           100.0f, 100.0f, 0.10f, 1.0f, 0.01f, inputs);

    EXPECT_GT(low_rate[0].price, high_rate[0].price);
}

TEST(PriceTest, LargerTTMProducesLowerPrice) {
    std::vector<PricingInput> inputs = {{0, 0.5f, 0.5f}};

    auto short_ttm = price(RangePricer::OptionType_Call,
                           100.0f, 100.0f, 0.05f, 0.5f, 0.01f, inputs);
    auto long_ttm  = price(RangePricer::OptionType_Call,
                           100.0f, 100.0f, 0.05f, 2.0f, 0.01f, inputs);

    EXPECT_GT(short_ttm[0].price, long_ttm[0].price);
}

TEST(PriceTest, InvalidTTMThrows) {
    std::vector<PricingInput> inputs = {{0, 0.5f, 0.5f}};
    EXPECT_THROW(
        price(RangePricer::OptionType_Call,
              100.0f, 100.0f, 0.05f, 0.0f, 0.01f, inputs),
        std::invalid_argument);
    EXPECT_THROW(
        price(RangePricer::OptionType_Call,
              100.0f, 100.0f, 0.05f, -1.0f, 0.01f, inputs),
        std::invalid_argument);
}

TEST(PriceTest, InvalidStockDiscretizationThrows) {
    std::vector<PricingInput> inputs = {{0, 0.5f, 0.5f}};
    EXPECT_THROW(
        price(RangePricer::OptionType_Call,
              100.0f, 100.0f, 0.05f, 1.0f, 0.0f, inputs),
        std::invalid_argument);
}

TEST(PriceTest, LargeBatchPreservesIndices) {
    const size_t N = 200;
    std::vector<PricingInput> inputs;
    inputs.reserve(N);
    for (size_t i = 0; i < N; ++i)
        inputs.push_back({static_cast<uint32_t>(i),
                          static_cast<float>(i) * 0.005f,
                          static_cast<float>(i) * 0.005f});

    auto outputs = price(
        RangePricer::OptionType_Call,
        100.0f, 100.0f, 0.05f, 1.0f, 0.01f, inputs);

    ASSERT_EQ(outputs.size(), N);
    for (size_t i = 0; i < N; ++i) {
        EXPECT_EQ(outputs[i].idx, i);
        EXPECT_FLOAT_EQ(outputs[i].alpha, inputs[i].alpha);
        EXPECT_FLOAT_EQ(outputs[i].beta, inputs[i].beta);
    }
}

// ── process() tests ──────────────────────────────────────────────────────────

static std::vector<uint8_t> build_batch(
    float S, float K, float r, float T, float ds,
    const std::vector<std::tuple<uint32_t, float, float>>& pairs_in,
    uint64_t batch_id) {

    flatbuffers::FlatBufferBuilder builder(512);

    std::vector<RangePricer::AlphaBetaPair> pairs;
    for (auto& [idx, a, b] : pairs_in)
        pairs.push_back({idx, a, b});

    auto pairs_vec = builder.CreateVectorOfStructs(pairs);

    RangePricer::PricingParamsBuilder pp(builder);
    pp.add_stock_price(S);
    pp.add_strike_price(K);
    pp.add_interest_rate(r);
    pp.add_time_to_maturity(T);
    pp.add_stock_discretization(ds);
    auto params = pp.Finish();

    RangePricer::BatchPricingRequestBuilder br(builder);
    br.add_batch_counter_id(batch_id);
    br.add_request(params);
    br.add_pairs(pairs_vec);
    builder.Finish(br.Finish());

    auto* buf = builder.GetBufferPointer();
    return {buf, buf + builder.GetSize()};
}

TEST(ProcessTest, ReturnsBatchPricingResponse) {
    auto buf = build_batch(100.0f, 100.0f, 0.05f, 1.0f, 0.01f,
                           {{0, 0.1f, 0.2f}, {1, 0.3f, 0.4f}}, 42);

    const auto* batch = flatbuffers::GetRoot<RangePricer::BatchPricingRequest>(buf.data());
    auto result = process(*batch->request(), *batch->pairs(), batch->batch_counter_id());

    const auto* resp = flatbuffers::GetRoot<RangePricer::BatchPricingResponse>(result.data());
    ASSERT_NE(resp, nullptr);
    EXPECT_EQ(resp->batch_id(), 42u);
    ASSERT_NE(resp->results(), nullptr);
    ASSERT_EQ(resp->results()->size(), 2u);

    float expected_pv = std::exp(-0.05f);
    auto* r0 = resp->results()->Get(0);
    EXPECT_EQ(r0->idx(), 0u);
    EXPECT_FLOAT_EQ(r0->alpha(), 0.1f);
    EXPECT_FLOAT_EQ(r0->beta(), 0.2f);
    EXPECT_NEAR(r0->price(), expected_pv, 1e-5f);

    auto* r1 = resp->results()->Get(1);
    EXPECT_EQ(r1->idx(), 1u);
    EXPECT_FLOAT_EQ(r1->alpha(), 0.3f);
    EXPECT_FLOAT_EQ(r1->beta(), 0.4f);
}

TEST(ProcessTest, InvalidParamsThrows) {
    auto buf = build_batch(100.0f, 100.0f, 0.05f, 0.0f, 0.01f,
                           {{0, 0.1f, 0.2f}}, 1);
    const auto* batch = flatbuffers::GetRoot<RangePricer::BatchPricingRequest>(buf.data());
    EXPECT_THROW(
        process(*batch->request(), *batch->pairs(), batch->batch_counter_id()),
        std::invalid_argument);
}

TEST(ProcessTest, PriceConsistentWithPriceFunction) {
    auto buf = build_batch(100.0f, 100.0f, 0.05f, 1.0f, 0.01f,
                           {{0, 0.5f, 0.5f}}, 1);
    const auto* batch = flatbuffers::GetRoot<RangePricer::BatchPricingRequest>(buf.data());
    auto result = process(*batch->request(), *batch->pairs(), batch->batch_counter_id());

    const auto* resp = flatbuffers::GetRoot<RangePricer::BatchPricingResponse>(result.data());
    float process_price = resp->results()->Get(0)->price();

    std::vector<PricingInput> inputs = {{0, 0.5f, 0.5f}};
    auto outputs = price(RangePricer::OptionType_Call,
                         100.0f, 100.0f, 0.05f, 1.0f, 0.01f, inputs);

    EXPECT_FLOAT_EQ(process_price, outputs[0].price);
}
