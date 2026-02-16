#include "core/Engine.h"

namespace squeeze {

Engine::Engine() {}

Engine::~Engine() {}

std::string Engine::getVersion() const
{
    return "0.2.0";
}

} // namespace squeeze
