#ifndef DIGITAL_DECODER_H
#define DIGITAL_DECODER_H

#include "mqtt.h"

#include <cstdint>
#include <chrono>
#include <map>
#include <string>
#include <string_view>

class DigitalDecoder {
public:
    DigitalDecoder(Mqtt& mqtt_init, std::string topic_prefix)
        : mqtt(mqtt_init), topic_prefix(std::move(topic_prefix)) {
        // Normalize: ensure trailing slash
        if (!this->topic_prefix.empty() && this->topic_prefix.back() != '/') {
            this->topic_prefix += '/';
        }
    }

    void handle_data(char data);
    void set_rx_good(bool state);

private:
    static bool is_payload_valid(uint64_t payload, uint64_t polynomial = 0);
    void publish_or_warn(std::string_view topic, std::string_view message, int qos = 1, bool retain = true) const;
    void update_sensor_state(uint32_t serial, uint64_t payload);
    void update_keypad_state(uint32_t serial, uint64_t payload);
    void update_keyfob_state(uint32_t serial, uint64_t payload);
    void handle_payload(uint64_t payload);
    void handle_bit(bool value);
    void decode_bit(bool value);
    void check_for_timeouts();

    unsigned int samples_since_edge = 0;
    bool last_sample = false;
    bool rx_good = false;
    std::chrono::steady_clock::time_point last_rx_good_update_time{};
    std::chrono::steady_clock::time_point last_timeout_check_time{};
    Mqtt& mqtt;
    std::string topic_prefix;
    uint32_t packet_count = 0;
    uint32_t error_count = 0;

    struct SensorState {
        std::chrono::steady_clock::time_point last_update_time{};
        bool has_lost_supervision = false;

        bool loop1 = false;
        bool loop2 = false;
        bool loop3 = false;
        bool tamper = false;
        bool low_bat = false;
    };

    struct KeypadState {
        std::chrono::steady_clock::time_point last_update_time{};
        bool has_lost_supervision = false;

        std::string phrase;

        char sequence = 0;
        bool low_bat = false;
    };

    std::map<uint32_t, SensorState> sensor_status_map;
    std::map<uint32_t, KeypadState> keypad_status_map;
    std::map<uint32_t, uint64_t> last_keyfob_payloads;
    std::chrono::steady_clock::time_point last_diag_publish_time{};

    enum class ManchesterState {
        LOW_PHASE_A,
        LOW_PHASE_B,
        HIGH_PHASE_A,
        HIGH_PHASE_B
    };

    uint64_t bit_payload = 0;
    ManchesterState manchester_state = ManchesterState::LOW_PHASE_A;
};

#endif // DIGITAL_DECODER_H
