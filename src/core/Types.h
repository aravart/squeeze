#pragma once

#include <string>

namespace squeeze {

class Bus;

enum class SendTap { preFader, postFader };

struct Send {
    Bus* bus;
    float levelDb;
    SendTap tap;
    int id;
};

struct MidiAssignment {
    std::string device;
    int channel;
    int noteLow;
    int noteHigh;

    static MidiAssignment all() { return {"", 0, 0, 127}; }
    static MidiAssignment none() { return {"", -1, 0, 0}; }
};

} // namespace squeeze
