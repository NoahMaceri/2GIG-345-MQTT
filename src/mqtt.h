#ifndef MQTT_H
#define MQTT_H

#include <string>
#include <string_view>
#include <mosquittopp.h>

class Mqtt : public mosqpp::mosquittopp
{
    private:
        std::string     host;
        std::string     id;
        int             port;
        int             keepalive;
        std::string     will_message;
        std::string     will_topic;

        void on_connect(int rc) override;
        void on_disconnect(int rc) override;
        void on_publish(int mid) override;

    public:
        Mqtt(const char *id, const char *host, int port, const char *username, const char *password, const char *will_topic, const char *will_message);
        ~Mqtt() override;
        [[nodiscard]] bool send(std::string_view topic, std::string_view message, int qos=1, bool retain=true);
        [[nodiscard]] bool set_will(std::string_view topic, std::string_view message);
};

#endif // MQTT_H