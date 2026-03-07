#ifndef ANALOG_DECODER_H
#define ANALOG_DECODER_H

#include <functional>
#include <utility>

/// @brief Converts raw IQ magnitude samples into a binary OOK (On-Off Keying) bit stream.
///
/// Applies a low-pass EMA filter, decimates by HW_RATIO, and uses an adaptive
/// threshold with slow decay to distinguish signal (1) from noise (0).
/// Decoded bits are delivered via the registered callback.
class AnalogDecoder {
public:
  AnalogDecoder() = default;

  /// @brief Processes a single magnitude sample from the RTL-SDR IQ stream.
  /// @param value Magnitude in the range [0, ~1.4] (sqrt of I^2 + Q^2).
  void handle_magnitude(float value);

  /// @brief Registers the callback invoked for each decoded OOK bit.
  /// @param callback Called with '1' (signal above threshold) or '0' (below).
  void set_callback(std::function<void(char)> callback) { cb = std::move(callback); }

private:
  std::function<void(char)> cb;

  int discarded_samples = 0;  ///< Decimation counter (skip HW_RATIO-1 samples per output).
  float ook_max = 0.0;        ///< Adaptive peak tracker for OOK threshold.
  float val = 0.0;            ///< Low-pass filtered magnitude.
};

#endif // ANALOG_DECODER_H
