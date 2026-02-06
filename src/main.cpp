#include <juce_core/juce_core.h>

extern "C" {
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
}

int main()
{
    // JUCE: prove it's linked
    juce::String juceVersion = juce::SystemStats::getJUCEVersion();
    std::cout << "JUCE: " << juceVersion << std::endl;

    // Lua: create a state and run a script
    lua_State* L = luaL_newstate();
    luaL_openlibs(L);

    const char* script = "return _VERSION";
    if (luaL_dostring(L, script) == LUA_OK)
    {
        const char* version = lua_tostring(L, -1);
        std::cout << "Lua:  " << version << std::endl;
        lua_pop(L, 1);
    }
    else
    {
        std::cerr << "Lua error: " << lua_tostring(L, -1) << std::endl;
        lua_pop(L, 1);
    }

    lua_close(L);

    std::cout << "Squeeze hello world OK" << std::endl;
    return 0;
}
