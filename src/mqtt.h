#ifndef MQTT_H
#define MQTT_H

#include "imqtt.h"

#include <string>
#include <string_view>
#include <mosquittopp.h>

/// @brief MQTT client wrapper around mosquittopp.
///
/// Manages connection lifecycle, authentication, and publishing.
/// Starts a background thread for async I/O on construction and
/// cleanly shuts down on destruction.
class Mqtt : public mosqpp::mosquittopp, public IMqtt {
private:
    std::string host;
    std::string id;
    int port;
    int keepalive;
    std::string will_message;
    std::string will_topic;

    void on_connect(int rc) override;
    void on_disconnect(int rc) override;
    void on_publish(int mid) override;

public:
    /// @brief Constructs and connects an MQTT client.
    /// @param id        Client identifier for the broker.
    /// @param host      Broker hostname or IP address.
    /// @param port      Broker port (typically 1883).
    /// @param username  Authentication username (empty string to skip).
    /// @param password  Authentication password (empty string to skip).
    /// @param will_topic  LWT topic published by the broker on unexpected disconnect.
    /// @param will_message LWT payload.
    Mqtt(const char* id, const char* host, int port, const char* username, const char* password, const char* will_topic, const char* will_message);
    ~Mqtt() override;

    /// @brief Publishes a message to the broker.
    /// @param topic   MQTT topic string.
    /// @param message Payload to publish.
    /// @param qos     Quality of service (0, 1, or 2).
    /// @param retain  Whether the broker should retain the message.
    /// @return true on success, false on publish failure.
    [[nodiscard]] bool send(std::string_view topic, std::string_view message, int qos = 1, bool retain = true);

    /// @brief Configures the Last Will and Testament message.
    /// @param topic   LWT topic.
    /// @param message LWT payload.
    /// @return true on success.
    [[nodiscard]] bool set_will(std::string_view topic, std::string_view message);
};

#endif // MQTT_H
