#include "digital_decoder.h"
#include "mqtt.h"

#include <spdlog/spdlog.h>
#include <string>
#include <format>
#include <chrono>
#include <string_view>

constexpr int SENSOR_TIMEOUT_MIN = 90 * 5;
constexpr int TIMEOUT_CHECK_INTERVAL_SEC = 60;

constexpr uint64_t SYNC_MASK = 0xFFFF000000000000ul;
constexpr uint64_t SYNC_PATTERN = 0xFFFE000000000000ul;

constexpr int RX_GOOD_MIN_SEC = 60;
constexpr int DIAG_PUBLISH_INTERVAL_SEC = 60;

// Payload field masks and shifts
constexpr uint64_t SOF_MASK       = 0xF00000000000ul;
constexpr int      SOF_SHIFT      = 44;
constexpr uint64_t SERIAL_MASK    = 0x0FFFFF000000ul;
constexpr int      SERIAL_SHIFT   = 24;
constexpr uint64_t STATUS_MASK    = 0x000000FF0000ul;
constexpr int      STATUS_SHIFT   = 16;
constexpr uint64_t KEYCODE_MASK   = 0x000000F00000ul;
constexpr int      KEYCODE_SHIFT  = 20;

// Sensor status bit masks
constexpr uint64_t LOOP1_BIT      = 0x000000800000ul;
constexpr uint64_t TAMPER_BIT     = 0x000000400000ul;
constexpr uint64_t LOOP2_BIT      = 0x000000200000ul;
constexpr uint64_t LOOP3_BIT      = 0x000000100000ul;
constexpr uint64_t LOW_BAT_BIT    = 0x000000080000ul;
constexpr uint64_t SUPERVISED_BIT = 0x000000040000ul;
constexpr uint64_t KEYPAD_BAT_BIT = 0x000000020000ul;

// Device type flags (in status byte)
constexpr uint64_t KEYPAD_FLAG = 0x01;
constexpr uint64_t KEYFOB_FLAG = 0x02;

// CRC polynomials
constexpr uint64_t CRC_POLY_2GIG     = 0x18050;
constexpr uint64_t CRC_POLY_HONEYWELL = 0x18005;

// Start-of-frame brand identifiers
constexpr uint64_t SOF_HONEYWELL = 0x8;
constexpr uint64_t SOF_VIVINT_D  = 0xD;
constexpr uint64_t SOF_VIVINT_E  = 0xE;

// Keyfob button codes
constexpr char KEYFOB_AWAY   = 0x1;
constexpr char KEYFOB_DISARM = 0x2;
constexpr char KEYFOB_STAY   = 0x4;
constexpr char KEYFOB_AUX    = 0x8;

// Keypad key codes
constexpr char KEYPAD_STAR   = 0xA;
constexpr char KEYPAD_ZERO   = 0xB;
constexpr char KEYPAD_HASH   = 0xC;
constexpr char KEYPAD_STAY   = 0xD;
constexpr char KEYPAD_AWAY   = 0xE;
constexpr char KEYPAD_FIRE   = 0xF;
constexpr char KEYPAD_POLICE = 0x0;

inline constexpr std::string_view OPEN_SENSOR_MSG = "OPEN";
inline constexpr std::string_view CLOSED_SENSOR_MSG = "CLOSED";
inline constexpr std::string_view TAMPER_MSG = "TAMPER";
inline constexpr std::string_view UNTAMPERED_MSG = "OK";
inline constexpr std::string_view LOW_BAT_MSG = "LOW";
inline constexpr std::string_view OK_BAT_MSG = "OK";
inline constexpr std::string_view RX_OK_MSG = "OK";
inline constexpr std::string_view RX_FAILED_MSG = "FAILED";
inline constexpr std::string_view TIMEOUT_MSG = "TIMEOUT";

void DigitalDecoder::publish_or_warn(std::string_view topic, const std::string_view message, const int qos, const bool retain) const {
    if (!mqtt.send(topic, message, qos, retain)) {
        spdlog::warn("Failed to publish to {}", topic);
    }
}

