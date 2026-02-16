#include "ffi/squeeze_ffi.h"
#include "core/Engine.h"
#include "core/Logger.h"

#include <juce_events/juce_events.h>
#include <cstring>
#include <new>

// --- EngineHandle ---

struct EngineHandle {
    squeeze::Engine engine;
};

static EngineHandle* cast(SqEngine e)
{
    return static_cast<EngineHandle*>(e);
}

static squeeze::Engine& eng(SqEngine e)
{
    return cast(e)->engine;
}

// --- JUCE initialization (lazy, process-wide, never torn down) ---

static bool juceInitialised = false;
static juce::ScopedJuceInitialiser_GUI* juceInit = nullptr;

static void ensureJuceInit()
{
    if (!juceInitialised)
    {
        juceInit = new juce::ScopedJuceInitialiser_GUI();
        juceInitialised = true;
    }
}

// --- String helpers ---

static char* to_c_string(const std::string& s)
{
    return strdup(s.c_str());
}

static void set_error(char** error, const std::string& msg)
{
    if (error) *error = to_c_string(msg);
}

// --- Logger API ---

void sq_set_log_level(int level)
{
    squeeze::Logger::setLevel(static_cast<squeeze::LogLevel>(level));
}

void sq_set_log_callback(void (*callback)(int level, const char* message, void* user_data),
                         void* user_data)
{
    squeeze::Logger::setCallback(callback, user_data);
}

// --- API implementation ---

void sq_free_string(char* s)
{
    free(s);
}

SqEngine sq_engine_create(char** error)
{
    ensureJuceInit();
    try
    {
        return static_cast<SqEngine>(new EngineHandle());
    }
    catch (const std::exception& e)
    {
        set_error(error, e.what());
        return nullptr;
    }
}

void sq_engine_destroy(SqEngine engine)
{
    if (!engine) return;
    delete cast(engine);
}

char* sq_version(SqEngine engine)
{
    return to_c_string(eng(engine).getVersion());
}
