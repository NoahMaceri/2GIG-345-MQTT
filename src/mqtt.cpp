#include "mqtt.h"

#include <mosquittopp.h>
#include <spdlog/spdlog.h>
#include <cstring>

Mqtt::Mqtt(const char* id, const char* host, const int port, const char* username, const char* password, const char* will_topic, const char* will_message) : mosquittopp(id) {
    int version = MQTT_PROTOCOL_V311;
    mosqpp::lib_init();
    this->keepalive = 30;
    this->id = id;
    this->port = port;
    this->host = host;
    this->will_topic = will_topic;
    this->will_message = will_message;
    reinitialise(this->id.c_str(), true);
    // Set version to 3.1.1
    opts_set(MOSQ_OPT_PROTOCOL_VERSION, &version);
    // Set username and password if non-null
    if (std::strlen(username) > 0 && std::strlen(password) > 0) {
        spdlog::info("Using credentials: {}", username);
        username_pw_set(username, password);
    }
    // Set last will and testament (LWT) message
    if (std::strlen(will_topic) > 0 && std::strlen(will_message) > 0) {
        if (int rc = set_will(will_topic, will_message)) {
            spdlog::info("Set LWT message to: {}", will_message);
        }
        else {
            spdlog::error("Failed to set LWT message");
        }
    }

    // non-blocking connection to broker request;
    connect_async(host, port, keepalive);
    // Start thread managing connection / publish / subscribe
    loop_start();
}

Mqtt::~Mqtt() {
    disconnect();
    loop_stop();
    mosqpp::lib_cleanup();
}

bool Mqtt::set_will(const std::string_view topic, const std::string_view message) {
    const int ret = will_set(std::string(topic).c_str(), static_cast<int>(message.size()), message.data(), 1, true);
    return (ret == MOSQ_ERR_SUCCESS);
}

void Mqtt::on_disconnect(int rc) {
    spdlog::warn("MQTT disconnected (rc={})", rc);
}

void Mqtt::on_connect(int rc) {
    if (rc == 0) {
        spdlog::info("MQTT connected");
    }
    else {
        spdlog::error("MQTT failed to connect (rc={})", rc);
    }
}

void Mqtt::on_publish(int mid) {
}

bool Mqtt::send(std::string_view topic, std::string_view message, const int qos, const bool retain) {
    spdlog::info("{}    {}{}", topic, message, (qos == 0) ? "*" : "");
    const int ret = publish(nullptr, std::string(topic).c_str(), static_cast<int>(message.size()), message.data(), qos, retain);
    return (ret == MOSQ_ERR_SUCCESS);
}
