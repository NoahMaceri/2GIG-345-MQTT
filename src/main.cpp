/// @file main.cpp
/// @brief Entry point for the 345 MHz security sensor decoder.
///
/// Reads IQ samples from an RTL-SDR dongle, converts them to magnitude via a
/// precomputed lookup table, runs OOK demodulation and Manchester decoding,
/// then publishes sensor/keypad/keyfob state changes over MQTT.
///
/// Configuration is loaded from a YAML file (default: config.yaml).

#include "digital_decoder.h"
#include "analog_decoder.h"
#include "mqtt.h"

#include <yaml-cpp/yaml.h>
#include <rtl-sdr.h>
#include <spdlog/spdlog.h>

#include <cmath>
#include <string>
#include <string_view>
#include <array>
#include <cstring>
#include <csignal>
#include <atomic>

static std::atomic<bool> running{true};
static rtlsdr_dev_t* global_dev = nullptr; ///< Shared with signal handler to cancel async reads.

/// @brief SIGINT/SIGTERM handler — cancels the RTL-SDR async read loop.
static void signal_handler(int /*signum*/) {
    running = false;
    if (global_dev) {
        rtlsdr_cancel_async(global_dev);
    }
}

void usage(const char* argv0) {
    spdlog::info("Usage: {} [-c <config.yaml>]", argv0);
}

int main(int argc, char** argv) {
    std::string config_path = "config.yaml";

    // First pass: extract -c flag before loading config
    for (int i = 1; i < argc; ++i) {
        std::string_view arg = argv[i];
        if (arg == "-c" && i + 1 < argc) {
            config_path = argv[++i];
        } else if (arg == "-h" || arg == "--help") {
            usage(argv[0]);
            return 0;
        } else {
            spdlog::warn("Unknown argument: {}", arg);
            usage(argv[0]);
            return 1;
        }
    }

    YAML::Node config;
    try {
        config = YAML::LoadFile(config_path);
    } catch (const YAML::Exception& e) {
        spdlog::error("Failed to load {}: {}", config_path, e.what());
        return 1;
    }

    // Configure log level
    auto log_level = config["log_level"].as<std::string>("info");
    spdlog::set_level(spdlog::level::from_str(log_level));

    // MQTT config
    auto mqtt_host = config["mqtt"]["host"].as<std::string>("127.0.0.1");
    int mqtt_port = config["mqtt"]["port"].as<int>(1883);
    auto mqtt_username = config["mqtt"]["username"].as<std::string>("");
    auto mqtt_password = config["mqtt"]["password"].as<std::string>("");
    auto topic_prefix = config["mqtt"]["topic_prefix"].as<std::string>("security/sensors345");

    // RTL-SDR config
    int dev_id = config["rtlsdr"]["device_id"].as<int>(0);
    int freq = config["rtlsdr"]["frequency"].as<int>(345000000);
    int gain = config["rtlsdr"]["gain"].as<int>(490);
    int sample_rate = config["rtlsdr"]["sample_rate"].as<int>(1000000);

    auto lwt_topic = std::format("{}rx_status", topic_prefix.empty() || topic_prefix.back() == '/' ? topic_prefix : topic_prefix + "/");
    auto mqtt = Mqtt("sensors345", mqtt_host.c_str(), mqtt_port, mqtt_username.c_str(), mqtt_password.c_str(), lwt_topic.c_str(), "FAILED");
    auto decoder = DigitalDecoder(mqtt, topic_prefix);
    AnalogDecoder analog;

    // Register signal handlers
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    if (rtlsdr_get_device_count() < 1) {
        spdlog::error("Could not find any devices");
        return -1;
    }

    rtlsdr_dev_t* dev = nullptr;

    if (rtlsdr_open(&dev, dev_id) < 0) {
        spdlog::error("Failed to open device");
        return -1;
    }

    global_dev = dev;

    if (rtlsdr_set_center_freq(dev, freq) < 0) {
        spdlog::error("Failed to set frequency");
        return -1;
    }

    spdlog::info("Successfully set the frequency to {}", rtlsdr_get_center_freq(dev));

    if (rtlsdr_set_tuner_gain_mode(dev, 1) < 0) {
        spdlog::error("Failed to set gain mode");
        return -1;
    }

    if (rtlsdr_set_tuner_gain(dev, gain) < 0) {
        spdlog::error("Failed to set gain");
        return -1;
    }

    spdlog::info("Successfully set gain to {}", rtlsdr_get_tuner_gain(dev));

    if (rtlsdr_set_sample_rate(dev, sample_rate) < 0) {
        spdlog::error("Failed to set sample rate");
        return -1;
    }

    spdlog::info("Successfully set the sample rate to {}", rtlsdr_get_sample_rate(dev));

    rtlsdr_reset_buffer(dev);

    // Precompute magnitude lookup table: maps every possible (I, Q) byte pair
    // to sqrt(I^2 + Q^2), avoiding per-sample sqrt calls at runtime.
    std::array<float, 0x10000> mag_lut{};
    for (uint32_t ii = 0; ii < 0x10000; ++ii) {
        uint8_t real_i = ii & 0xFF;
        uint8_t imag_i = ii >> 8;

        float real = (static_cast<float>(real_i) - 127.4f) * (1.0f / 128.0f);
        float imag = (static_cast<float>(imag_i) - 127.4f) * (1.0f / 128.0f);

        float mag = std::sqrt(real * real + imag * imag);
        mag_lut[ii] = mag;
    }

    analog.set_callback([&](const char data) { decoder.handle_data(data); });

    struct CallbackContext {
        AnalogDecoder* analog;
        std::array<float, 0x10000>* mag_lut;
    };

    CallbackContext ctx{&analog, &mag_lut};

    auto cb = [](unsigned char* buf, uint32_t len, void* user_ctx) {
        const auto* context = static_cast<CallbackContext*>(user_ctx);

        int n_samples = len / 2;
        for (int i = 0; i < n_samples; ++i) {
            uint16_t sample;
            std::memcpy(&sample, buf + i * 2, sizeof(sample));
            const float mag = (*context->mag_lut)[sample];
            context->analog->handle_magnitude(mag);
        }
    };

    // Initialize RX state to good
    decoder.set_rx_good(true);
    spdlog::info("Starting async read...");
    const int err = rtlsdr_read_async(dev, cb, &ctx, 0, 0);

    if (err != 0) {
        spdlog::error("Read async returned {}", err);
    }

    spdlog::info("Shutting down...");
    global_dev = nullptr;
    rtlsdr_close(dev);
    return 0;
}