void DigitalDecoder::set_rx_good(const bool state) {
    const auto topic = std::format("{}rx_status", topic_prefix);
    const auto now = std::chrono::steady_clock::now();

    if (rx_good != state || (now - last_rx_good_update_time) > std::chrono::seconds(RX_GOOD_MIN_SEC)) {
        publish_or_warn(topic, state ? RX_OK_MSG : RX_FAILED_MSG);
    }

    rx_good = state;
    last_rx_good_update_time = now;

    // Rate-limit timeout checks to once per minute
    if ((now - last_timeout_check_time) > std::chrono::seconds(TIMEOUT_CHECK_INTERVAL_SEC)) {
        last_timeout_check_time = now;
        check_for_timeouts();
    }
}

void DigitalDecoder::update_keyfob_state(uint32_t serial, const uint64_t payload) {
    if (const auto it = last_keyfob_payloads.find(serial); it != last_keyfob_payloads.end() && it->second == payload) {
        return;
    }

    const auto topic = std::format("{}keyfob/{}/keypress", topic_prefix, serial);
    const char c = ((payload & KEYCODE_MASK) >> KEYCODE_SHIFT);
    std::string key;
    if (c == KEYFOB_AWAY) {
        key = "AWAY";
    } else if (c == KEYFOB_DISARM) {
        key = "DISARM";
    } else if (c == KEYFOB_STAY) {
        key = "STAY";
    } else if (c == KEYFOB_AUX) {
        key = "AUX";
    } else {
        key = "UNK";
    }
    publish_or_warn(topic, key, 1, false);

    last_keyfob_payloads[serial] = payload;
}

void DigitalDecoder::update_keypad_state(uint32_t serial, const uint64_t payload) {
    const auto now = std::chrono::steady_clock::now();

    KeypadState last_state;
    KeypadState current_state;

    current_state.last_update_time = now;
    current_state.has_lost_supervision = false;

    current_state.sequence = (payload & SOF_MASK) >> SOF_SHIFT;
    current_state.low_bat = payload & KEYPAD_BAT_BIT;

    // If supervised, we won't get keypress updates, so we shouldn't update the state or publish anything.
    if (payload & SUPERVISED_BIT) {
        return;
    }

    const auto found = keypad_status_map.find(serial);
    if (found == keypad_status_map.end()) {
        last_state.sequence = static_cast<char>(0xff);
        last_state.low_bat = !current_state.low_bat;
    } else {
        last_state = found->second;
    }

    if (current_state.sequence != last_state.sequence) {
        const auto topic = std::format("{}keypad/{}/keypress", topic_prefix, serial);
        const char c = ((payload & KEYCODE_MASK) >> KEYCODE_SHIFT);

        std::string key;
        if (c == KEYPAD_STAR) {
            key = "*";
        } else if (c == KEYPAD_ZERO) {
            key = "0";
        } else if (c == KEYPAD_HASH) {
            key = "#";
        } else if (c == KEYPAD_STAY) {
            key = "STAY";
        } else if (c == KEYPAD_AWAY) {
            key = "AWAY";
        } else if (c == KEYPAD_FIRE) {
            key = "FIRE";
        } else if (c == KEYPAD_POLICE) {
            key = "POLICE";
        } else {
            key = (c + '0');
        }
        publish_or_warn(topic, key, 1, false);

        if ((c >= 1 && c <= KEYPAD_HASH) && (now - last_state.last_update_time <= std::chrono::seconds(2)) && (last_state.phrase.length() < 10)) {
            current_state.phrase = last_state.phrase + key;
            const auto phrase_topic = std::format("{}keypad/{}/keyphrase/{}", topic_prefix, serial, current_state.phrase.length());
            publish_or_warn(phrase_topic, current_state.phrase, 1, false);
        } else if (c == KEYPAD_ZERO || (c >= 1 && c <= 9)) {
            current_state.phrase = key;
        }

        keypad_status_map[serial] = current_state;
    }
}

