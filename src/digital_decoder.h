#ifndef DIGITAL_DECODER_H
#define DIGITAL_DECODER_H

#include "imqtt.h"

#include <cstdint>
#include <chrono>
#include <map>
#include <string>
#include <string_view>

/// @brief Decodes 345 MHz security sensor protocols and publishes state via MQTT.
///
/// Processing pipeline: raw OOK bits → edge-based sampling → Manchester decoding
/// → 64-bit payload extraction (sync word detection) → CRC validation → MQTT publish.
///
/// Supports Honeywell, 2GIG, and Vivint sensors, keypads, and keyfobs.
class DigitalDecoder {
    friend class DigitalDecoderAccess;

public:
    /// @param mqtt_init    MQTT client used for publishing sensor state.
    /// @param topic_prefix Base MQTT topic (e.g. "security/sensors345"). A trailing
    ///                     slash is appended automatically if missing.
    DigitalDecoder(IMqtt& mqtt_init, std::string topic_prefix)
        : mqtt(mqtt_init), topic_prefix(std::move(topic_prefix)) {
        // Normalize: ensure trailing slash
        if (!this->topic_prefix.empty() && this->topic_prefix.back() != '/') {
            this->topic_prefix += '/';
        }
    }

    /// @brief Feeds a single OOK bit (0 or 1) from the analog decoder.
    void handle_data(char data);

    /// @brief Updates and publishes the receiver health status.
    /// @param state true if the RTL-SDR is receiving data normally.
    void set_rx_good(bool state);

private:
    /// @brief Validates a 48-bit payload using CRC-16 division.
    /// @param payload    Full 64-bit value (upper 16 bits are sync word).
    /// @param polynomial CRC polynomial. If 0, auto-detected from the SOF nibble.
    /// @return true if the CRC remainder is zero.
    static bool is_payload_valid(uint64_t payload, uint64_t polynomial = 0);

    /// @brief Publishes an MQTT message, logging a warning on failure.
    void publish_or_warn(std::string_view topic, std::string_view message, int qos = 1, bool retain = true) const;

    /// @brief Extracts sensor loop/tamper/battery state and publishes changes.
    void update_sensor_state(uint32_t serial, uint64_t payload);

    /// @brief Extracts keypad presses, builds key phrases, and publishes.
    void update_keypad_state(uint32_t serial, uint64_t payload);

    /// @brief Extracts keyfob button presses and publishes (deduplicated per serial).
    void update_keyfob_state(uint32_t serial, uint64_t payload);

    /// @brief Routes a validated payload to the appropriate device handler.
    void handle_payload(uint64_t payload);

    /// @brief Appends a decoded bit to the shift register and checks for sync.
    void handle_bit(bool value);

    /// @brief Manchester state machine — converts phase transitions into data bits.
    void decode_bit(bool value);

    /// @brief Publishes a timeout message for sensors that have gone silent.
    void check_for_timeouts();

    unsigned int samples_since_edge = 0;
    bool last_sample = false;
    bool rx_good = false;
    std::chrono::steady_clock::time_point last_rx_good_update_time{};
    std::chrono::steady_clock::time_point last_timeout_check_time{};
    IMqtt& mqtt;
    std::string topic_prefix;
    uint32_t packet_count = 0;
    uint32_t error_count = 0;

    /// @brief Cached state for a door/window/motion sensor.
    struct SensorState {
        std::chrono::steady_clock::time_point last_update_time{};
        bool has_lost_supervision = false;

        bool loop1 = false;   ///< Primary contact loop (e.g. door open/closed).
        bool loop2 = false;   ///< Secondary loop.
        bool loop3 = false;   ///< Tertiary loop.
        bool tamper = false;  ///< Tamper switch (cover removed).
        bool low_bat = false; ///< Low battery indicator.
    };

    /// @brief Cached state for a wireless keypad.
    struct KeypadState {
        std::chrono::steady_clock::time_point last_update_time{};
        bool has_lost_supervision = false;

        std::string phrase;   ///< Accumulated key sequence (e.g. PIN entry).

        char sequence = 0;    ///< Packet sequence counter for deduplication.
        bool low_bat = false;
    };

    std::map<uint32_t, SensorState> sensor_status_map;
    std::map<uint32_t, KeypadState> keypad_status_map;
    std::map<uint32_t, uint64_t> last_keyfob_payloads;
    std::chrono::steady_clock::time_point last_diag_publish_time{};
    std::chrono::steady_clock::time_point last_heartbeat_time{};

    /// @brief Manchester decoding state machine phases.
    /// Each bit period has two phases (A and B). A transition between
    /// LOW and HIGH at the phase boundary encodes a data bit.
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
