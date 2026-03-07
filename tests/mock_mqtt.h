#ifndef MOCK_MQTT_H
#define MOCK_MQTT_H

#include "imqtt.h"

#include <string>
#include <vector>

struct PublishedMessage {
    std::string topic;
    std::string message;
    int qos;
    bool retain;
};

class MockMqtt : public IMqtt {
public:
    std::vector<PublishedMessage> messages;

    bool send(std::string_view topic, std::string_view message, int qos = 1, bool retain = true) override {
        messages.push_back({std::string(topic), std::string(message), qos, retain});
        return true;
    }

    void clear() { messages.clear(); }

    bool has_topic(const std::string& topic) const {
        for (const auto& m : messages) {
            if (m.topic == topic) return true;
        }
        return false;
    }

    const PublishedMessage* find_last(const std::string& topic) const {
        for (auto it = messages.rbegin(); it != messages.rend(); ++it) {
            if (it->topic == topic) return &(*it);
        }
        return nullptr;
    }
};

#endif // MOCK_MQTT_H
