#ifndef IMQTT_H
#define IMQTT_H

#include <string_view>

class IMqtt {
public:
    virtual ~IMqtt() = default;
    [[nodiscard]] virtual bool send(std::string_view topic, std::string_view message, int qos = 1, bool retain = true) = 0;
};

#endif // IMQTT_H
