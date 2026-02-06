#pragma once

#include <string>

namespace squeeze {

enum class PortDirection { input, output };
enum class SignalType { audio, midi };

struct PortDescriptor {
    std::string name;
    PortDirection direction;
    SignalType signalType;
    int channels;

    bool operator==(const PortDescriptor& o) const {
        return name == o.name && direction == o.direction
            && signalType == o.signalType && channels == o.channels;
    }
    bool operator!=(const PortDescriptor& o) const { return !(*this == o); }
};

struct PortAddress {
    int nodeId;
    PortDirection direction;
    std::string portName;

    bool operator==(const PortAddress& o) const {
        return nodeId == o.nodeId && direction == o.direction
            && portName == o.portName;
    }
    bool operator!=(const PortAddress& o) const { return !(*this == o); }
};

bool isValid(const PortDescriptor& port);
bool canConnect(const PortDescriptor& src, const PortDescriptor& dst);

} // namespace squeeze
