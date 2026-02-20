#include "core/Engine.h"
#include "core/Logger.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <queue>
#include <set>

namespace squeeze {

// Helper: convert dB to linear gain
static float dbToLinear(float db)
{
    return std::pow(10.0f, db / 20.0f);
}

// Helper: apply equal-power pan to stereo buffer
static void applyPan(juce::AudioBuffer<float>& buffer, float pan, int numSamples)
{
    if (buffer.getNumChannels() < 2 || pan == 0.0f) return;

    // Equal-power pan: left = cos(angle), right = sin(angle)
    // pan in [-1, 1], center = 0
    float angle = (pan + 1.0f) * 0.5f * juce::MathConstants<float>::halfPi;
    float leftGain = std::cos(angle);
    float rightGain = std::sin(angle);

    buffer.applyGain(0, 0, numSamples, leftGain);
    buffer.applyGain(1, 0, numSamples, rightGain);
}

// ═══════════════════════════════════════════════════════════════════
// Construction / Destruction
// ═══════════════════════════════════════════════════════════════════

Engine::Engine(double sampleRate, int blockSize)
    : sampleRate_(sampleRate), blockSize_(blockSize)
{
    // Create Master bus
    auto master = std::make_unique<Bus>("Master", true);
    master->setHandle(assignHandle());
    master->prepare(sampleRate_, blockSize_);
    master_ = master.get();
    buses_.push_back(std::move(master));

    transport_.prepare(sampleRate_, blockSize_);
    perfMonitor_.prepare(sampleRate_, blockSize_);
    paramTokenNames_.reserve(kMaxParamTokens);

    buildAndSwapSnapshot();

    SQ_INFO("Engine: created sr=%.0f bs=%d masterHandle=%d",
            sampleRate_, blockSize_, master_->getHandle());
}

Engine::~Engine()
{
    delete activeSnapshot_;
    activeSnapshot_ = nullptr;
    commandQueue_.collectGarbage();
    // Destroy any remaining deferred items
    for (auto& item : pendingGarbage_)
        item.destroy();
    pendingGarbage_.clear();
    SQ_INFO("Engine: destroyed");
}

std::string Engine::getVersion() const
{
    return "0.3.0";
}

// ═══════════════════════════════════════════════════════════════════
// Garbage collection
// ═══════════════════════════════════════════════════════════════════

void Engine::collectGarbage()
{
    int count = commandQueue_.collectGarbage();
    if (count > 0)
        SQ_TRACE("Engine: collected %d garbage items", count);
    Logger::drain();
}

void Engine::deferDelete(GarbageItem item)
{
    pendingGarbage_.push_back(item);
}

// ═══════════════════════════════════════════════════════════════════
// Handle allocation + processor registry
// ═══════════════════════════════════════════════════════════════════

int Engine::assignHandle()
{
    return nextHandle_++;
}

void Engine::registerProcessor(Processor* p)
{
    if (!p) return;
    processorRegistry_[p->getHandle()] = p;
    SQ_TRACE("Engine: registered proc handle=%d name=%s",
             p->getHandle(), p->getName().c_str());
}

void Engine::unregisterProcessor(Processor* p)
{
    if (!p) return;
    processorRegistry_.erase(p->getHandle());
    SQ_TRACE("Engine: unregistered proc handle=%d name=%s",
             p->getHandle(), p->getName().c_str());
}

Processor* Engine::getProcessor(int procHandle) const
{
    std::lock_guard<std::mutex> lock(controlMutex_);
    auto it = processorRegistry_.find(procHandle);
    if (it == processorRegistry_.end()) return nullptr;
    return it->second;
}

// ═══════════════════════════════════════════════════════════════════
// Source management
// ═══════════════════════════════════════════════════════════════════

Source* Engine::addSource(const std::string& name, std::unique_ptr<Processor> generator)
{
    std::lock_guard<std::mutex> lock(controlMutex_);
    collectGarbage();

    if (!generator)
    {
        SQ_WARN("Engine::addSource: null generator");
        return nullptr;
    }

    auto src = std::make_unique<Source>(name, std::move(generator));
    src->setHandle(assignHandle());

    // Assign handle to generator and register it
    src->getGenerator()->setHandle(assignHandle());
    registerProcessor(src->getGenerator());

    src->prepare(sampleRate_, blockSize_);
    src->getGenerator()->setPlayHead(&transport_);
    src->routeTo(master_);

    Source* raw = src.get();
    sources_.push_back(std::move(src));

    SQ_DEBUG("Engine::addSource: name=%s handle=%d genHandle=%d",
             name.c_str(), raw->getHandle(), raw->getGenerator()->getHandle());

    maybeRebuildSnapshot();
    return raw;
}

bool Engine::removeSource(Source* src)
{
    std::lock_guard<std::mutex> lock(controlMutex_);
    collectGarbage();

    if (!src) return false;

    auto it = std::find_if(sources_.begin(), sources_.end(),
                           [src](const auto& p) { return p.get() == src; });
    if (it == sources_.end())
    {
        SQ_DEBUG("Engine::removeSource: not found handle=%d", src->getHandle());
        return false;
    }

    // Unregister generator
    unregisterProcessor(src->getGenerator());

    // Unregister chain processors
    auto& chain = src->getChain();
    for (int i = 0; i < chain.size(); ++i)
        unregisterProcessor(chain.at(i));

    // Remove MIDI routes targeting this source
    if (midiRouter_.removeRoutesForNode(src->getHandle()))
        midiRouter_.commit();

    SQ_DEBUG("Engine::removeSource: handle=%d name=%s", src->getHandle(), src->getName().c_str());

    // Defer deletion — audio thread may still reference this Source
    auto removed = std::move(*it);
    sources_.erase(it);
    deferDelete(GarbageItem::wrap(removed.release()));

    maybeRebuildSnapshot();
    return true;
}

Source* Engine::getSource(int handle) const
{
    std::lock_guard<std::mutex> lock(controlMutex_);
    for (const auto& s : sources_)
        if (s->getHandle() == handle) return s.get();
    return nullptr;
}

std::vector<Source*> Engine::getSources() const
{
    std::lock_guard<std::mutex> lock(controlMutex_);
    std::vector<Source*> result;
    result.reserve(sources_.size());
    for (const auto& s : sources_)
        result.push_back(s.get());
    return result;
}

int Engine::getSourceCount() const
{
    std::lock_guard<std::mutex> lock(controlMutex_);
    return static_cast<int>(sources_.size());
}

// ═══════════════════════════════════════════════════════════════════
// Bus management
// ═══════════════════════════════════════════════════════════════════

Bus* Engine::addBus(const std::string& name)
{
    std::lock_guard<std::mutex> lock(controlMutex_);
    collectGarbage();

    auto bus = std::make_unique<Bus>(name);
    bus->setHandle(assignHandle());
    bus->prepare(sampleRate_, blockSize_);
    bus->routeTo(master_);

    Bus* raw = bus.get();
    buses_.push_back(std::move(bus));

    SQ_DEBUG("Engine::addBus: name=%s handle=%d", name.c_str(), raw->getHandle());
    maybeRebuildSnapshot();
    return raw;
}

bool Engine::removeBus(Bus* bus)
{
    std::lock_guard<std::mutex> lock(controlMutex_);
    collectGarbage();

    if (!bus) return false;

    if (bus->isMaster())
    {
        SQ_WARN("Engine::removeBus: cannot remove Master");
        return false;
    }

    auto it = std::find_if(buses_.begin(), buses_.end(),
                           [bus](const auto& p) { return p.get() == bus; });
    if (it == buses_.end())
    {
        SQ_DEBUG("Engine::removeBus: not found handle=%d", bus->getHandle());
        return false;
    }

    // Unregister chain processors
    auto& chain = bus->getChain();
    for (int i = 0; i < chain.size(); ++i)
        unregisterProcessor(chain.at(i));

    // Re-route any sources that were targeting this bus to Master
    for (auto& src : sources_)
    {
        if (src->getOutputBus() == bus)
            src->routeTo(master_);
    }

    // Re-route any buses that were targeting this bus to Master
    for (auto& b : buses_)
    {
        if (b.get() != bus && b->getOutputBus() == bus)
            b->routeTo(master_);
    }

    SQ_DEBUG("Engine::removeBus: handle=%d name=%s", bus->getHandle(), bus->getName().c_str());

    // Defer deletion — audio thread may still reference this Bus
    auto removed = std::move(*it);
    buses_.erase(it);
    deferDelete(GarbageItem::wrap(removed.release()));

    maybeRebuildSnapshot();
    return true;
}

Bus* Engine::getBus(int handle) const
{
    std::lock_guard<std::mutex> lock(controlMutex_);
    for (const auto& b : buses_)
        if (b->getHandle() == handle) return b.get();
    return nullptr;
}

std::vector<Bus*> Engine::getBuses() const
{
    std::lock_guard<std::mutex> lock(controlMutex_);
    std::vector<Bus*> result;
    result.reserve(buses_.size());
    for (const auto& b : buses_)
        result.push_back(b.get());
    return result;
}

int Engine::getBusCount() const
{
    std::lock_guard<std::mutex> lock(controlMutex_);
    return static_cast<int>(buses_.size());
}

Bus* Engine::getMaster() const
{
    return master_;
}

// ═══════════════════════════════════════════════════════════════════
// Routing
// ═══════════════════════════════════════════════════════════════════

void Engine::route(Source* src, Bus* bus)
{
    std::lock_guard<std::mutex> lock(controlMutex_);
    collectGarbage();

    if (!src || !bus) return;
    SQ_DEBUG("Engine::route: source=%d -> bus=%d", src->getHandle(), bus->getHandle());
    src->routeTo(bus);
    maybeRebuildSnapshot();
}

int Engine::sendFrom(Source* src, Bus* bus, float levelDb, SendTap tap)
{
    std::lock_guard<std::mutex> lock(controlMutex_);
    collectGarbage();

    if (!src || !bus) return -1;
    SQ_DEBUG("Engine::sendFrom: source=%d -> bus=%d level=%.1f tap=%s",
             src->getHandle(), bus->getHandle(), levelDb,
             tap == SendTap::preFader ? "pre" : "post");
    int sendId = src->addSend(bus, levelDb, tap);
    maybeRebuildSnapshot();
    return sendId;
}

void Engine::removeSend(Source* src, int sendId)
{
    std::lock_guard<std::mutex> lock(controlMutex_);
    collectGarbage();

    if (!src) return;
    src->removeSend(sendId);
    maybeRebuildSnapshot();
}

void Engine::setSendLevel(Source* src, int sendId, float levelDb)
{
    std::lock_guard<std::mutex> lock(controlMutex_);
    collectGarbage();

    if (!src) return;
    src->setSendLevel(sendId, levelDb);
    maybeRebuildSnapshot();
}

void Engine::setSendTap(Source* src, int sendId, SendTap tap)
{
    std::lock_guard<std::mutex> lock(controlMutex_);
    collectGarbage();

    if (!src) return;
    SQ_DEBUG("Engine::setSendTap: source=%d sendId=%d tap=%s",
             src->getHandle(), sendId,
             tap == SendTap::preFader ? "pre" : "post");
    src->setSendTap(sendId, tap);
    maybeRebuildSnapshot();
}

bool Engine::busRoute(Bus* from, Bus* to)
{
    std::lock_guard<std::mutex> lock(controlMutex_);
    collectGarbage();

    if (!from || !to) return false;

    if (from->isMaster())
    {
        SQ_WARN("Engine::busRoute: Master cannot route to another bus");
        return false;
    }

    if (wouldCreateCycle(from, to))
    {
        SQ_WARN("Engine::busRoute: would create cycle %d -> %d",
                from->getHandle(), to->getHandle());
        return false;
    }

    SQ_DEBUG("Engine::busRoute: bus=%d -> bus=%d", from->getHandle(), to->getHandle());
    from->routeTo(to);
    maybeRebuildSnapshot();
    return true;
}

int Engine::busSend(Bus* from, Bus* to, float levelDb, SendTap tap)
{
    std::lock_guard<std::mutex> lock(controlMutex_);
    collectGarbage();

    if (!from || !to) return -1;

    if (wouldCreateCycle(from, to))
    {
        SQ_WARN("Engine::busSend: would create cycle %d -> %d",
                from->getHandle(), to->getHandle());
        return -1;
    }

    SQ_DEBUG("Engine::busSend: bus=%d -> bus=%d level=%.1f tap=%s",
             from->getHandle(), to->getHandle(), levelDb,
             tap == SendTap::preFader ? "pre" : "post");
    int sendId = from->addSend(to, levelDb, tap);
    maybeRebuildSnapshot();
    return sendId;
}

void Engine::busRemoveSend(Bus* bus, int sendId)
{
    std::lock_guard<std::mutex> lock(controlMutex_);
    collectGarbage();

    if (!bus) return;
    bus->removeSend(sendId);
    maybeRebuildSnapshot();
}

void Engine::busSendLevel(Bus* bus, int sendId, float levelDb)
{
    std::lock_guard<std::mutex> lock(controlMutex_);
    collectGarbage();

    if (!bus) return;
    bus->setSendLevel(sendId, levelDb);
    maybeRebuildSnapshot();
}

void Engine::busSendTap(Bus* bus, int sendId, SendTap tap)
{
    std::lock_guard<std::mutex> lock(controlMutex_);
    collectGarbage();

    if (!bus) return;
    SQ_DEBUG("Engine::busSendTap: bus=%d sendId=%d tap=%s",
             bus->getHandle(), sendId,
             tap == SendTap::preFader ? "pre" : "post");
    bus->setSendTap(sendId, tap);
    maybeRebuildSnapshot();
}

// ═══════════════════════════════════════════════════════════════════
// Cycle detection
// ═══════════════════════════════════════════════════════════════════

bool Engine::wouldCreateCycle(Bus* from, Bus* to) const
{
    // BFS from 'to' following downstream routing and sends.
    // If we reach 'from', a cycle would be created.
    if (from == to) return true;

    std::queue<Bus*> frontier;
    std::set<Bus*> visited;

    frontier.push(to);
    visited.insert(to);

    while (!frontier.empty())
    {
        Bus* current = frontier.front();
        frontier.pop();

        // Follow output bus routing
        Bus* downstream = current->getOutputBus();
        if (downstream)
        {
            if (downstream == from) return true;
            if (visited.find(downstream) == visited.end())
            {
                visited.insert(downstream);
                frontier.push(downstream);
            }
        }

        // Follow sends
        for (const auto& send : current->getSends())
        {
            if (send.bus == from) return true;
            if (visited.find(send.bus) == visited.end())
            {
                visited.insert(send.bus);
                frontier.push(send.bus);
            }
        }
    }

    return false;
}

// ═══════════════════════════════════════════════════════════════════
// Insert chains
// ═══════════════════════════════════════════════════════════════════

Processor* Engine::sourceAppend(Source* src, std::unique_ptr<Processor> p)
{
    std::lock_guard<std::mutex> lock(controlMutex_);
    collectGarbage();

    if (!src || !p) return nullptr;
    p->setHandle(assignHandle());
    p->prepare(sampleRate_, blockSize_);
    Processor* raw = p.get();
    raw->setPlayHead(&transport_);
    registerProcessor(raw);
    src->getChain().append(std::move(p));
    SQ_DEBUG("Engine::sourceAppend: source=%d proc=%d", src->getHandle(), raw->getHandle());
    maybeRebuildSnapshot();
    return raw;
}

Processor* Engine::sourceInsert(Source* src, int index, std::unique_ptr<Processor> p)
{
    std::lock_guard<std::mutex> lock(controlMutex_);
    collectGarbage();

    if (!src || !p) return nullptr;
    p->setHandle(assignHandle());
    p->prepare(sampleRate_, blockSize_);
    Processor* raw = p.get();
    raw->setPlayHead(&transport_);
    registerProcessor(raw);
    src->getChain().insert(index, std::move(p));
    SQ_DEBUG("Engine::sourceInsert: source=%d index=%d proc=%d",
             src->getHandle(), index, raw->getHandle());
    maybeRebuildSnapshot();
    return raw;
}

void Engine::sourceRemove(Source* src, int index)
{
    std::lock_guard<std::mutex> lock(controlMutex_);
    collectGarbage();

    if (!src) return;
    auto& chain = src->getChain();
    if (index < 0 || index >= chain.size()) return;
    Processor* p = chain.at(index);
    unregisterProcessor(p);
    auto removed = chain.remove(index);
    SQ_DEBUG("Engine::sourceRemove: source=%d index=%d", src->getHandle(), index);

    // Defer deletion — audio thread may still reference this Processor
    if (removed)
        deferDelete(GarbageItem::wrap(removed.release()));

    maybeRebuildSnapshot();
}

int Engine::sourceChainSize(Source* src) const
{
    std::lock_guard<std::mutex> lock(controlMutex_);
    if (!src) return 0;
    return src->getChain().size();
}

Processor* Engine::busAppend(Bus* bus, std::unique_ptr<Processor> p)
{
    std::lock_guard<std::mutex> lock(controlMutex_);
    collectGarbage();

    if (!bus || !p) return nullptr;
    p->setHandle(assignHandle());
    p->prepare(sampleRate_, blockSize_);
    Processor* raw = p.get();
    raw->setPlayHead(&transport_);
    registerProcessor(raw);
    bus->getChain().append(std::move(p));
    SQ_DEBUG("Engine::busAppend: bus=%d proc=%d", bus->getHandle(), raw->getHandle());
    maybeRebuildSnapshot();
    return raw;
}

Processor* Engine::busInsert(Bus* bus, int index, std::unique_ptr<Processor> p)
{
    std::lock_guard<std::mutex> lock(controlMutex_);
    collectGarbage();

    if (!bus || !p) return nullptr;
    p->setHandle(assignHandle());
    p->prepare(sampleRate_, blockSize_);
    Processor* raw = p.get();
    raw->setPlayHead(&transport_);
    registerProcessor(raw);
    bus->getChain().insert(index, std::move(p));
    SQ_DEBUG("Engine::busInsert: bus=%d index=%d proc=%d",
             bus->getHandle(), index, raw->getHandle());
    maybeRebuildSnapshot();
    return raw;
}

void Engine::busRemove(Bus* bus, int index)
{
    std::lock_guard<std::mutex> lock(controlMutex_);
    collectGarbage();

    if (!bus) return;
    auto& chain = bus->getChain();
    if (index < 0 || index >= chain.size()) return;
    Processor* p = chain.at(index);
    unregisterProcessor(p);
    auto removed = chain.remove(index);
    SQ_DEBUG("Engine::busRemove: bus=%d index=%d", bus->getHandle(), index);

    // Defer deletion — audio thread may still reference this Processor
    if (removed)
        deferDelete(GarbageItem::wrap(removed.release()));

    maybeRebuildSnapshot();
}

int Engine::busChainSize(Bus* bus) const
{
    std::lock_guard<std::mutex> lock(controlMutex_);
    if (!bus) return 0;
    return bus->getChain().size();
}

// ═══════════════════════════════════════════════════════════════════
// Parameters
// ═══════════════════════════════════════════════════════════════════

float Engine::getParameter(int procHandle, const std::string& name) const
{
    std::lock_guard<std::mutex> lock(controlMutex_);
    auto it = processorRegistry_.find(procHandle);
    if (it == processorRegistry_.end()) return 0.0f;
    return it->second->getParameter(name);
}

bool Engine::setParameter(int procHandle, const std::string& name, float value)
{
    std::lock_guard<std::mutex> lock(controlMutex_);
    auto it = processorRegistry_.find(procHandle);
    if (it == processorRegistry_.end()) return false;
    SQ_DEBUG("Engine::setParameter: proc=%d param=%s value=%f", procHandle, name.c_str(), value);
    it->second->setParameter(name, value);
    return true;
}

std::string Engine::getParameterText(int procHandle, const std::string& name) const
{
    std::lock_guard<std::mutex> lock(controlMutex_);
    auto it = processorRegistry_.find(procHandle);
    if (it == processorRegistry_.end()) return "";
    return it->second->getParameterText(name);
}

std::vector<ParamDescriptor> Engine::getParameterDescriptors(int procHandle) const
{
    std::lock_guard<std::mutex> lock(controlMutex_);
    auto it = processorRegistry_.find(procHandle);
    if (it == processorRegistry_.end()) return {};
    return it->second->getParameterDescriptors();
}

// ═══════════════════════════════════════════════════════════════════
// Metering
// ═══════════════════════════════════════════════════════════════════

float Engine::busPeak(Bus* bus) const
{
    if (!bus) return 0.0f;
    return bus->getPeak();
}

float Engine::busRMS(Bus* bus) const
{
    if (!bus) return 0.0f;
    return bus->getRMS();
}

// ═══════════════════════════════════════════════════════════════════
// Batching
// ═══════════════════════════════════════════════════════════════════

void Engine::batchBegin()
{
    std::lock_guard<std::mutex> lock(controlMutex_);
    batching_ = true;
    snapshotDirty_ = false;
    SQ_DEBUG("Engine::batchBegin");
}

void Engine::batchCommit()
{
    std::lock_guard<std::mutex> lock(controlMutex_);
    SQ_DEBUG("Engine::batchCommit: dirty=%d", snapshotDirty_);
    batching_ = false;
    if (snapshotDirty_)
    {
        snapshotDirty_ = false;
        buildAndSwapSnapshot();
    }
}

void Engine::maybeRebuildSnapshot()
{
    if (batching_)
    {
        snapshotDirty_ = true;
        return;
    }
    buildAndSwapSnapshot();
}

// ═══════════════════════════════════════════════════════════════════
// Transport (control thread → command queue → audio thread)
// ═══════════════════════════════════════════════════════════════════

void Engine::transportPlay()
{
    std::lock_guard<std::mutex> lock(controlMutex_);
    collectGarbage();
    Command cmd;
    cmd.type = Command::Type::transportPlay;
    commandQueue_.sendCommand(cmd);

    // Prime clock dispatch: compute current beat from published position
    double beats = 0.0;
    if (sampleRate_ > 0.0 && shadowTempo_ > 0.0)
    {
        int64_t samples = publishedPositionSamples_.load(std::memory_order_relaxed);
        double seconds = static_cast<double>(samples) / sampleRate_;
        beats = seconds * (shadowTempo_ / 60.0);
    }
    clockDispatch_.prime(beats, shadowTempo_, shadowLooping_,
                         shadowLoopStartBeats_, shadowLoopEndBeats_);

    SQ_DEBUG("Engine::transportPlay");
}

void Engine::transportStop()
{
    std::lock_guard<std::mutex> lock(controlMutex_);
    collectGarbage();
    Command cmd;
    cmd.type = Command::Type::transportStop;
    commandQueue_.sendCommand(cmd);
    clockDispatch_.onTransportStop();
    SQ_DEBUG("Engine::transportStop");
}

void Engine::transportPause()
{
    std::lock_guard<std::mutex> lock(controlMutex_);
    collectGarbage();
    Command cmd;
    cmd.type = Command::Type::transportPause;
    commandQueue_.sendCommand(cmd);
    SQ_DEBUG("Engine::transportPause");
}

void Engine::transportSetTempo(double bpm)
{
    std::lock_guard<std::mutex> lock(controlMutex_);
    collectGarbage();
    shadowTempo_ = std::clamp(bpm, 1.0, 999.0);
    Command cmd;
    cmd.type = Command::Type::setTempo;
    cmd.doubleValue1 = bpm;
    commandQueue_.sendCommand(cmd);
    SQ_DEBUG("Engine::transportSetTempo: bpm=%f", bpm);
}

void Engine::transportSetTimeSignature(int numerator, int denominator)
{
    std::lock_guard<std::mutex> lock(controlMutex_);
    collectGarbage();
    Command cmd;
    cmd.type = Command::Type::setTimeSignature;
    cmd.intValue1 = numerator;
    cmd.intValue2 = denominator;
    commandQueue_.sendCommand(cmd);
    SQ_DEBUG("Engine::transportSetTimeSignature: %d/%d", numerator, denominator);
}

void Engine::transportSeekSamples(int64_t samples)
{
    std::lock_guard<std::mutex> lock(controlMutex_);
    collectGarbage();
    Command cmd;
    cmd.type = Command::Type::seekSamples;
    cmd.int64Value = samples;
    commandQueue_.sendCommand(cmd);

    if (isTransportPlaying() && sampleRate_ > 0.0 && shadowTempo_ > 0.0)
    {
        double seconds = static_cast<double>(samples) / sampleRate_;
        double beats = seconds * (shadowTempo_ / 60.0);
        clockDispatch_.prime(beats, shadowTempo_, shadowLooping_,
                             shadowLoopStartBeats_, shadowLoopEndBeats_);
    }

    SQ_DEBUG("Engine::transportSeekSamples: %lld", (long long)samples);
}

void Engine::transportSeekBeats(double beats)
{
    std::lock_guard<std::mutex> lock(controlMutex_);
    collectGarbage();
    Command cmd;
    cmd.type = Command::Type::seekBeats;
    cmd.doubleValue1 = beats;
    commandQueue_.sendCommand(cmd);

    if (isTransportPlaying())
        clockDispatch_.prime(beats, shadowTempo_, shadowLooping_,
                             shadowLoopStartBeats_, shadowLoopEndBeats_);

    SQ_DEBUG("Engine::transportSeekBeats: %f", beats);
}

void Engine::transportSetLoopPoints(double startBeats, double endBeats)
{
    std::lock_guard<std::mutex> lock(controlMutex_);
    collectGarbage();
    if (endBeats > startBeats)
    {
        shadowLoopStartBeats_ = startBeats;
        shadowLoopEndBeats_ = endBeats;
    }
    Command cmd;
    cmd.type = Command::Type::setLoopPoints;
    cmd.doubleValue1 = startBeats;
    cmd.doubleValue2 = endBeats;
    commandQueue_.sendCommand(cmd);
    SQ_DEBUG("Engine::transportSetLoopPoints: %f - %f", startBeats, endBeats);
}

void Engine::transportSetLooping(bool enabled)
{
    std::lock_guard<std::mutex> lock(controlMutex_);
    collectGarbage();
    if (enabled && shadowLoopStartBeats_ == 0.0 && shadowLoopEndBeats_ == 0.0)
        shadowLooping_ = false;
    else
        shadowLooping_ = enabled;
    Command cmd;
    cmd.type = Command::Type::setLooping;
    cmd.intValue1 = enabled ? 1 : 0;
    commandQueue_.sendCommand(cmd);
    SQ_DEBUG("Engine::transportSetLooping: %s", enabled ? "true" : "false");
}

// ═══════════════════════════════════════════════════════════════════
// Transport queries (control thread)
// ═══════════════════════════════════════════════════════════════════

double Engine::getTransportPosition() const
{
    std::lock_guard<std::mutex> lock(controlMutex_);
    int64_t samples = publishedPositionSamples_.load(std::memory_order_relaxed);
    if (sampleRate_ <= 0.0) return 0.0;
    double seconds = static_cast<double>(samples) / sampleRate_;
    return seconds * (shadowTempo_ / 60.0);
}

double Engine::getTransportTempo() const
{
    std::lock_guard<std::mutex> lock(controlMutex_);
    return shadowTempo_;
}

bool Engine::isTransportPlaying() const
{
    return publishedState_.load(std::memory_order_relaxed)
           == static_cast<int>(TransportState::playing);
}

bool Engine::isTransportLooping() const
{
    std::lock_guard<std::mutex> lock(controlMutex_);
    return shadowLooping_;
}

// ═══════════════════════════════════════════════════════════════════
// Clock dispatch (control thread, no controlMutex_ needed)
// ═══════════════════════════════════════════════════════════════════

uint32_t Engine::addClock(double resolution, double latencyMs,
                           SqClockCallback callback, void* userData)
{
    return clockDispatch_.addClock(resolution, latencyMs, callback, userData);
}

void Engine::removeClock(uint32_t clockId)
{
    clockDispatch_.removeClock(clockId);
}

// ═══════════════════════════════════════════════════════════════════
// Event scheduling (control thread)
// ═══════════════════════════════════════════════════════════════════

int Engine::resolveParamToken(const std::string& name)
{
    // Search existing tokens (linear scan — small N)
    int count = static_cast<int>(paramTokenNames_.size());
    for (int i = 0; i < count; ++i)
    {
        if (paramTokenNames_[i] == name)
            return i;
    }
    if (count >= kMaxParamTokens)
    {
        SQ_WARN("Engine::resolveParamToken: token registry full, cannot resolve '%s'",
                name.c_str());
        return -1;
    }
    paramTokenNames_.push_back(name);
    return count;
}

bool Engine::scheduleNoteOn(int sourceHandle, double beatTime,
                            int channel, int note, float velocity)
{
    std::lock_guard<std::mutex> lock(controlMutex_);
    SQ_DEBUG("Engine::scheduleNoteOn: src=%d beat=%.3f ch=%d note=%d vel=%.2f",
             sourceHandle, beatTime, channel, note, velocity);
    ScheduledEvent ev{};
    ev.beatTime     = beatTime;
    ev.targetHandle = sourceHandle;
    ev.type         = ScheduledEvent::Type::noteOn;
    ev.channel      = channel;
    ev.data1        = note;
    ev.data2        = 0;
    ev.floatValue   = velocity;
    return eventScheduler_.schedule(ev);
}

bool Engine::scheduleNoteOff(int sourceHandle, double beatTime,
                             int channel, int note)
{
    std::lock_guard<std::mutex> lock(controlMutex_);
    SQ_DEBUG("Engine::scheduleNoteOff: src=%d beat=%.3f ch=%d note=%d",
             sourceHandle, beatTime, channel, note);
    ScheduledEvent ev{};
    ev.beatTime     = beatTime;
    ev.targetHandle = sourceHandle;
    ev.type         = ScheduledEvent::Type::noteOff;
    ev.channel      = channel;
    ev.data1        = note;
    ev.data2        = 0;
    ev.floatValue   = 0.0f;
    return eventScheduler_.schedule(ev);
}

bool Engine::scheduleCC(int sourceHandle, double beatTime,
                        int channel, int ccNum, int ccVal)
{
    std::lock_guard<std::mutex> lock(controlMutex_);
    SQ_DEBUG("Engine::scheduleCC: src=%d beat=%.3f ch=%d cc=%d val=%d",
             sourceHandle, beatTime, channel, ccNum, ccVal);
    ScheduledEvent ev{};
    ev.beatTime     = beatTime;
    ev.targetHandle = sourceHandle;
    ev.type         = ScheduledEvent::Type::cc;
    ev.channel      = channel;
    ev.data1        = ccNum;
    ev.data2        = ccVal;
    ev.floatValue   = 0.0f;
    return eventScheduler_.schedule(ev);
}

bool Engine::schedulePitchBend(int sourceHandle, double beatTime,
                               int channel, int value)
{
    std::lock_guard<std::mutex> lock(controlMutex_);
    SQ_DEBUG("Engine::schedulePitchBend: src=%d beat=%.3f ch=%d val=%d",
             sourceHandle, beatTime, channel, value);
    ScheduledEvent ev{};
    ev.beatTime     = beatTime;
    ev.targetHandle = sourceHandle;
    ev.type         = ScheduledEvent::Type::pitchBend;
    ev.channel      = channel;
    ev.data1        = value;
    ev.data2        = 0;
    ev.floatValue   = 0.0f;
    return eventScheduler_.schedule(ev);
}

bool Engine::scheduleParamChange(int procHandle, double beatTime,
                                 const std::string& paramName, float value)
{
    std::lock_guard<std::mutex> lock(controlMutex_);
    int token = resolveParamToken(paramName);
    if (token < 0)
        return false;
    SQ_DEBUG("Engine::scheduleParamChange: proc=%d beat=%.3f param=%s token=%d val=%.3f",
             procHandle, beatTime, paramName.c_str(), token, value);
    ScheduledEvent ev{};
    ev.beatTime     = beatTime;
    ev.targetHandle = procHandle;
    ev.type         = ScheduledEvent::Type::paramChange;
    ev.channel      = 0;
    ev.data1        = token;
    ev.data2        = 0;
    ev.floatValue   = value;
    return eventScheduler_.schedule(ev);
}

// ═══════════════════════════════════════════════════════════════════
// MixerSnapshot — build and swap
// ═══════════════════════════════════════════════════════════════════

void Engine::buildAndSwapSnapshot()
{
    auto* snapshot = new MixerSnapshot();

    // --- Sources ---
    for (const auto& src : sources_)
    {
        MixerSnapshot::SourceEntry entry;
        entry.source = src.get();
        entry.generator = src->getGenerator();
        entry.chainProcessors = src->getChain().getProcessorArray();
        entry.buffer.setSize(2, blockSize_);
        entry.buffer.clear();
        entry.outputBus = src->getOutputBus();
        entry.sends = src->getSends();
        snapshot->sources.push_back(std::move(entry));
    }

    // --- Buses: topological sort for dependency order ---
    // Build adjacency: edges from bus -> downstream (routing + sends)
    // We need reverse: in-degree based topo sort where master is last
    std::unordered_map<Bus*, std::vector<Bus*>> dependsOn; // bus -> buses that must be processed first
    std::unordered_map<Bus*, int> inDegree;

    for (const auto& b : buses_)
    {
        inDegree[b.get()] = 0;
        dependsOn[b.get()] = {};
    }

    // If bus X routes to bus Y, then Y depends on X (X must process first)
    // We track this as: Y's entry in 'dependsOn' includes X
    // But for topo sort, we need: for each bus, which buses depend on it (outgoing edges)
    std::unordered_map<Bus*, std::vector<Bus*>> feedsInto;

    for (const auto& b : buses_)
    {
        Bus* downstream = b->getOutputBus();
        if (downstream)
        {
            feedsInto[b.get()].push_back(downstream);
            inDegree[downstream]++;
        }
        for (const auto& send : b->getSends())
        {
            feedsInto[b.get()].push_back(send.bus);
            inDegree[send.bus]++;
        }
    }

    // Kahn's algorithm
    std::queue<Bus*> ready;
    for (const auto& b : buses_)
    {
        if (inDegree[b.get()] == 0)
            ready.push(b.get());
    }

    std::vector<Bus*> busOrder;
    while (!ready.empty())
    {
        Bus* current = ready.front();
        ready.pop();
        busOrder.push_back(current);

        if (feedsInto.count(current))
        {
            for (Bus* dep : feedsInto[current])
            {
                inDegree[dep]--;
                if (inDegree[dep] == 0)
                    ready.push(dep);
            }
        }
    }

    // Build bus entries in dependency order
    for (Bus* bus : busOrder)
    {
        MixerSnapshot::BusEntry entry;
        entry.bus = bus;
        entry.chainProcessors = bus->getChain().getProcessorArray();
        entry.buffer.setSize(2, blockSize_);
        entry.buffer.clear();
        entry.sends = bus->getSends();
        entry.outputBus = bus->getOutputBus();
        snapshot->buses.push_back(std::move(entry));
    }

    // Attach pending garbage to this snapshot — they'll be destroyed when
    // this snapshot is eventually garbage-collected (after the next swap).
    snapshot->attachedGarbage = std::move(pendingGarbage_);

    SQ_DEBUG("Engine::buildAndSwapSnapshot: %d sources, %d buses, %d deferred",
             static_cast<int>(snapshot->sources.size()),
             static_cast<int>(snapshot->buses.size()),
             static_cast<int>(snapshot->attachedGarbage.size()));

    Command cmd;
    cmd.type = Command::Type::swapSnapshot;
    cmd.ptr = snapshot;
    if (!commandQueue_.sendCommand(cmd))
    {
        SQ_WARN("Engine::buildAndSwapSnapshot: command queue full, deleting snapshot");
        delete snapshot;
    }
}

// ═══════════════════════════════════════════════════════════════════
// Command handling (audio thread)
// ═══════════════════════════════════════════════════════════════════

void Engine::handleCommand(const Command& cmd)
{
    switch (cmd.type)
    {
        case Command::Type::swapSnapshot:
        {
            auto* newSnapshot = static_cast<MixerSnapshot*>(cmd.ptr);
            auto* old = activeSnapshot_;
            activeSnapshot_ = newSnapshot;
            if (old)
                commandQueue_.sendGarbage(GarbageItem::wrap(old));
            SQ_TRACE_RT("Engine: swapped snapshot");
            break;
        }
        case Command::Type::transportPlay:
            transport_.play();
            publishedState_.store(static_cast<int>(transport_.getState()),
                                  std::memory_order_relaxed);
            break;
        case Command::Type::transportStop:
            transport_.stop();
            eventScheduler_.clear();
            publishedState_.store(static_cast<int>(transport_.getState()),
                                  std::memory_order_relaxed);
            publishedPositionSamples_.store(0, std::memory_order_relaxed);
            break;
        case Command::Type::transportPause:
            transport_.pause();
            publishedState_.store(static_cast<int>(transport_.getState()),
                                  std::memory_order_relaxed);
            break;
        case Command::Type::setTempo:
            transport_.setTempo(cmd.doubleValue1);
            break;
        case Command::Type::setTimeSignature:
            transport_.setTimeSignature(cmd.intValue1, cmd.intValue2);
            break;
        case Command::Type::seekSamples:
            transport_.setPositionInSamples(cmd.int64Value);
            eventScheduler_.clear();
            publishedPositionSamples_.store(transport_.getPositionInSamples(),
                                            std::memory_order_relaxed);
            break;
        case Command::Type::seekBeats:
            transport_.setPositionInBeats(cmd.doubleValue1);
            eventScheduler_.clear();
            publishedPositionSamples_.store(transport_.getPositionInSamples(),
                                            std::memory_order_relaxed);
            break;
        case Command::Type::setLoopPoints:
            transport_.setLoopPoints(cmd.doubleValue1, cmd.doubleValue2);
            publishedPositionSamples_.store(transport_.getPositionInSamples(),
                                            std::memory_order_relaxed);
            break;
        case Command::Type::setLooping:
            transport_.setLooping(cmd.intValue1 != 0);
            publishedPositionSamples_.store(transport_.getPositionInSamples(),
                                            std::memory_order_relaxed);
            break;
    }
}

// ═══════════════════════════════════════════════════════════════════
// processBlock (audio thread)
// ═══════════════════════════════════════════════════════════════════

void Engine::processBlock(float* const* outputChannels, int numChannels, int numSamples)
{
    perfMonitor_.beginBlock();

    // 1. Drain pending commands
    commandQueue_.processPending([this](const Command& cmd) { handleCommand(cmd); });

    // 2. Advance transport
    transport_.advance(numSamples);
    publishedPositionSamples_.store(transport_.getPositionInSamples(),
                                    std::memory_order_relaxed);

    // 2b. Retrieve scheduled events for this block
    int resolvedCount = 0;
    if (transport_.isPlaying() && sampleRate_ > 0.0 && transport_.getTempo() > 0.0)
    {
        if (transport_.didLoopWrap())
        {
            double blockStart = transport_.getBlockStartBeats();
            double loopEnd    = transport_.getLoopEndBeats();
            double loopStart  = transport_.getLoopStartBeats();
            double blockEnd   = transport_.getBlockEndBeats();
            double samplesPerBeat = sampleRate_ * 60.0 / transport_.getTempo();

            int preWrapSamples = static_cast<int>(
                std::round((loopEnd - blockStart) * samplesPerBeat));
            preWrapSamples = std::clamp(preWrapSamples, 0, numSamples);

            resolvedCount = eventScheduler_.retrieve(
                blockStart, loopEnd, preWrapSamples,
                transport_.getTempo(), sampleRate_,
                resolvedEvents_, kMaxResolvedEvents);

            int postWrapSamples = numSamples - preWrapSamples;
            if (postWrapSamples > 0 && resolvedCount < kMaxResolvedEvents)
            {
                int more = eventScheduler_.retrieve(
                    loopStart, blockEnd, postWrapSamples,
                    transport_.getTempo(), sampleRate_,
                    resolvedEvents_ + resolvedCount,
                    kMaxResolvedEvents - resolvedCount);
                // Offset post-wrap events to full-block position
                for (int i = 0; i < more; ++i)
                    resolvedEvents_[resolvedCount + i].sampleOffset += preWrapSamples;
                resolvedCount += more;
            }
        }
        else
        {
            resolvedCount = eventScheduler_.retrieve(
                transport_.getBlockStartBeats(),
                transport_.getBlockEndBeats(),
                numSamples,
                transport_.getTempo(), sampleRate_,
                resolvedEvents_, kMaxResolvedEvents);
        }
    }

    // 2c. Push beat range to ClockDispatch
    if (transport_.isPlaying() && sampleRate_ > 0.0 && transport_.getTempo() > 0.0)
    {
        if (transport_.didLoopWrap())
        {
            double blockStart = transport_.getBlockStartBeats();
            double loopEnd    = transport_.getLoopEndBeats();
            double loopStart  = transport_.getLoopStartBeats();
            double blockEnd   = transport_.getBlockEndBeats();
            double tempo      = transport_.getTempo();
            bool   looping    = transport_.isLooping();

            clockDispatch_.pushBeatRange({blockStart, loopEnd, tempo, looping, loopStart, loopEnd});
            clockDispatch_.pushBeatRange({loopStart, blockEnd, tempo, looping, loopStart, loopEnd});
        }
        else
        {
            clockDispatch_.pushBeatRange({
                transport_.getBlockStartBeats(),
                transport_.getBlockEndBeats(),
                transport_.getTempo(),
                transport_.isLooping(),
                transport_.getLoopStartBeats(),
                transport_.getLoopEndBeats()
            });
        }
    }

    // 3. If no snapshot, output silence
    if (!activeSnapshot_)
    {
        for (int ch = 0; ch < numChannels; ++ch)
            std::memset(outputChannels[ch], 0, sizeof(float) * static_cast<size_t>(numSamples));
        perfMonitor_.endBlock();
        return;
    }

    // 3. Clear all bus buffers
    for (auto& busEntry : activeSnapshot_->buses)
        busEntry.buffer.clear();

    // 4. Clear MIDI buffers and dispatch MIDI via MidiRouter
    std::unordered_map<int, juce::MidiBuffer*> midiBufferMap;
    for (auto& srcEntry : activeSnapshot_->sources)
    {
        srcEntry.midiBuffer.clear();
        midiBufferMap[srcEntry.source->getHandle()] = &srcEntry.midiBuffer;
    }
    midiRouter_.dispatch(midiBufferMap, numSamples);

    // 4b. Dispatch resolved scheduled events
    for (int i = 0; i < resolvedCount; ++i)
    {
        auto& ev = resolvedEvents_[i];
        switch (ev.type)
        {
            case ScheduledEvent::Type::noteOn:
            {
                int vel = std::clamp(static_cast<int>(ev.floatValue * 127.0f + 0.5f), 0, 127);
                auto msg = juce::MidiMessage::noteOn(ev.channel, ev.data1,
                                                      static_cast<juce::uint8>(vel));
                auto it = midiBufferMap.find(ev.targetHandle);
                if (it != midiBufferMap.end())
                    it->second->addEvent(msg, ev.sampleOffset);
                break;
            }
            case ScheduledEvent::Type::noteOff:
            {
                auto msg = juce::MidiMessage::noteOff(ev.channel, ev.data1);
                auto it = midiBufferMap.find(ev.targetHandle);
                if (it != midiBufferMap.end())
                    it->second->addEvent(msg, ev.sampleOffset);
                break;
            }
            case ScheduledEvent::Type::cc:
            {
                auto msg = juce::MidiMessage::controllerEvent(ev.channel, ev.data1, ev.data2);
                auto it = midiBufferMap.find(ev.targetHandle);
                if (it != midiBufferMap.end())
                    it->second->addEvent(msg, ev.sampleOffset);
                break;
            }
            case ScheduledEvent::Type::pitchBend:
            {
                auto msg = juce::MidiMessage::pitchWheel(ev.channel, ev.data1);
                auto it = midiBufferMap.find(ev.targetHandle);
                if (it != midiBufferMap.end())
                    it->second->addEvent(msg, ev.sampleOffset);
                break;
            }
            case ScheduledEvent::Type::paramChange:
            {
                // Look up processor in snapshot by handle
                if (ev.data1 >= 0 && ev.data1 < static_cast<int>(paramTokenNames_.size()))
                {
                    const std::string& paramName = paramTokenNames_[ev.data1];
                    // Search sources (generators and chain processors)
                    Processor* target = nullptr;
                    for (auto& srcEntry : activeSnapshot_->sources)
                    {
                        if (srcEntry.generator && srcEntry.generator->getHandle() == ev.targetHandle)
                        {
                            target = srcEntry.generator;
                            break;
                        }
                        for (auto* proc : srcEntry.chainProcessors)
                        {
                            if (proc->getHandle() == ev.targetHandle)
                            {
                                target = proc;
                                break;
                            }
                        }
                        if (target) break;
                    }
                    // Search bus chain processors
                    if (!target)
                    {
                        for (auto& busEntry : activeSnapshot_->buses)
                        {
                            for (auto* proc : busEntry.chainProcessors)
                            {
                                if (proc->getHandle() == ev.targetHandle)
                                {
                                    target = proc;
                                    break;
                                }
                            }
                            if (target) break;
                        }
                    }
                    if (target)
                        target->setParameter(paramName, ev.floatValue);
                }
                break;
            }
        }
    }

    // 5. Process sources
    int perfSlot = 0;
    for (auto& srcEntry : activeSnapshot_->sources)
    {
        perfMonitor_.beginSlot(perfSlot, srcEntry.source->getHandle());
        auto& buffer = srcEntry.buffer;
        buffer.clear();

        // Generator
        if (srcEntry.generator)
            srcEntry.generator->process(buffer, srcEntry.midiBuffer);

        // Chain processors
        for (auto* proc : srcEntry.chainProcessors)
        {
            if (!proc->isBypassed())
                proc->process(buffer);
        }

        // Pre-fader sends
        for (const auto& send : srcEntry.sends)
        {
            if (send.tap == SendTap::preFader && send.bus)
            {
                float gain = dbToLinear(send.levelDb);
                // Find the bus entry buffer and accumulate
                for (auto& busEntry : activeSnapshot_->buses)
                {
                    if (busEntry.bus == send.bus)
                    {
                        int channels = std::min(buffer.getNumChannels(),
                                                busEntry.buffer.getNumChannels());
                        for (int ch = 0; ch < channels; ++ch)
                            busEntry.buffer.addFrom(ch, 0, buffer, ch, 0, numSamples, gain);
                        break;
                    }
                }
            }
        }

        // Apply gain and pan
        float srcGain = srcEntry.source->getGain();
        buffer.applyGain(0, numSamples, srcGain);
        applyPan(buffer, srcEntry.source->getPan(), numSamples);

        // Post-fader sends
        for (const auto& send : srcEntry.sends)
        {
            if (send.tap == SendTap::postFader && send.bus)
            {
                float gain = dbToLinear(send.levelDb);
                for (auto& busEntry : activeSnapshot_->buses)
                {
                    if (busEntry.bus == send.bus)
                    {
                        int channels = std::min(buffer.getNumChannels(),
                                                busEntry.buffer.getNumChannels());
                        for (int ch = 0; ch < channels; ++ch)
                            busEntry.buffer.addFrom(ch, 0, buffer, ch, 0, numSamples, gain);
                        break;
                    }
                }
            }
        }

        // Accumulate into output bus
        if (srcEntry.outputBus)
        {
            for (auto& busEntry : activeSnapshot_->buses)
            {
                if (busEntry.bus == srcEntry.outputBus)
                {
                    int channels = std::min(buffer.getNumChannels(),
                                            busEntry.buffer.getNumChannels());
                    for (int ch = 0; ch < channels; ++ch)
                        busEntry.buffer.addFrom(ch, 0, buffer, ch, 0, numSamples);
                    break;
                }
            }
        }

        perfMonitor_.endSlot(perfSlot);
        ++perfSlot;
    }

    // 6. Process buses in dependency order
    for (auto& busEntry : activeSnapshot_->buses)
    {
        perfMonitor_.beginSlot(perfSlot, busEntry.bus->getHandle());
        auto& buffer = busEntry.buffer;

        // Chain processors
        for (auto* proc : busEntry.chainProcessors)
        {
            if (!proc->isBypassed())
                proc->process(buffer);
        }

        // Pre-fader sends
        for (const auto& send : busEntry.sends)
        {
            if (send.tap == SendTap::preFader && send.bus)
            {
                float gain = dbToLinear(send.levelDb);
                for (auto& targetEntry : activeSnapshot_->buses)
                {
                    if (targetEntry.bus == send.bus)
                    {
                        int channels = std::min(buffer.getNumChannels(),
                                                targetEntry.buffer.getNumChannels());
                        for (int ch = 0; ch < channels; ++ch)
                            targetEntry.buffer.addFrom(ch, 0, buffer, ch, 0, numSamples, gain);
                        break;
                    }
                }
            }
        }

        // Apply gain and pan
        float busGain = busEntry.bus->getGain();
        buffer.applyGain(0, numSamples, busGain);
        applyPan(buffer, busEntry.bus->getPan(), numSamples);

        // Post-fader sends
        for (const auto& send : busEntry.sends)
        {
            if (send.tap == SendTap::postFader && send.bus)
            {
                float gain = dbToLinear(send.levelDb);
                for (auto& targetEntry : activeSnapshot_->buses)
                {
                    if (targetEntry.bus == send.bus)
                    {
                        int channels = std::min(buffer.getNumChannels(),
                                                targetEntry.buffer.getNumChannels());
                        for (int ch = 0; ch < channels; ++ch)
                            targetEntry.buffer.addFrom(ch, 0, buffer, ch, 0, numSamples, gain);
                        break;
                    }
                }
            }
        }

        // Metering
        busEntry.bus->updateMetering(buffer, numSamples);

        // Accumulate into downstream bus
        if (busEntry.outputBus)
        {
            for (auto& targetEntry : activeSnapshot_->buses)
            {
                if (targetEntry.bus == busEntry.outputBus)
                {
                    int channels = std::min(buffer.getNumChannels(),
                                            targetEntry.buffer.getNumChannels());
                    for (int ch = 0; ch < channels; ++ch)
                        targetEntry.buffer.addFrom(ch, 0, buffer, ch, 0, numSamples);
                    break;
                }
            }
        }

        perfMonitor_.endSlot(perfSlot);
        ++perfSlot;
    }

    // 7. Copy master bus buffer to output
    Bus* masterBus = master_;
    for (auto& busEntry : activeSnapshot_->buses)
    {
        if (busEntry.bus == masterBus)
        {
            int channels = std::min(numChannels, busEntry.buffer.getNumChannels());
            int samples = std::min(numSamples, busEntry.buffer.getNumSamples());
            for (int ch = 0; ch < channels; ++ch)
                std::memcpy(outputChannels[ch], busEntry.buffer.getReadPointer(ch),
                            sizeof(float) * static_cast<size_t>(samples));
            // Zero any extra output channels
            for (int ch = channels; ch < numChannels; ++ch)
                std::memset(outputChannels[ch], 0, sizeof(float) * static_cast<size_t>(numSamples));
            perfMonitor_.endBlock();
            return;
        }
    }

    // Fallback: silence
    for (int ch = 0; ch < numChannels; ++ch)
        std::memset(outputChannels[ch], 0, sizeof(float) * static_cast<size_t>(numSamples));

    perfMonitor_.endBlock();
}

// ═══════════════════════════════════════════════════════════════════
// Accessors
// ═══════════════════════════════════════════════════════════════════

double Engine::getSampleRate() const
{
    return sampleRate_;
}

int Engine::getBlockSize() const
{
    return blockSize_;
}

MidiRouter& Engine::getMidiRouter()
{
    return midiRouter_;
}

PerfMonitor& Engine::getPerfMonitor()
{
    return perfMonitor_;
}

// ═══════════════════════════════════════════════════════════════════
// Testing
// ═══════════════════════════════════════════════════════════════════

void Engine::render(int numSamples)
{
    std::lock_guard<std::mutex> lock(controlMutex_);

    // Drain commands synchronously
    commandQueue_.processPending([this](const Command& cmd) { handleCommand(cmd); });

    juce::AudioBuffer<float> outputBuffer(2, numSamples);
    outputBuffer.clear();

    float* channels[2] = {outputBuffer.getWritePointer(0),
                          outputBuffer.getWritePointer(1)};
    processBlock(channels, 2, numSamples);

    SQ_TRACE("Engine::render: %d samples", numSamples);
}

} // namespace squeeze