void DigitalDecoder::update_sensor_state(uint32_t serial, uint64_t payload) {
    const auto now = std::chrono::steady_clock::now();

    SensorState last_state;
    SensorState current_state;

    current_state.last_update_time = now;
    current_state.has_lost_supervision = false;

    current_state.loop1 = payload & LOOP1_BIT;
    current_state.loop2 = payload & LOOP2_BIT;
    current_state.loop3 = payload & LOOP3_BIT;
    current_state.tamper = payload & TAMPER_BIT;
    current_state.low_bat = payload & LOW_BAT_BIT;

    const bool supervised = payload & SUPERVISED_BIT;

    const auto found = sensor_status_map.find(serial);
    if (found == sensor_status_map.end()) {
        last_state.has_lost_supervision = !current_state.has_lost_supervision;
        last_state.loop1 = !current_state.loop1;
        last_state.loop2 = !current_state.loop2;
        last_state.loop3 = !current_state.loop3;
        last_state.tamper = !current_state.tamper;
        last_state.low_bat = !current_state.low_bat;
    } else {
        last_state = found->second;
    }

    const int qos = supervised ? 0 : 1;

    auto publish_field = [&](const bool current, const bool last, const char* attr, const std::string_view on_msg, const std::string_view off_msg) {
        if (current != last || supervised) {
            const auto topic = std::format("{}sensor/{}/{}", topic_prefix, serial, attr);
            publish_or_warn(topic, current ? on_msg : off_msg, qos);
        }
    };

    publish_field(current_state.loop1, last_state.loop1, "loop1", OPEN_SENSOR_MSG, CLOSED_SENSOR_MSG);
    publish_field(current_state.loop2, last_state.loop2, "loop2", OPEN_SENSOR_MSG, CLOSED_SENSOR_MSG);
    publish_field(current_state.loop3, last_state.loop3, "loop3", OPEN_SENSOR_MSG, CLOSED_SENSOR_MSG);
    publish_field(current_state.tamper, last_state.tamper, "tamper", TAMPER_MSG, UNTAMPERED_MSG);
    publish_field(current_state.low_bat, last_state.low_bat, "battery", LOW_BAT_MSG, OK_BAT_MSG);

    sensor_status_map[serial] = current_state;
}

void DigitalDecoder::check_for_timeouts() {
    const auto now = std::chrono::steady_clock::now();

    for (auto& [serial, state] : sensor_status_map) {
        if ((now - state.last_update_time) > std::chrono::minutes(SENSOR_TIMEOUT_MIN)) {
            if (!state.has_lost_supervision) {
                state.has_lost_supervision = true;
                auto status_topic = std::format("{}sensor/{}/status", topic_prefix, serial);
                publish_or_warn(status_topic, TIMEOUT_MSG);
            }
        }
    }
}

bool DigitalDecoder::is_payload_valid(const uint64_t payload, uint64_t polynomial) {
    const uint64_t sof = (payload & SOF_MASK) >> SOF_SHIFT;

    if (polynomial == 0) {
        if (sof == SOF_HONEYWELL) {
            polynomial = CRC_POLY_HONEYWELL;
        } else {
            // 2GIG, Vivint, and unknown brands all use the same polynomial
            polynomial = CRC_POLY_2GIG;
        }
    }
    uint64_t sum = payload & (~SYNC_MASK);
    uint64_t current_divisor = polynomial << 31;

    while (current_divisor >= polynomial) {
        if (sum != 0 && __builtin_clzll(sum) == __builtin_clzll(current_divisor)) {
            sum ^= current_divisor;
        }
        current_divisor >>= 1;
    }

    return (sum == 0);
}

