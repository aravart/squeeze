#include "ffi/squeeze_ffi.h"
#include "core/AudioDevice.h"
#include "core/Engine.h"
#include "core/GainNode.h"
#include "core/Logger.h"
#include "core/MidiDeviceManager.h"
#include "core/PluginManager.h"
#include "core/PluginNode.h"
#include "core/TestProcessor.h"

#include <juce_events/juce_events.h>
#include <cstring>
#include <mutex>
#include <new>

// --- EngineHandle ---

struct EngineHandle {
    squeeze::Engine engine;
    squeeze::PluginManager pluginManager;
    squeeze::AudioDevice audioDevice{engine};
    squeeze::MidiDeviceManager midiDeviceManager{engine.getMidiRouter()};
    std::mutex audioMutex;
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

int sq_output_node(SqEngine engine)
{
    return eng(engine).getOutputNodeId();
}

int sq_node_count(SqEngine engine)
{
    return eng(engine).getNodeCount();
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

// --- Connection management ---

int sq_connect(SqEngine engine, int src_node, const char* src_port,
               int dst_node, const char* dst_port, char** error)
{
    std::string err;
    int result = eng(engine).connect(src_node, src_port, dst_node, dst_port, err);
    if (result < 0)
    {
        set_error(error, err);
    }
    else
    {
        if (error) *error = nullptr;
    }
    return result;
}

bool sq_disconnect(SqEngine engine, int conn_id)
{
    return eng(engine).disconnect(conn_id);
}

SqConnectionList sq_connections(SqEngine engine)
{
    SqConnectionList result = {nullptr, 0};
    auto conns = eng(engine).getConnections();
    if (conns.empty()) return result;

    result.count = static_cast<int>(conns.size());
    result.connections = static_cast<SqConnection*>(
        malloc(sizeof(SqConnection) * conns.size()));

    for (int i = 0; i < result.count; i++)
    {
        result.connections[i].id = conns[static_cast<size_t>(i)].id;
        result.connections[i].src_node = conns[static_cast<size_t>(i)].source.nodeId;
        result.connections[i].src_port = strdup(conns[static_cast<size_t>(i)].source.portName.c_str());
        result.connections[i].dst_node = conns[static_cast<size_t>(i)].dest.nodeId;
        result.connections[i].dst_port = strdup(conns[static_cast<size_t>(i)].dest.portName.c_str());
    }

    return result;
}

void sq_free_connection_list(SqConnectionList list)
{
    for (int i = 0; i < list.count; i++)
    {
        free(list.connections[i].src_port);
        free(list.connections[i].dst_port);
    }
    free(list.connections);
}

// --- Transport ---

void sq_transport_play(SqEngine engine) { eng(engine).transportPlay(); }
void sq_transport_stop(SqEngine engine) { eng(engine).transportStop(); }
void sq_transport_pause(SqEngine engine) { eng(engine).transportPause(); }
void sq_transport_set_tempo(SqEngine engine, double bpm) { eng(engine).transportSetTempo(bpm); }
void sq_transport_set_time_signature(SqEngine engine, int numerator, int denominator)
{
    eng(engine).transportSetTimeSignature(numerator, denominator);
}
void sq_transport_seek_samples(SqEngine engine, int64_t samples) { eng(engine).transportSeekSamples(samples); }
void sq_transport_seek_beats(SqEngine engine, double beats) { eng(engine).transportSeekBeats(beats); }
void sq_transport_set_loop_points(SqEngine engine, double start_beats, double end_beats)
{
    eng(engine).transportSetLoopPoints(start_beats, end_beats);
}
void sq_transport_set_looping(SqEngine engine, bool enabled) { eng(engine).transportSetLooping(enabled); }
double sq_transport_position(SqEngine engine) { return eng(engine).getTransportPosition(); }
double sq_transport_tempo(SqEngine engine) { return eng(engine).getTransportTempo(); }
bool sq_transport_is_playing(SqEngine engine) { return eng(engine).isTransportPlaying(); }

// --- Event scheduling ---

bool sq_schedule_note_on(SqEngine engine, int node_id, double beat_time,
                         int channel, int note, float velocity)
{
    return eng(engine).scheduleNoteOn(node_id, beat_time, channel, note, velocity);
}

bool sq_schedule_note_off(SqEngine engine, int node_id, double beat_time,
                          int channel, int note)
{
    return eng(engine).scheduleNoteOff(node_id, beat_time, channel, note);
}

bool sq_schedule_cc(SqEngine engine, int node_id, double beat_time,
                    int channel, int cc_num, int cc_val)
{
    return eng(engine).scheduleCC(node_id, beat_time, channel, cc_num, cc_val);
}

bool sq_schedule_param_change(SqEngine engine, int node_id, double beat_time,
                              const char* param_name, float value)
{
    return eng(engine).scheduleParamChange(node_id, beat_time, param_name, value);
}

// --- Plugin nodes ---

int sq_add_test_synth(SqEngine engine)
{
    auto proc = std::make_unique<squeeze::TestProcessor>(0, 2, true);
    auto node = std::make_unique<squeeze::PluginNode>(std::move(proc), 0, 2, true);
    return eng(engine).addNode("test_synth", std::move(node));
}

// --- String list ---

void sq_free_string_list(SqStringList list)
{
    for (int i = 0; i < list.count; i++)
        free(list.items[i]);
    free(list.items);
}

// --- Plugin manager ---

bool sq_load_plugin_cache(SqEngine engine, const char* path, char** error)
{
    std::string err;
    bool ok = cast(engine)->pluginManager.loadCache(path, err);
    if (!ok)
        set_error(error, err);
    else if (error)
        *error = nullptr;
    return ok;
}

int sq_add_plugin(SqEngine engine, const char* name, char** error)
{
    auto* handle = cast(engine);
    double sr = handle->engine.getSampleRate();
    int bs = handle->engine.getBlockSize();

    std::string err;
    auto node = handle->pluginManager.createNode(name, sr, bs, err);
    if (!node)
    {
        set_error(error, err);
        return -1;
    }

    if (error) *error = nullptr;
    return handle->engine.addNode(name, std::move(node));
}

SqStringList sq_available_plugins(SqEngine engine)
{
    SqStringList result = {nullptr, 0};
    auto names = cast(engine)->pluginManager.getAvailablePlugins();
    if (names.empty()) return result;

    result.count = static_cast<int>(names.size());
    result.items = static_cast<char**>(malloc(sizeof(char*) * names.size()));

    for (int i = 0; i < result.count; i++)
        result.items[i] = strdup(names[static_cast<size_t>(i)].c_str());

    return result;
}

int sq_num_plugins(SqEngine engine)
{
    return cast(engine)->pluginManager.getNumPlugins();
}

// --- MIDI device management ---

void sq_free_midi_route_list(SqMidiRouteList list)
{
    for (int i = 0; i < list.count; i++)
        free(list.routes[i].device);
    free(list.routes);
}

SqStringList sq_midi_devices(SqEngine engine)
{
    SqStringList result = {nullptr, 0};
    auto names = cast(engine)->midiDeviceManager.getAvailableDevices();
    if (names.empty()) return result;

    result.count = static_cast<int>(names.size());
    result.items = static_cast<char**>(malloc(sizeof(char*) * names.size()));
    for (int i = 0; i < result.count; i++)
        result.items[i] = strdup(names[static_cast<size_t>(i)].c_str());
    return result;
}

bool sq_midi_open(SqEngine engine, const char* name, char** error)
{
    std::string err;
    bool ok = cast(engine)->midiDeviceManager.openDevice(name, err);
    if (!ok)
        set_error(error, err);
    else if (error)
        *error = nullptr;
    return ok;
}

void sq_midi_close(SqEngine engine, const char* name)
{
    cast(engine)->midiDeviceManager.closeDevice(name);
}

SqStringList sq_midi_open_devices(SqEngine engine)
{
    SqStringList result = {nullptr, 0};
    auto names = cast(engine)->midiDeviceManager.getOpenDevices();
    if (names.empty()) return result;

    result.count = static_cast<int>(names.size());
    result.items = static_cast<char**>(malloc(sizeof(char*) * names.size()));
    for (int i = 0; i < result.count; i++)
        result.items[i] = strdup(names[static_cast<size_t>(i)].c_str());
    return result;
}

// --- MIDI routing ---

int sq_midi_route(SqEngine engine, const char* device, int node_id,
                  int channel_filter, int note_filter, char** error)
{
    auto& router = eng(engine).getMidiRouter();
    std::string err;
    int id = router.addRoute(device, node_id, channel_filter, note_filter, err);
    if (id < 0)
    {
        set_error(error, err);
        return -1;
    }
    router.commit();
    if (error) *error = nullptr;
    return id;
}

bool sq_midi_unroute(SqEngine engine, int route_id)
{
    auto& router = eng(engine).getMidiRouter();
    bool ok = router.removeRoute(route_id);
    if (ok)
        router.commit();
    return ok;
}

SqMidiRouteList sq_midi_routes(SqEngine engine)
{
    SqMidiRouteList result = {nullptr, 0};
    auto routes = eng(engine).getMidiRouter().getRoutes();
    if (routes.empty()) return result;

    result.count = static_cast<int>(routes.size());
    result.routes = static_cast<SqMidiRoute*>(
        malloc(sizeof(SqMidiRoute) * routes.size()));

    for (int i = 0; i < result.count; i++)
    {
        result.routes[i].id = routes[static_cast<size_t>(i)].id;
        result.routes[i].device = strdup(routes[static_cast<size_t>(i)].deviceName.c_str());
        result.routes[i].node_id = routes[static_cast<size_t>(i)].nodeId;
        result.routes[i].channel_filter = routes[static_cast<size_t>(i)].channelFilter;
        result.routes[i].note_filter = routes[static_cast<size_t>(i)].noteFilter;
    }
    return result;
}

// --- Audio device ---

bool sq_start(SqEngine engine, double sample_rate, int block_size, char** error)
{
    auto* h = cast(engine);
    std::lock_guard<std::mutex> lock(h->audioMutex);
    std::string err;
    bool ok = h->audioDevice.start(sample_rate, block_size, err);
    if (!ok)
        set_error(error, err);
    else if (error)
        *error = nullptr;
    return ok;
}

void sq_stop(SqEngine engine)
{
    auto* h = cast(engine);
    std::lock_guard<std::mutex> lock(h->audioMutex);
    h->audioDevice.stop();
}

bool sq_is_running(SqEngine engine)
{
    return cast(engine)->audioDevice.isRunning();
}

double sq_sample_rate(SqEngine engine)
{
    return cast(engine)->audioDevice.getSampleRate();
}

int sq_block_size(SqEngine engine)
{
    return cast(engine)->audioDevice.getBlockSize();
}

// --- Testing ---

void sq_prepare_for_testing(SqEngine engine, double sample_rate, int block_size)
{
    eng(engine).prepareForTesting(sample_rate, block_size);
}

void sq_render(SqEngine engine, int num_samples)
{
    eng(engine).render(num_samples);
}
