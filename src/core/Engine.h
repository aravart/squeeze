#pragma once

#include <string>

namespace squeeze {

class Engine {
public:
    Engine();
    ~Engine();

    /// Returns the engine version string.
    std::string getVersion() const;
};

} // namespace squeeze
