#include "digital_decoder.h"
#include "mock_mqtt.h"

#include <gtest/gtest.h>
#include <string>

// Accessor to reach private methods for testing
class DigitalDecoderAccess {
public:
    static void handle_payload(DigitalDecoder& d, uint64_t payload) {
        d.handle_payload(payload);
    }

    static bool is_payload_valid(uint64_t payload, uint64_t polynomial = 0) {
        return DigitalDecoder::is_payload_valid(payload, polynomial);
    }
};

class DigitalDecoderTest : public ::testing::Test {
protected:
    MockMqtt mqtt;
    DigitalDecoder decoder{mqtt, "test/"};

    void handle_payload(uint64_t payload) {
        DigitalDecoderAccess::handle_payload(decoder, payload);
    }

    static bool is_payload_valid(uint64_t payload, uint64_t polynomial = 0) {
        return DigitalDecoderAccess::is_payload_valid(payload, polynomial);
    }
};

// -- CRC validation tests --

TEST_F(DigitalDecoderTest, PayloadCrcValidation) {
    // Real captured payloads should pass CRC
    EXPECT_TRUE(is_payload_valid(0xFFFEA5FACD008C60ul));
    EXPECT_TRUE(is_payload_valid(0xFFFEA5FACC80DC60ul));
    EXPECT_TRUE(is_payload_valid(0xFFFEA5FACC006C50ul));

    // Flipping a bit should fail CRC
    EXPECT_FALSE(is_payload_valid(0xFFFEA5FACD008C61ul));
    EXPECT_FALSE(is_payload_valid(0xFFFEA5FACC80DC61ul));
}

// -- Sensor state tests using real captured payloads --

TEST_F(DigitalDecoderTest, ValidSensorPayloadPublishesState) {
    // FFFEA5FACD008C60 — Serial 391885 (0x5FACD), Status 0x00, loop1 CLOSED
    handle_payload(0xFFFEA5FACD008C60ul);

    auto* loop1 = mqtt.find_last("test/sensor/391885/loop1");
    ASSERT_NE(loop1, nullptr);
    EXPECT_EQ(loop1->message, "CLOSED");
}

TEST_F(DigitalDecoderTest, SensorLoop1OpenPayload) {
    // FFFEA5FACC80DC60 — Serial 391884 (0x5FACC), Status 0x80 (loop1 open)
    handle_payload(0xFFFEA5FACC80DC60ul);

    auto* loop1 = mqtt.find_last("test/sensor/391884/loop1");
    ASSERT_NE(loop1, nullptr);
    EXPECT_EQ(loop1->message, "OPEN");
}

TEST_F(DigitalDecoderTest, SensorStateTransition) {
    // Door open
    handle_payload(0xFFFEA5FACC80DC60ul);
    auto* loop1 = mqtt.find_last("test/sensor/391884/loop1");
    ASSERT_NE(loop1, nullptr);
    EXPECT_EQ(loop1->message, "OPEN");

    mqtt.clear();

    // Door closed
    handle_payload(0xFFFEA5FACC006C50ul);
    loop1 = mqtt.find_last("test/sensor/391884/loop1");
    ASSERT_NE(loop1, nullptr);
    EXPECT_EQ(loop1->message, "CLOSED");
}

TEST_F(DigitalDecoderTest, InvalidPayloadIncrementsErrorCount) {
    // Corrupt payload — should fail CRC
    handle_payload(0xFFFEA5FACD008C61ul);

    // Should NOT publish sensor state
    EXPECT_FALSE(mqtt.has_topic("test/sensor/391885/loop1"));

    // Should still publish last_valid_packet_time diagnostic
    EXPECT_TRUE(mqtt.has_topic("test/diagnostics/last_valid_packet_time"));
}

TEST_F(DigitalDecoderTest, DuplicatePayloadDoesNotRepublish) {
    handle_payload(0xFFFEA5FACD008C60ul);
    ASSERT_TRUE(mqtt.has_topic("test/sensor/391885/loop1"));

    mqtt.clear();

    // Same payload again — state unchanged, no new publish for loop fields
    handle_payload(0xFFFEA5FACD008C60ul);
    EXPECT_EQ(mqtt.find_last("test/sensor/391885/loop1"), nullptr);
}

TEST_F(DigitalDecoderTest, ErrorRateDiagnostics) {
    // First call publishes diagnostics immediately (last_diag_publish_time is epoch)
    handle_payload(0xFFFEA5FACD008C61ul); // invalid — publishes "1/1"

    auto* diag = mqtt.find_last("test/diagnostics/error_rate");
    ASSERT_NE(diag, nullptr);
    EXPECT_EQ(diag->message, "1/1");
}

// -- rx_status tests --

TEST_F(DigitalDecoderTest, SetRxGoodPublishesOK) {
    decoder.set_rx_good(true);
    auto* rx = mqtt.find_last("test/rx_status");
    ASSERT_NE(rx, nullptr);
    EXPECT_EQ(rx->message, "OK");
}

TEST_F(DigitalDecoderTest, SetRxBadPublishesFailed) {
    decoder.set_rx_good(false);
    auto* rx = mqtt.find_last("test/rx_status");
    ASSERT_NE(rx, nullptr);
    EXPECT_EQ(rx->message, "FAILED");
}

// -- Topic prefix tests --

TEST_F(DigitalDecoderTest, TopicPrefixTrailingSlash) {
    MockMqtt m2;
    DigitalDecoder d2(m2, "no_slash");
    d2.set_rx_good(true);
    auto* rx = m2.find_last("no_slash/rx_status");
    ASSERT_NE(rx, nullptr);
    EXPECT_EQ(rx->message, "OK");
}

TEST_F(DigitalDecoderTest, TopicPrefixAlreadyHasSlash) {
    MockMqtt m2;
    DigitalDecoder d2(m2, "has_slash/");
    d2.set_rx_good(true);
    auto* rx = m2.find_last("has_slash/rx_status");
    ASSERT_NE(rx, nullptr);
}

// -- Heartbeat test --

TEST_F(DigitalDecoderTest, HeartbeatPublishedOnFirstData) {
    decoder.handle_data(1);

    auto* hb = mqtt.find_last("test/diagnostics/heartbeat");
    ASSERT_NE(hb, nullptr);
    // Should be a reasonable epoch timestamp (after 2020)
    long long ts = std::stoll(hb->message);
    EXPECT_GT(ts, 1577836800LL);
}

// -- handle_data edge cases --

TEST_F(DigitalDecoderTest, InvalidDataIgnored) {
    decoder.handle_data(2);
    decoder.handle_data(-1);
    decoder.handle_data(42);
    EXPECT_TRUE(mqtt.messages.empty());
}
