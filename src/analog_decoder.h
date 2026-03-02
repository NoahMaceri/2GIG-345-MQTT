#ifndef ANALOG_DECODER_H
#define ANALOG_DECODER_H

#include <functional>
#include <utility>

class AnalogDecoder
{
  public:
    AnalogDecoder() = default;

    void handle_magnitude(float value);
    void set_callback(std::function<void(char)> callback) {cb = std::move(callback);}

  private:
    std::function<void(char)> cb;

    int discarded_samples = 0;
    float ook_max = 0.0;
    float val = 0.0;
};

#endif // ANALOG_DECODER_H