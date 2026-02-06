#include <juce_core/juce_core.h>

#define SOL_ALL_SAFETIES_ON 1
#include <sol/sol.hpp>

int main()
{
    juce::String juceVersion = juce::SystemStats::getJUCEVersion();
    std::cout << "JUCE: " << juceVersion << std::endl;

    sol::state lua;
    lua.open_libraries(sol::lib::base, sol::lib::string, sol::lib::math);

    lua["getVersion"] = []() { return "Squeeze 0.1.0"; };

    std::string luaVersion = lua["_VERSION"];
    std::string appVersion = lua.script("return getVersion()").get<std::string>();

    std::cout << "Lua:  " << luaVersion << std::endl;
    std::cout << "sol2: called back into C++ -> " << appVersion << std::endl;

    std::cout << "Squeeze hello world OK" << std::endl;
    return 0;
}
