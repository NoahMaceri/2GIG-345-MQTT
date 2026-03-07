#include "analog_decoder.h"

#include <cmath>
#include <algorithm>

constexpr int HW_RATIO = 17;
constexpr float MIN_OOK_THRESHOLD = 0.25f;
constexpr float OOK_THRESHOLD_RATIO = 0.75f;
constexpr float OOK_DECAY_PER_SAMPLE = 0.0001f;
constexpr float FILTER_ALPHA = 0.7f;

void AnalogDecoder::handle_magnitude(float value) {
    val = FILTER_ALPHA * val + (1.0f - FILTER_ALPHA) * value;
    value = val;

    if (discarded_samples < (HW_RATIO - 1)) {
        discarded_samples++;
        return;
    }

    discarded_samples = 0;

    value = std::min(value, 1.0f);

    ook_max -= OOK_DECAY_PER_SAMPLE;
    ook_max = std::max(ook_max, value);
    ook_max = std::max(ook_max, MIN_OOK_THRESHOLD / OOK_THRESHOLD_RATIO);

    if (cb) {
        if (value > ook_max * OOK_THRESHOLD_RATIO) {
            cb(1);
        }
        else {
            cb(0);
        }
    }
}
