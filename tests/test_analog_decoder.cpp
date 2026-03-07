#include "analog_decoder.h"

#include <gtest/gtest.h>
#include <vector>
#include <cmath>

class AnalogDecoderTest : public ::testing::Test {
protected:
    AnalogDecoder decoder;
    std::vector<char> output_bits;

    void SetUp() override {
        decoder.set_callback([this](char bit) { output_bits.push_back(bit); });
    }

    // Feed N samples of a constant magnitude
    void feed_constant(float mag, int count) {
        for (int i = 0; i < count; ++i) {
            decoder.handle_magnitude(mag);
        }
    }
};

TEST_F(AnalogDecoderTest, HighSignalProducesOnes) {
    // Feed a strong signal well above threshold for enough samples to produce output
    // HW_RATIO=17, so every 17th sample produces an output bit
    feed_constant(0.9f, 17 * 10);

    ASSERT_FALSE(output_bits.empty());
    for (char bit : output_bits) {
        EXPECT_EQ(bit, 1);
    }
}

TEST_F(AnalogDecoderTest, LowSignalProducesZeros) {
    // First prime the peak tracker with a strong signal
    feed_constant(0.9f, 17 * 5);
    output_bits.clear();

    // Then drop to near-zero — should produce 0s
    feed_constant(0.0f, 17 * 20);

    ASSERT_FALSE(output_bits.empty());
    // After the transition period, all bits should be 0
    int zero_count = 0;
    for (char bit : output_bits) {
        if (bit == 0) zero_count++;
    }
    EXPECT_GT(zero_count, 0);
}

TEST_F(AnalogDecoderTest, NoCallbackNoCrash) {
    AnalogDecoder d;
    // Should not crash without a callback
    EXPECT_NO_THROW(d.handle_magnitude(0.5f));
    EXPECT_NO_THROW(d.handle_magnitude(0.0f));
}

TEST_F(AnalogDecoderTest, DecimationRate) {
    // HW_RATIO=17: expect 1 output bit per 17 input samples
    int n_samples = 17 * 5;
    feed_constant(0.9f, n_samples);

    // Should have exactly 5 output bits (first sample is discarded in decimation)
    EXPECT_EQ(output_bits.size(), 5u);
}

TEST_F(AnalogDecoderTest, SignalTransitionProducesMixedBits) {
    // Alternate high and low to produce a mix of 1s and 0s
    // Prime with high signal first
    feed_constant(0.9f, 17 * 10);
    output_bits.clear();

    // Alternate: high burst then low burst
    feed_constant(0.0f, 17 * 10);
    feed_constant(0.9f, 17 * 10);

    bool has_zero = false, has_one = false;
    for (char bit : output_bits) {
        if (bit == 0) has_zero = true;
        if (bit == 1) has_one = true;
    }
    EXPECT_TRUE(has_zero);
    EXPECT_TRUE(has_one);
}