void DigitalDecoder::handle_payload(uint64_t payload) {
    const uint64_t sof = (payload & SOF_MASK) >> SOF_SHIFT;
    const uint64_t ser = (payload & SERIAL_MASK) >> SERIAL_SHIFT;
    const uint64_t typ = (payload & STATUS_MASK) >> STATUS_SHIFT;

    const bool valid_sensor = is_payload_valid(payload);
    const bool valid_keypad = is_payload_valid(payload, CRC_POLY_2GIG) && (typ & KEYPAD_FLAG);
    const bool valid_keyfob = is_payload_valid(payload, CRC_POLY_2GIG) && (typ & KEYFOB_FLAG);

    if (valid_sensor || valid_keypad || valid_keyfob) {
        spdlog::info("Valid Payload: {:X} (Serial {}/{:X}, Status {:X})", payload, ser, ser, typ);
        if (sof == SOF_HONEYWELL) {
            spdlog::info("Honeywell Sensor");
        } else if (sof == SOF_VIVINT_D || sof == SOF_VIVINT_E) {
            spdlog::info("Vivint Sensor {:x}", sof);
        }
    } else {
        spdlog::warn("Invalid Payload: {:X} (Serial {}/{:X}, Status {:X})", payload, ser, ser, typ);
        // Known SOFs: 0x2-0x4, 0x7-0xF
        if (sof < 0x2 || sof == 0x5 || sof == 0x6) {
            spdlog::warn("Unknown Brand Sensor {:x}", sof);
        }
    }

    packet_count++;
    if (!valid_sensor && !valid_keypad && !valid_keyfob) {
        error_count++;
        spdlog::warn("{}/{} packets failed CRC", error_count, packet_count);
    }

    // Rate-limit diagnostics publishing
    const auto now = std::chrono::steady_clock::now();
    if ((now - last_diag_publish_time) > std::chrono::seconds(DIAG_PUBLISH_INTERVAL_SEC)) {
        last_diag_publish_time = now;
        const auto diag_topic = std::format("{}diagnostics/error_rate", topic_prefix);
        const auto rate = std::format("{}/{}", error_count, packet_count);
        publish_or_warn(diag_topic, rate, 0, true);
    }

    if (valid_sensor && !keypad_status_map.contains(ser)) {
        set_rx_good(true);
        update_sensor_state(ser, payload);
    } else if (valid_keypad) {
        set_rx_good(true);
        update_keypad_state(ser, payload);
    } else if (valid_keyfob) {
        set_rx_good(true);
        update_keyfob_state(ser, payload);
    }
}


void DigitalDecoder::handle_bit(bool value) {
    bit_payload <<= 1;
    bit_payload |= (value ? 1 : 0);

    if ((bit_payload & SYNC_MASK) == SYNC_PATTERN) {
        handle_payload(bit_payload);
        bit_payload = 0;
    }
}

void DigitalDecoder::decode_bit(bool value) {
    switch (manchester_state) {
    case ManchesterState::LOW_PHASE_A: {
        manchester_state = value ? ManchesterState::HIGH_PHASE_B : ManchesterState::LOW_PHASE_A;
        break;
    }
    case ManchesterState::LOW_PHASE_B: {
        handle_bit(false);
        manchester_state = value ? ManchesterState::HIGH_PHASE_A : ManchesterState::LOW_PHASE_A;
        break;
    }
    case ManchesterState::HIGH_PHASE_A: {
        manchester_state = value ? ManchesterState::HIGH_PHASE_A : ManchesterState::LOW_PHASE_B;
        break;
    }
    case ManchesterState::HIGH_PHASE_B: {
        handle_bit(true);
        manchester_state = value ? ManchesterState::HIGH_PHASE_A : ManchesterState::LOW_PHASE_A;
        break;
    }
    }
}

void DigitalDecoder::handle_data(char data) {
    constexpr int samples_per_bit = 8;

    if (data != 0 && data != 1) return;

    const bool this_sample = (data == 1);

    if (this_sample == last_sample) {
        samples_since_edge++;

        if ((samples_since_edge % samples_per_bit) == (samples_per_bit / 2)) {
            decode_bit(this_sample);
        }
    } else {
        samples_since_edge = 1;
    }
    last_sample = this_sample;
}
