#include "ffi/squeeze_ffi.h"
#include "core/Engine.h"
#include "core/GainNode.h"
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

// --- Node management ---

int sq_add_gain(SqEngine engine)
{
    return eng(engine).addNode("gain", std::make_unique<squeeze::GainNode>());
}

bool sq_remove_node(SqEngine engine, int node_id)
{
    return eng(engine).removeNode(node_id);
}

char* sq_node_name(SqEngine engine, int node_id)
{
    auto name = eng(engine).getNodeName(node_id);
    if (name.empty()) return nullptr;
    return to_c_string(name);
}

SqPortList sq_get_ports(SqEngine engine, int node_id)
{
    SqPortList result = {nullptr, 0};
    auto* node = eng(engine).getNode(node_id);
    if (!node) return result;

    auto inputs = node->getInputPorts();
    auto outputs = node->getOutputPorts();
    int total = static_cast<int>(inputs.size() + outputs.size());
    if (total == 0) return result;

    result.ports = static_cast<SqPortDescriptor*>(
        malloc(sizeof(SqPortDescriptor) * static_cast<size_t>(total)));
    result.count = total;

    int i = 0;
    for (const auto& p : inputs)
    {
        result.ports[i].name = strdup(p.name.c_str());
        result.ports[i].direction = 0;
        result.ports[i].signal_type = (p.signalType == squeeze::SignalType::audio) ? 0 : 1;
        result.ports[i].channels = p.channels;
        i++;
    }
    for (const auto& p : outputs)
    {
        result.ports[i].name = strdup(p.name.c_str());
        result.ports[i].direction = 1;
        result.ports[i].signal_type = (p.signalType == squeeze::SignalType::audio) ? 0 : 1;
        result.ports[i].channels = p.channels;
        i++;
    }

    return result;
}

void sq_free_port_list(SqPortList list)
{
    for (int i = 0; i < list.count; i++)
        free(list.ports[i].name);
    free(list.ports);
}

SqParamDescriptorList sq_param_descriptors(SqEngine engine, int node_id)
{
    SqParamDescriptorList result = {nullptr, 0};
    auto* node = eng(engine).getNode(node_id);
    if (!node) return result;

    auto descs = node->getParameterDescriptors();
    if (descs.empty()) return result;

    result.descriptors = static_cast<SqParamDescriptor*>(
        malloc(sizeof(SqParamDescriptor) * descs.size()));
    result.count = static_cast<int>(descs.size());

    for (int i = 0; i < result.count; i++)
    {
        result.descriptors[i].name = strdup(descs[static_cast<size_t>(i)].name.c_str());
        result.descriptors[i].default_value = descs[static_cast<size_t>(i)].defaultValue;
        result.descriptors[i].num_steps = descs[static_cast<size_t>(i)].numSteps;
        result.descriptors[i].automatable = descs[static_cast<size_t>(i)].automatable;
        result.descriptors[i].boolean_param = descs[static_cast<size_t>(i)].boolean;
        result.descriptors[i].label = strdup(descs[static_cast<size_t>(i)].label.c_str());
        result.descriptors[i].group = strdup(descs[static_cast<size_t>(i)].group.c_str());
    }

    return result;
}

void sq_free_param_descriptor_list(SqParamDescriptorList list)
{
    for (int i = 0; i < list.count; i++)
    {
        free(list.descriptors[i].name);
        free(list.descriptors[i].label);
        free(list.descriptors[i].group);
    }
    free(list.descriptors);
}

float sq_get_param(SqEngine engine, int node_id, const char* name)
{
    auto* node = eng(engine).getNode(node_id);
    if (!node) return 0.0f;
    return node->getParameter(name);
}

bool sq_set_param(SqEngine engine, int node_id, const char* name, float value)
{
    auto* node = eng(engine).getNode(node_id);
    if (!node) return false;
    node->setParameter(name, value);
    return true;
}

char* sq_param_text(SqEngine engine, int node_id, const char* name)
{
    auto* node = eng(engine).getNode(node_id);
    if (!node) return nullptr;
    auto text = node->getParameterText(name);
    if (text.empty()) return nullptr;
    return to_c_string(text);
}
