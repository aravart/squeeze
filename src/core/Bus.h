#pragma once

namespace squeeze {

// Forward declaration — full implementation in Tier 4.
// Defined here so Source can store Bus* and tests can create Bus objects.
class Bus {
public:
    Bus() = default;
    ~Bus() = default;

    Bus(const Bus&) = delete;
    Bus& operator=(const Bus&) = delete;
};

} // namespace squeeze
