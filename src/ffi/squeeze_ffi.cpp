#include "ffi/squeeze_ffi.h"
#include "core/AudioDevice.h"
#include "core/Engine.h"
#include "core/GainProcessor.h"
#include "core/Logger.h"
#include "core/MidiDeviceManager.h"
#include "core/PluginManager.h"
#include "core/PluginProcessor.h"
#include "core/TestProcessor.h"
#include "gui/EditorManager.h"

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
    squeeze::EditorManager editorManager;
    std::mutex audioMutex;

    EngineHandle(double sr, int bs) : engine(sr, bs) {}
};

static EngineHandle* cast(SqEngine e)
{
    return static_cast<EngineHandle*>(e);
}

static squeeze::Engine& eng(SqEngine e)
{
    return cast(e)->engine;
}

// --- JUCE initialization ---

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

static squeeze::SendTap parseTap(const char* tap)
{
    if (tap && std::strcmp(tap, "pre") == 0)
        return squeeze::SendTap::preFader;
    return squeeze::SendTap::postFader;
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

// --- String / List free ---

void sq_free_string(char* s)
{
    free(s);
}

void sq_free_string_list(SqStringList list)
{
    for (int i = 0; i < list.count; i++)
        free(list.items[i]);
    free(list.items);
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

void sq_free_midi_route_list(SqMidiRouteList list)
{
    for (int i = 0; i < list.count; i++)
        free(list.routes[i].device);
    free(list.routes);
}

// ═══════════════════════════════════════════════════════════════════
// Engine lifecycle
// ═══════════════════════════════════════════════════════════════════

SqEngine sq_engine_create(double sample_rate, int block_size, char** error)
{
    ensureJuceInit();
    try
    {
        return static_cast<SqEngine>(new EngineHandle(sample_rate, block_size));
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
    cast(engine)->editorManager.closeAll();
    delete cast(engine);
}

char* sq_version(SqEngine engine)
{
    return to_c_string(eng(engine).getVersion());
}

double sq_engine_sample_rate(SqEngine engine)
{
    return eng(engine).getSampleRate();
}

int sq_engine_block_size(SqEngine engine)
{
    return eng(engine).getBlockSize();
}

// ═══════════════════════════════════════════════════════════════════
// Source management
// ═══════════════════════════════════════════════════════════════════

int sq_add_source(SqEngine engine, const char* name)
{
    auto gen = std::make_unique<squeeze::GainProcessor>();
    auto* src = eng(engine).addSource(name, std::move(gen));
    if (!src) return -1;
    return src->getHandle();
}

bool sq_remove_source(SqEngine engine, int source_handle)
{
    auto* src = eng(engine).getSource(source_handle);
    if (!src) return false;
    return eng(engine).removeSource(src);
}

int sq_source_count(SqEngine engine)
{
    return eng(engine).getSourceCount();
}

int sq_source_generator(SqEngine engine, int source_handle)
{
    auto* src = eng(engine).getSource(source_handle);
    if (!src) return -1;
    auto* gen = src->getGenerator();
    if (!gen) return -1;
    return gen->getHandle();
}

char* sq_source_name(SqEngine engine, int source_handle)
{
    auto* src = eng(engine).getSource(source_handle);
    if (!src) return to_c_string("");
    return to_c_string(src->getName());
}

float sq_source_gain(SqEngine engine, int source_handle)
{
    auto* src = eng(engine).getSource(source_handle);
    if (!src) return 0.0f;
    return src->getGain();
}

void sq_source_set_gain(SqEngine engine, int source_handle, float gain)
{
    auto* src = eng(engine).getSource(source_handle);
    if (src) src->setGain(gain);
}

float sq_source_pan(SqEngine engine, int source_handle)
{
    auto* src = eng(engine).getSource(source_handle);
    if (!src) return 0.0f;
    return src->getPan();
}

void sq_source_set_pan(SqEngine engine, int source_handle, float pan)
{
    auto* src = eng(engine).getSource(source_handle);
    if (src) src->setPan(pan);
}

bool sq_source_bypassed(SqEngine engine, int source_handle)
{
    auto* src = eng(engine).getSource(source_handle);
    if (!src) return false;
    return src->isBypassed();
}

void sq_source_set_bypassed(SqEngine engine, int source_handle, bool bypassed)
{
    auto* src = eng(engine).getSource(source_handle);
    if (src) src->setBypassed(bypassed);
}

void sq_source_midi_assign(SqEngine engine, int source_handle,
                           const char* device, int channel,
                           int note_low, int note_high)
{
    auto* src = eng(engine).getSource(source_handle);
    if (!src) return;
    squeeze::MidiAssignment assignment;
    assignment.device = device ? device : "";
    assignment.channel = channel;
    assignment.noteLow = note_low;
    assignment.noteHigh = note_high;
    src->setMidiAssignment(assignment);
}

// ═══════════════════════════════════════════════════════════════════
// Bus management
// ═══════════════════════════════════════════════════════════════════

int sq_add_bus(SqEngine engine, const char* name)
{
    auto* bus = eng(engine).addBus(name);
    if (!bus) return -1;
    return bus->getHandle();
}

bool sq_remove_bus(SqEngine engine, int bus_handle)
{
    auto* bus = eng(engine).getBus(bus_handle);
    if (!bus) return false;
    return eng(engine).removeBus(bus);
}

int sq_bus_count(SqEngine engine)
{
    return eng(engine).getBusCount();
}

int sq_master(SqEngine engine)
{
    auto* m = eng(engine).getMaster();
    return m ? m->getHandle() : -1;
}

char* sq_bus_name(SqEngine engine, int bus_handle)
{
    auto* bus = eng(engine).getBus(bus_handle);
    if (!bus) return to_c_string("");
    return to_c_string(bus->getName());
}

float sq_bus_gain(SqEngine engine, int bus_handle)
{
    auto* bus = eng(engine).getBus(bus_handle);
    if (!bus) return 0.0f;
    return bus->getGain();
}

void sq_bus_set_gain(SqEngine engine, int bus_handle, float gain)
{
    auto* bus = eng(engine).getBus(bus_handle);
    if (bus) bus->setGain(gain);
}

float sq_bus_pan(SqEngine engine, int bus_handle)
{
    auto* bus = eng(engine).getBus(bus_handle);
    if (!bus) return 0.0f;
    return bus->getPan();
}

void sq_bus_set_pan(SqEngine engine, int bus_handle, float pan)
{
    auto* bus = eng(engine).getBus(bus_handle);
    if (bus) bus->setPan(pan);
}

bool sq_bus_bypassed(SqEngine engine, int bus_handle)
{
    auto* bus = eng(engine).getBus(bus_handle);
    if (!bus) return false;
    return bus->isBypassed();
}

void sq_bus_set_bypassed(SqEngine engine, int bus_handle, bool bypassed)
{
    auto* bus = eng(engine).getBus(bus_handle);
    if (bus) bus->setBypassed(bypassed);
}

// ═══════════════════════════════════════════════════════════════════
// Routing
// ═══════════════════════════════════════════════════════════════════

void sq_route(SqEngine engine, int source_handle, int bus_handle)
{
    auto* src = eng(engine).getSource(source_handle);
    auto* bus = eng(engine).getBus(bus_handle);
    if (src && bus)
        eng(engine).route(src, bus);
}

int sq_send(SqEngine engine, int source_handle, int bus_handle, float level_db)
{
    auto* src = eng(engine).getSource(source_handle);
    auto* bus = eng(engine).getBus(bus_handle);
    if (!src || !bus) return -1;
    return eng(engine).sendFrom(src, bus, level_db);
}

void sq_remove_send(SqEngine engine, int source_handle, int send_id)
{
    auto* src = eng(engine).getSource(source_handle);
    if (src)
        eng(engine).removeSend(src, send_id);
}

void sq_set_send_level(SqEngine engine, int source_handle, int send_id, float level_db)
{
    auto* src = eng(engine).getSource(source_handle);
    if (src)
        eng(engine).setSendLevel(src, send_id, level_db);
}

void sq_set_send_tap(SqEngine engine, int source_handle, int send_id, const char* tap)
{
    auto* src = eng(engine).getSource(source_handle);
    if (src)
        src->setSendTap(send_id, parseTap(tap));
}

bool sq_bus_route(SqEngine engine, int from_handle, int to_handle)
{
    auto* from = eng(engine).getBus(from_handle);
    auto* to = eng(engine).getBus(to_handle);
    if (!from || !to) return false;
    return eng(engine).busRoute(from, to);
}

int sq_bus_send(SqEngine engine, int from_handle, int to_handle, float level_db)
{
    auto* from = eng(engine).getBus(from_handle);
    auto* to = eng(engine).getBus(to_handle);
    if (!from || !to) return -1;
    return eng(engine).busSend(from, to, level_db);
}

void sq_bus_remove_send(SqEngine engine, int bus_handle, int send_id)
{
    auto* bus = eng(engine).getBus(bus_handle);
    if (bus)
        eng(engine).busRemoveSend(bus, send_id);
}

void sq_bus_set_send_level(SqEngine engine, int bus_handle, int send_id, float level_db)
{
    auto* bus = eng(engine).getBus(bus_handle);
    if (bus)
        eng(engine).busSendLevel(bus, send_id, level_db);
}

void sq_bus_set_send_tap(SqEngine engine, int bus_handle, int send_id, const char* tap)
{
    auto* bus = eng(engine).getBus(bus_handle);
    if (bus)
        bus->setSendTap(send_id, parseTap(tap));
}

// ═══════════════════════════════════════════════════════════════════
// Source chain
// ═══════════════════════════════════════════════════════════════════

int sq_source_append_proc(SqEngine engine, int source_handle)
{
    auto* src = eng(engine).getSource(source_handle);
    if (!src) return -1;
    auto p = std::make_unique<squeeze::GainProcessor>();
    auto* proc = eng(engine).sourceAppend(src, std::move(p));
    return proc ? proc->getHandle() : -1;
}

int sq_source_insert_proc(SqEngine engine, int source_handle, int index)
{
    auto* src = eng(engine).getSource(source_handle);
    if (!src) return -1;
    auto p = std::make_unique<squeeze::GainProcessor>();
    auto* proc = eng(engine).sourceInsert(src, index, std::move(p));
    return proc ? proc->getHandle() : -1;
}

void sq_source_remove_proc(SqEngine engine, int source_handle, int index)
{
    auto* src = eng(engine).getSource(source_handle);
    if (src)
        eng(engine).sourceRemove(src, index);
}

int sq_source_chain_size(SqEngine engine, int source_handle)
{
    auto* src = eng(engine).getSource(source_handle);
    if (!src) return 0;
    return eng(engine).sourceChainSize(src);
}

// ═══════════════════════════════════════════════════════════════════
// Bus chain
// ═══════════════════════════════════════════════════════════════════

int sq_bus_append_proc(SqEngine engine, int bus_handle)
{
    auto* bus = eng(engine).getBus(bus_handle);
    if (!bus) return -1;
    auto p = std::make_unique<squeeze::GainProcessor>();
    auto* proc = eng(engine).busAppend(bus, std::move(p));
    return proc ? proc->getHandle() : -1;
}

int sq_bus_insert_proc(SqEngine engine, int bus_handle, int index)
{
    auto* bus = eng(engine).getBus(bus_handle);
    if (!bus) return -1;
    auto p = std::make_unique<squeeze::GainProcessor>();
    auto* proc = eng(engine).busInsert(bus, index, std::move(p));
    return proc ? proc->getHandle() : -1;
}

void sq_bus_remove_proc(SqEngine engine, int bus_handle, int index)
{
    auto* bus = eng(engine).getBus(bus_handle);
    if (bus)
        eng(engine).busRemove(bus, index);
}

int sq_bus_chain_size(SqEngine engine, int bus_handle)
{
    auto* bus = eng(engine).getBus(bus_handle);
    if (!bus) return 0;
    return eng(engine).busChainSize(bus);
}

// ═══════════════════════════════════════════════════════════════════
// Parameters
// ═══════════════════════════════════════════════════════════════════

float sq_get_param(SqEngine engine, int proc_handle, const char* name)
{
    return eng(engine).getParameter(proc_handle, name);
}

bool sq_set_param(SqEngine engine, int proc_handle, const char* name, float value)
{
    return eng(engine).setParameter(proc_handle, name, value);
}

char* sq_param_text(SqEngine engine, int proc_handle, const char* name)
{
    auto text = eng(engine).getParameterText(proc_handle, name);
    if (text.empty()) return nullptr;
    return to_c_string(text);
}

SqParamDescriptorList sq_param_descriptors(SqEngine engine, int proc_handle)
{
    SqParamDescriptorList result = {nullptr, 0};
    auto descs = eng(engine).getParameterDescriptors(proc_handle);
    if (descs.empty()) return result;

    result.descriptors = static_cast<SqParamDescriptor*>(
        malloc(sizeof(SqParamDescriptor) * descs.size()));
    result.count = static_cast<int>(descs.size());

    for (int i = 0; i < result.count; i++)
    {
        auto& d = descs[static_cast<size_t>(i)];
        result.descriptors[i].name = strdup(d.name.c_str());
        result.descriptors[i].default_value = d.defaultValue;
        result.descriptors[i].min_value = d.minValue;
        result.descriptors[i].max_value = d.maxValue;
        result.descriptors[i].num_steps = d.numSteps;
        result.descriptors[i].automatable = d.automatable;
        result.descriptors[i].boolean_param = d.boolean;
        result.descriptors[i].label = strdup(d.label.c_str());
        result.descriptors[i].group = strdup(d.group.c_str());
    }

    return result;
}

// ═══════════════════════════════════════════════════════════════════
// Metering
// ═══════════════════════════════════════════════════════════════════

float sq_bus_peak(SqEngine engine, int bus_handle)
{
    auto* bus = eng(engine).getBus(bus_handle);
    if (!bus) return 0.0f;
    return eng(engine).busPeak(bus);
}

float sq_bus_rms(SqEngine engine, int bus_handle)
{
    auto* bus = eng(engine).getBus(bus_handle);
    if (!bus) return 0.0f;
    return eng(engine).busRMS(bus);
}

// ═══════════════════════════════════════════════════════════════════
// Batching
// ═══════════════════════════════════════════════════════════════════

void sq_batch_begin(SqEngine engine)
{
    eng(engine).batchBegin();
}

void sq_batch_commit(SqEngine engine)
{
    eng(engine).batchCommit();
}

// ═══════════════════════════════════════════════════════════════════
// Transport
// ═══════════════════════════════════════════════════════════════════

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

// ═══════════════════════════════════════════════════════════════════
// Event scheduling
// ═══════════════════════════════════════════════════════════════════

bool sq_schedule_note_on(SqEngine engine, int source_handle, double beat_time,
                         int channel, int note, float velocity)
{
    return eng(engine).scheduleNoteOn(source_handle, beat_time, channel, note, velocity);
}

bool sq_schedule_note_off(SqEngine engine, int source_handle, double beat_time,
                          int channel, int note)
{
    return eng(engine).scheduleNoteOff(source_handle, beat_time, channel, note);
}

bool sq_schedule_cc(SqEngine engine, int source_handle, double beat_time,
                    int channel, int cc_num, int cc_val)
{
    return eng(engine).scheduleCC(source_handle, beat_time, channel, cc_num, cc_val);
}

bool sq_schedule_param_change(SqEngine engine, int proc_handle, double beat_time,
                              const char* param_name, float value)
{
    return eng(engine).scheduleParamChange(proc_handle, beat_time, param_name, value);
}

// ═══════════════════════════════════════════════════════════════════
// Plugin manager
// ═══════════════════════════════════════════════════════════════════

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
    auto proc = handle->pluginManager.createProcessor(name, sr, bs, err);
    if (!proc)
    {
        set_error(error, err);
        return -1;
    }

    if (error) *error = nullptr;
    auto* src = handle->engine.addSource(name, std::move(proc));
    if (!src) return -1;
    return src->getHandle();
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

// ═══════════════════════════════════════════════════════════════════
// MIDI device management
// ═══════════════════════════════════════════════════════════════════

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

// ═══════════════════════════════════════════════════════════════════
// MIDI routing
// ═══════════════════════════════════════════════════════════════════

int sq_midi_route(SqEngine engine, const char* device, int source_handle,
                  int channel_filter, int note_filter, char** error)
{
    auto& router = eng(engine).getMidiRouter();
    std::string err;
    int id = router.addRoute(device, source_handle, channel_filter, note_filter, err);
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
        result.routes[i].target_handle = routes[static_cast<size_t>(i)].nodeId;
        result.routes[i].channel_filter = routes[static_cast<size_t>(i)].channelFilter;
        result.routes[i].note_filter = routes[static_cast<size_t>(i)].noteFilter;
    }
    return result;
}

// ═══════════════════════════════════════════════════════════════════
// Audio device
// ═══════════════════════════════════════════════════════════════════

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

// ═══════════════════════════════════════════════════════════════════
// Plugin editor
// ═══════════════════════════════════════════════════════════════════

bool sq_open_editor(SqEngine engine, int proc_handle, char** error)
{
    auto* h = cast(engine);
    std::string err;
    bool ok = h->editorManager.open(h->engine, proc_handle, err);
    if (!ok)
        set_error(error, err);
    else if (error)
        *error = nullptr;
    return ok;
}

bool sq_close_editor(SqEngine engine, int proc_handle, char** error)
{
    auto* h = cast(engine);
    std::string err;
    bool ok = h->editorManager.close(proc_handle, err);
    if (!ok)
        set_error(error, err);
    else if (error)
        *error = nullptr;
    return ok;
}

bool sq_has_editor(SqEngine engine, int proc_handle)
{
    return cast(engine)->editorManager.hasEditor(proc_handle);
}

void sq_process_events(int timeout_ms)
{
    auto* mm = juce::MessageManager::getInstance();
    if (mm != nullptr)
        mm->runDispatchLoopUntil(timeout_ms);
}

// ═══════════════════════════════════════════════════════════════════
// Testing
// ═══════════════════════════════════════════════════════════════════

void sq_render(SqEngine engine, int num_samples)
{
    eng(engine).render(num_samples);
}
