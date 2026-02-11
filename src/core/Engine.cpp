#include "core/Engine.h"
#include "core/Buffer.h"
#include "core/Logger.h"
#include "core/PluginNode.h"
#include "core/SamplerNode.h"

#include <unordered_map>
#include <unordered_set>

namespace squeeze {

Engine::Engine(Scheduler& scheduler)
    : scheduler_(scheduler)
{
    formatManager_.addDefaultFormats();
    audioFormatManager_.registerBasicFormats();
}

Engine::~Engine()
{
    stop();
    delete activeSnapshot_;
}

bool Engine::start(double sampleRate, int blockSize)
{
    SQ_LOG("start: sr=%.0f bs=%d", sampleRate, blockSize);

    // Try to open a real audio device
    juce::AudioDeviceManager::AudioDeviceSetup setup;
    setup.sampleRate = sampleRate;
    setup.bufferSize = blockSize;

    auto err = deviceManager_.initialise(0, 2, nullptr, true, {}, &setup);
    if (err.isEmpty())
    {
        deviceManager_.addAudioCallback(this);
        // audioDeviceAboutToStart has now fired, setting actual sr/bs
        // Rebuild snapshot with correct values
        updateGraph();
        SQ_LOG("start: audio device opened (sr=%.0f bs=%d)",
               sampleRate_.load(), blockSize_.load());
        return true;
    }

    // No device available (testing or headless)
    SQ_LOG("start: no audio device: %s", err.toRawUTF8());
    sampleRate_.store(sampleRate);
    blockSize_.store(blockSize);
    running_.store(true);
    return true;
}

void Engine::stop()
{
    SQ_LOG("stop");
    if (deviceManager_.getCurrentAudioDevice() != nullptr)
    {
        deviceManager_.removeAudioCallback(this);
        deviceManager_.closeAudioDevice();
    }
    running_.store(false);
}

bool Engine::isRunning() const { return running_.load(); }
double Engine::getSampleRate() const { return sampleRate_.load(); }
int Engine::getBlockSize() const { return blockSize_.load(); }

void Engine::prepareForTesting(double sampleRate, int blockSize)
{
    std::lock_guard<std::mutex> lock(controlMutex_);
    sampleRate_.store(sampleRate);
    blockSize_.store(blockSize);
    perfMonitor_.prepare(sampleRate, blockSize);
    transport_.setSampleRate(sampleRate);
}

PerfMonitor& Engine::getPerfMonitor()
{
    return perfMonitor_;
}

PerfSnapshot Engine::getPerfSnapshot()
{
    std::lock_guard<std::mutex> lock(controlMutex_);
    auto snap = perfMonitor_.getSnapshot();

    // Fill in MIDI device names from MidiRouter
    auto stats = midiRouter_.getDeviceStats();
    snap.midi.clear();
    for (const auto& s : stats)
    {
        PerfSnapshot::MidiQueuePerf mp;
        mp.nodeId = -1;
        mp.deviceName = s.deviceName;
        mp.fillLevel = s.fillLevel;
        mp.peakFillLevel = 0;
        mp.droppedCount = s.droppedCount;
        snap.midi.push_back(mp);
    }

    return snap;
}

Graph& Engine::getGraph()
{
    return graph_;
}

MidiRouter& Engine::getMidiRouter()
{
    return midiRouter_;
}

Transport& Engine::getTransport()
{
    return transport_;
}

EventQueue& Engine::getEventQueue()
{
    return eventQueue_;
}

void Engine::transportPlay()
{
    Command cmd;
    cmd.type = Command::Type::transportPlay;
    scheduler_.sendCommand(cmd);
}

void Engine::transportStop()
{
    Command cmd;
    cmd.type = Command::Type::transportStop;
    scheduler_.sendCommand(cmd);
}

void Engine::transportPause()
{
    Command cmd;
    cmd.type = Command::Type::transportPause;
    scheduler_.sendCommand(cmd);
}

void Engine::transportSetTempo(double bpm)
{
    Command cmd;
    cmd.type = Command::Type::transportSetTempo;
    cmd.doubleValue1 = bpm;
    scheduler_.sendCommand(cmd);
}

void Engine::transportSetTimeSignature(int numerator, int denominator)
{
    Command cmd;
    cmd.type = Command::Type::transportSetTimeSig;
    cmd.intValue1 = numerator;
    cmd.intValue2 = denominator;
    scheduler_.sendCommand(cmd);
}

void Engine::transportSetPositionInSamples(int64_t samples)
{
    Command cmd;
    cmd.type = Command::Type::transportSetPositionSamples;
    cmd.int64Value = samples;
    scheduler_.sendCommand(cmd);
}

void Engine::transportSetPositionInBeats(double beats)
{
    Command cmd;
    cmd.type = Command::Type::transportSetPositionBeats;
    cmd.doubleValue1 = beats;
    scheduler_.sendCommand(cmd);
}

void Engine::transportSetLoopPoints(double startBeats, double endBeats)
{
    Command cmd;
    cmd.type = Command::Type::transportSetLoopPoints;
    cmd.doubleValue1 = startBeats;
    cmd.doubleValue2 = endBeats;
    scheduler_.sendCommand(cmd);
}

void Engine::transportSetLooping(bool enabled)
{
    Command cmd;
    cmd.type = Command::Type::transportSetLooping;
    cmd.intValue1 = enabled ? 1 : 0;
    scheduler_.sendCommand(cmd);
}

// ============================================================
// Plugin cache
// ============================================================

bool Engine::loadPluginCache(const std::string& xmlPath)
{
    std::lock_guard<std::mutex> lock(controlMutex_);
    return cache_.loadFromFile(juce::File(xmlPath));
}

std::vector<std::string> Engine::getAvailablePluginNames() const
{
    std::lock_guard<std::mutex> lock(controlMutex_);
    auto names = cache_.getAvailablePluginNames();
    std::vector<std::string> result;
    result.reserve(names.size());
    for (const auto& n : names)
        result.push_back(n.toStdString());
    return result;
}

const juce::PluginDescription* Engine::findPluginByName(const std::string& name) const
{
    std::lock_guard<std::mutex> lock(controlMutex_);
    return cache_.findByName(juce::String(name));
}

// ============================================================
// Node management
// ============================================================

int Engine::addNodeLocked(std::unique_ptr<Node> node, const std::string& name)
{
    auto* pn = dynamic_cast<PluginNode*>(node.get());
    if (pn)
        pn->getProcessor()->setPlayHead(&transport_);

    Node* raw = node.get();
    int id = graph_.addNode(raw);
    ownedNodes_[id] = std::move(node);
    nodeNames_[id] = name;
    return id;
}

int Engine::addNode(std::unique_ptr<Node> node, const std::string& name)
{
    std::lock_guard<std::mutex> lock(controlMutex_);
    return addNodeLocked(std::move(node), name);
}

bool Engine::removeNode(int id)
{
    std::lock_guard<std::mutex> lock(controlMutex_);
    SQ_LOG("removeNode: id=%d", id);
    auto it = ownedNodes_.find(id);
    if (it == ownedNodes_.end())
        return false;

    graph_.removeNode(id);

    // Remove any MIDI routes targeting this node
    midiRouter_.removeRoutesForNode(id);

    // Defer destruction for RT safety
    pendingDeletions_.push_back(std::move(it->second));
    ownedNodes_.erase(it);
    nodeNames_.erase(id);

    // Push updated graph so audio thread stops referencing removed node
    updateGraphLocked();

    return true;
}

Node* Engine::getNode(int id) const
{
    std::lock_guard<std::mutex> lock(controlMutex_);
    return graph_.getNode(id);
}

std::string Engine::getNodeName(int id) const
{
    std::lock_guard<std::mutex> lock(controlMutex_);
    auto it = nodeNames_.find(id);
    return (it != nodeNames_.end()) ? it->second : "";
}

std::vector<std::pair<int, std::string>> Engine::getNodes() const
{
    std::lock_guard<std::mutex> lock(controlMutex_);
    std::vector<std::pair<int, std::string>> result;
    for (const auto& kv : ownedNodes_)
    {
        auto nameIt = nodeNames_.find(kv.first);
        std::string name = (nameIt != nodeNames_.end()) ? nameIt->second : "unknown";
        result.push_back({kv.first, name});
    }
    return result;
}

// ============================================================
// Plugin instantiation
// ============================================================

int Engine::addPlugin(const std::string& name, std::string& errorMessage)
{
    std::lock_guard<std::mutex> lock(controlMutex_);
    SQ_LOG("addPlugin: %s", name.c_str());
    auto* desc = cache_.findByName(juce::String(name));
    if (!desc)
    {
        errorMessage = "Plugin '" + name + "' not found in cache";
        return -1;
    }

    double sr = sampleRate_.load();
    int bs = blockSize_.load();
    if (sr <= 0.0) sr = 44100.0;
    if (bs <= 0) bs = 512;

    juce::String juceError;
    auto pluginNode = PluginNode::create(*desc, formatManager_, sr, bs, juceError);
    if (!pluginNode)
    {
        errorMessage = "Failed to create plugin: " + juceError.toStdString();
        return -1;
    }

    return addNodeLocked(std::move(pluginNode), name);
}

// ============================================================
// Sampler management
// ============================================================

int Engine::addSampler(const std::string& name, int maxVoices, std::string& errorMessage)
{
    std::lock_guard<std::mutex> lock(controlMutex_);
    SQ_LOG("addSampler: '%s' maxVoices=%d", name.c_str(), maxVoices);

    if (maxVoices < 1)
    {
        errorMessage = "maxVoices must be >= 1";
        return -1;
    }

    auto node = std::make_unique<SamplerNode>(maxVoices);

    double sr = sampleRate_.load();
    int bs = blockSize_.load();
    if (sr > 0.0 && bs > 0)
        node->prepare(sr, bs);

    return addNodeLocked(std::move(node), name);
}

bool Engine::setSamplerBuffer(int nodeId, int bufferId)
{
    std::lock_guard<std::mutex> lock(controlMutex_);
    SQ_LOG("setSamplerBuffer: node=%d buffer=%d", nodeId, bufferId);

    Node* raw = graph_.getNode(nodeId);
    if (!raw)
        return false;

    auto* sampler = dynamic_cast<SamplerNode*>(raw);
    if (!sampler)
        return false;

    if (bufferId < 0)
    {
        sampler->setBuffer(nullptr);
        return true;
    }

    auto it = ownedBuffers_.find(bufferId);
    if (it == ownedBuffers_.end())
        return false;

    sampler->setBuffer(it->second.get());
    return true;
}

// ============================================================
// Graph topology
// ============================================================

int Engine::connect(int srcId, const std::string& srcPort,
                    int dstId, const std::string& dstPort, std::string& error)
{
    std::lock_guard<std::mutex> lock(controlMutex_);
    SQ_LOG("connect: %d:%s -> %d:%s", srcId, srcPort.c_str(), dstId, dstPort.c_str());
    PortAddress source{srcId, PortDirection::output, srcPort};
    PortAddress dest{dstId, PortDirection::input, dstPort};

    int connId = graph_.connect(source, dest);
    if (connId < 0)
    {
        error = graph_.getLastError();
        return -1;
    }

    return connId;
}

bool Engine::disconnect(int connId)
{
    std::lock_guard<std::mutex> lock(controlMutex_);
    SQ_LOG("disconnect: conn=%d", connId);
    return graph_.disconnect(connId);
}

std::vector<Connection> Engine::getConnections() const
{
    std::lock_guard<std::mutex> lock(controlMutex_);
    return graph_.getConnections();
}

// ============================================================
// Graph push
// ============================================================

void Engine::updateGraphLocked()
{
    updateGraphLocked(graph_);
}

void Engine::updateGraphLocked(const Graph& graph)
{
    SQ_LOG("updateGraph: %d nodes", graph.getNodeCount());
    double sr = sampleRate_.load();
    int bs = blockSize_.load();

    auto* snapshot = buildSnapshot(graph, sr, bs);

    Command cmd;
    cmd.type = Command::Type::swapGraph;
    cmd.ptr = snapshot;
    if (!scheduler_.sendCommand(cmd))
    {
        SQ_LOG("updateGraph: command queue full, snapshot dropped");
        delete snapshot;
    }
}

void Engine::updateGraph()
{
    std::lock_guard<std::mutex> lock(controlMutex_);
    updateGraphLocked();
}

void Engine::updateGraph(const Graph& graph)
{
    std::lock_guard<std::mutex> lock(controlMutex_);
    updateGraphLocked(graph);
}

// ============================================================
// Parameters
// ============================================================

bool Engine::setParameter(int nodeId, int paramIndex, float value)
{
    std::lock_guard<std::mutex> lock(controlMutex_);
    Node* node = graph_.getNode(nodeId);
    if (!node)
        return false;

    node->setParameter(paramIndex, value);
    return true;
}

float Engine::getParameter(int nodeId, int paramIndex) const
{
    std::lock_guard<std::mutex> lock(controlMutex_);
    Node* node = graph_.getNode(nodeId);
    if (!node)
        return 0.0f;

    return node->getParameter(paramIndex);
}

bool Engine::setParameterByName(int nodeId, const std::string& name, float value)
{
    std::lock_guard<std::mutex> lock(controlMutex_);
    Node* node = graph_.getNode(nodeId);
    if (!node)
        return false;

    return node->setParameterByName(name, value);
}

float Engine::getParameterByName(int nodeId, const std::string& name) const
{
    std::lock_guard<std::mutex> lock(controlMutex_);
    Node* node = graph_.getNode(nodeId);
    if (!node)
        return 0.0f;

    return node->getParameterByName(name);
}

std::vector<ParameterDescriptor> Engine::getParameterDescriptors(int nodeId) const
{
    std::lock_guard<std::mutex> lock(controlMutex_);
    Node* node = graph_.getNode(nodeId);
    if (!node)
        return {};

    return node->getParameterDescriptors();
}

std::string Engine::getParameterText(int nodeId, int paramIndex) const
{
    std::lock_guard<std::mutex> lock(controlMutex_);
    Node* node = graph_.getNode(nodeId);
    if (!node)
        return "";

    return node->getParameterText(paramIndex);
}

// ============================================================
// Snapshot building
// ============================================================

GraphSnapshot* Engine::buildSnapshot(const Graph& graph,
                                     double sampleRate, int blockSize)
{
    auto order = graph.getExecutionOrder();
    auto connections = graph.getConnections();

    SQ_LOG("buildSnapshot: %d nodes, sr=%.0f bs=%d",
           (int)order.size(), sampleRate, blockSize);

    auto* snap = new GraphSnapshot();

    if (order.empty())
        return snap;

    // Map nodeId to index in slots
    std::unordered_map<int, int> idToIndex;
    for (int i = 0; i < (int)order.size(); ++i)
        idToIndex[order[i]] = i;

    // Determine max channel count for silence buffer
    int maxChannels = 2;

    for (int i = 0; i < (int)order.size(); ++i)
    {
        int nodeId = order[i];
        Node* node = graph.getNode(nodeId);

        int audioSrc = -1;

        // Find incoming audio connections for this node
        for (const auto& conn : connections)
        {
            if (conn.dest.nodeId != nodeId)
                continue;

            Node* srcNode = graph.getNode(conn.source.nodeId);
            if (!srcNode) continue;

            for (const auto& p : srcNode->getOutputPorts())
            {
                if (p.name == conn.source.portName)
                {
                    if (p.signalType == SignalType::audio)
                        audioSrc = idToIndex[conn.source.nodeId];
                    break;
                }
            }
        }

        snap->slots.push_back({node, nodeId, audioSrc, false});

        // Determine output channel count from this node's output ports
        int outChannels = 2;
        for (const auto& p : node->getOutputPorts())
        {
            if (p.signalType == SignalType::audio)
            {
                outChannels = p.channels;
                break;
            }
        }

        if (outChannels > maxChannels)
            maxChannels = outChannels;

        snap->audioOutputs.emplace_back(outChannels, blockSize);
        snap->midiBuffers.emplace_back();
        snap->midiBuffers.back().ensureSize(256);
    }

    snap->silenceBuffer.setSize(maxChannels, blockSize);
    snap->silenceBuffer.clear();
    snap->scratchMidi.ensureSize(512);

    // Determine which slots are audio leaves (no other slot reads their audio output)
    std::unordered_set<int> hasAudioConsumer;
    for (const auto& slot : snap->slots)
    {
        if (slot.audioSourceIndex >= 0)
            hasAudioConsumer.insert(slot.audioSourceIndex);
    }

    for (int i = 0; i < (int)snap->slots.size(); ++i)
    {
        // A leaf is a node that has an audio output port but no one reads it
        bool hasAudioOutput = false;
        for (const auto& p : snap->slots[i].node->getOutputPorts())
        {
            if (p.signalType == SignalType::audio)
            {
                hasAudioOutput = true;
                break;
            }
        }
        snap->slots[i].isAudioLeaf = hasAudioOutput && !hasAudioConsumer.count(i);
    }

    return snap;
}

// ============================================================
// Audio processing
// ============================================================

void Engine::processBlock(juce::AudioBuffer<float>& outputBuffer,
                          juce::MidiBuffer& outputMidi,
                          int numSamples)
{
    perfMonitor_.beginCallback();

    // 1. Drain scheduler
    scheduler_.processPending([this](const Command& cmd) {
        switch (cmd.type)
        {
            case Command::Type::swapGraph:
            {
                auto* oldSnapshot = activeSnapshot_;
                activeSnapshot_ = static_cast<GraphSnapshot*>(cmd.ptr);
                SQ_LOG_RT("snapshot swap");
                if (oldSnapshot)
                    scheduler_.sendGarbage(GarbageItem::wrap(oldSnapshot));
                break;
            }
            case Command::Type::setParameter:
            {
                if (cmd.node)
                    cmd.node->setParameter(cmd.paramIndex, cmd.paramValue);
                break;
            }
            case Command::Type::transportPlay:
                transport_.play();
                break;
            case Command::Type::transportStop:
                transport_.stop();
                eventQueue_.clear();
                break;
            case Command::Type::transportPause:
                transport_.pause();
                break;
            case Command::Type::transportSetTempo:
                transport_.setTempo(cmd.doubleValue1);
                break;
            case Command::Type::transportSetTimeSig:
                transport_.setTimeSignature(cmd.intValue1, cmd.intValue2);
                break;
            case Command::Type::transportSetPositionSamples:
                transport_.setPositionInSamples(cmd.int64Value);
                eventQueue_.clear();
                break;
            case Command::Type::transportSetPositionBeats:
                transport_.setPositionInBeats(cmd.doubleValue1);
                eventQueue_.clear();
                break;
            case Command::Type::transportSetLoopPoints:
                transport_.setLoopPoints(cmd.doubleValue1, cmd.doubleValue2);
                break;
            case Command::Type::transportSetLooping:
                transport_.setLooping(cmd.intValue1 != 0);
                break;
        }
    });

    // 2. Advance transport
    transport_.advance(numSamples);

    // 2b. Retrieve resolved events from EventQueue
    ResolvedEvent resolvedEvents[kMaxResolvedEvents];
    int resolvedCount = 0;
    if (transport_.isPlaying() && transport_.getSampleRate() > 0.0)
    {
        resolvedCount = eventQueue_.retrieve(
            transport_.getBlockStartBeats(), transport_.getBlockEndBeats(),
            transport_.isLooping(),
            transport_.getLoopStartBeats(), transport_.getLoopEndBeats(),
            numSamples, transport_.getTempo(), transport_.getSampleRate(),
            resolvedEvents, kMaxResolvedEvents);
    }

    // 3. If no snapshot, output silence
    if (!activeSnapshot_ || activeSnapshot_->slots.empty())
    {
        outputBuffer.clear();
        outputMidi.clear();
        perfMonitor_.endCallback();
        return;
    }

    auto& snap = *activeSnapshot_;

    // 4. Clear all node MIDI buffers, then dispatch MIDI from router
    std::unordered_map<int, juce::MidiBuffer*> nodeBufferMap;
    for (int i = 0; i < (int)snap.slots.size(); ++i)
    {
        snap.midiBuffers[i].clear();
        nodeBufferMap[snap.slots[i].nodeId] = &snap.midiBuffers[i];
    }
    midiRouter_.dispatchMidi(nodeBufferMap);

    // 4b. Merge EventQueue MIDI events into node MidiBuffers
    for (int e = 0; e < resolvedCount; ++e)
    {
        auto& ev = resolvedEvents[e];
        if (ev.type == ScheduledEvent::Type::paramChange)
            continue;

        auto it = nodeBufferMap.find(ev.targetNodeId);
        if (it == nodeBufferMap.end())
            continue;

        juce::MidiMessage msg;
        switch (ev.type)
        {
            case ScheduledEvent::Type::noteOn:
                msg = juce::MidiMessage::noteOn(ev.channel, ev.data1,
                                                static_cast<juce::uint8>(ev.floatValue));
                break;
            case ScheduledEvent::Type::noteOff:
                msg = juce::MidiMessage::noteOff(ev.channel, ev.data1);
                break;
            case ScheduledEvent::Type::cc:
                msg = juce::MidiMessage::controllerEvent(ev.channel, ev.data1, ev.data2);
                break;
            default:
                continue;
        }
        it->second->addEvent(msg, ev.sampleOffset);
    }

    // 5. Process each node in execution order (with sub-block splitting for param changes)
    for (int i = 0; i < (int)snap.slots.size(); ++i)
    {
        auto& slot = snap.slots[i];

        // Resolve audio input
        juce::AudioBuffer<float>& audioIn =
            (slot.audioSourceIndex >= 0)
                ? snap.audioOutputs[slot.audioSourceIndex]
                : snap.silenceBuffer;

        // MIDI input comes from the router-populated buffer
        juce::MidiBuffer& midiIn = snap.midiBuffers[i];

        // Clear audio output
        snap.audioOutputs[i].clear();

        // Collect param change events targeting this node (already sorted by sampleOffset)
        int paramChangeIndices[kMaxResolvedEvents];
        int paramChangeCount = 0;
        for (int e = 0; e < resolvedCount; ++e)
        {
            if (resolvedEvents[e].type == ScheduledEvent::Type::paramChange &&
                resolvedEvents[e].targetNodeId == slot.nodeId)
            {
                paramChangeIndices[paramChangeCount++] = e;
            }
        }

        perfMonitor_.beginNode(i, slot.nodeId);

        if (paramChangeCount == 0)
        {
            // Fast path: no param changes, single process call
            ProcessContext ctx{audioIn, snap.audioOutputs[i],
                              midiIn, snap.midiBuffers[i],
                              numSamples};
            slot.node->process(ctx);
        }
        else
        {
            // Sub-block splitting for sample-accurate parameter changes
            int currentSample = 0;

            for (int p = 0; p <= paramChangeCount; ++p)
            {
                int segEnd = (p < paramChangeCount)
                    ? resolvedEvents[paramChangeIndices[p]].sampleOffset
                    : numSamples;

                int subBlockSize = segEnd - currentSample;
                if (subBlockSize > 0)
                {
                    // Create sub-block audio views
                    juce::AudioBuffer<float> subIn(
                        audioIn.getArrayOfWritePointers(),
                        audioIn.getNumChannels(), currentSample, subBlockSize);
                    juce::AudioBuffer<float> subOut(
                        snap.audioOutputs[i].getArrayOfWritePointers(),
                        snap.audioOutputs[i].getNumChannels(), currentSample, subBlockSize);

                    // Filter MIDI for this sub-block range
                    snap.scratchMidi.clear();
                    snap.scratchMidi.addEvents(midiIn, currentSample, subBlockSize,
                                               -currentSample);

                    ProcessContext ctx{subIn, subOut,
                                      snap.scratchMidi, snap.scratchMidi,
                                      subBlockSize};
                    slot.node->process(ctx);
                }

                // Apply parameter change between segments
                if (p < paramChangeCount)
                {
                    auto& ev = resolvedEvents[paramChangeIndices[p]];
                    slot.node->setParameter(ev.data1, ev.floatValue);
                }

                currentSample = segEnd;
            }
        }

        perfMonitor_.endNode(i);
    }

    // 6. Sum all audio leaf nodes to device output
    outputBuffer.clear();
    outputMidi.clear();

    for (int i = 0; i < (int)snap.slots.size(); ++i)
    {
        if (!snap.slots[i].isAudioLeaf)
            continue;

        auto& leafAudio = snap.audioOutputs[i];
        int chToAdd = std::min(outputBuffer.getNumChannels(),
                               leafAudio.getNumChannels());
        for (int ch = 0; ch < chToAdd; ++ch)
            for (int s = 0; s < numSamples; ++s)
                outputBuffer.addSample(ch, s, leafAudio.getSample(ch, s));
    }

    perfMonitor_.endCallback();
}

// JUCE AudioIODeviceCallback
void Engine::audioDeviceIOCallbackWithContext(
    const float* const* /*inputChannelData*/, int /*numInputChannels*/,
    float* const* outputChannelData, int numOutputChannels,
    int numSamples,
    const juce::AudioIODeviceCallbackContext& /*context*/)
{
    juce::AudioBuffer<float> output(outputChannelData, numOutputChannels, numSamples);
    juce::MidiBuffer midi;
    processBlock(output, midi, numSamples);
}

void Engine::audioDeviceAboutToStart(juce::AudioIODevice* device)
{
    std::lock_guard<std::mutex> lock(controlMutex_);
    double sr = device->getCurrentSampleRate();
    int bs = device->getCurrentBufferSizeSamples();
    sampleRate_.store(sr);
    blockSize_.store(bs);
    running_.store(true);
    perfMonitor_.prepare(sr, bs);
    transport_.setSampleRate(sr);
    SQ_LOG("audioDeviceAboutToStart: sr=%.0f bs=%d", sr, bs);

    // Re-prepare all nodes with the device's actual sample rate and block size
    for (auto& [id, node] : ownedNodes_)
        node->prepare(sr, bs);

    updateGraphLocked();
}

void Engine::audioDeviceStopped()
{
    SQ_LOG("audioDeviceStopped");
    running_.store(false);
}

// ============================================================
// Buffer management
// ============================================================

int Engine::loadBuffer(const std::string& filePath, std::string& errorMessage)
{
    std::lock_guard<std::mutex> lock(controlMutex_);
    SQ_LOG("loadBuffer: %s", filePath.c_str());

    auto buffer = Buffer::loadFromFile(filePath, audioFormatManager_, errorMessage);
    if (!buffer)
        return -1;

    int id = nextBufferId_++;
    bufferNames_[id] = buffer->getName();
    ownedBuffers_[id] = std::move(buffer);
    return id;
}

int Engine::createBuffer(int numChannels, int lengthInSamples, double sampleRate,
                         const std::string& name, std::string& errorMessage)
{
    std::lock_guard<std::mutex> lock(controlMutex_);
    SQ_LOG("createBuffer: '%s' %d ch, %d samples, %.0f Hz",
           name.c_str(), numChannels, lengthInSamples, sampleRate);

    auto buffer = Buffer::createEmpty(numChannels, lengthInSamples, sampleRate, name);
    if (!buffer)
    {
        errorMessage = "Invalid buffer parameters";
        return -1;
    }

    int id = nextBufferId_++;
    bufferNames_[id] = name;
    ownedBuffers_[id] = std::move(buffer);
    return id;
}

bool Engine::removeBuffer(int id)
{
    std::lock_guard<std::mutex> lock(controlMutex_);
    SQ_LOG("removeBuffer: id=%d", id);
    auto it = ownedBuffers_.find(id);
    if (it == ownedBuffers_.end())
        return false;

    // Null out any SamplerNode references to this buffer
    const Buffer* buf = it->second.get();
    for (auto& [nodeId, nodePtr] : ownedNodes_)
    {
        auto* sampler = dynamic_cast<SamplerNode*>(nodePtr.get());
        if (sampler && sampler->getBuffer() == buf)
            sampler->setBuffer(nullptr);
    }

    // Defer destruction for RT safety
    pendingBufferDeletions_.push_back(std::move(it->second));
    ownedBuffers_.erase(it);
    bufferNames_.erase(id);
    return true;
}

Buffer* Engine::getBuffer(int id) const
{
    std::lock_guard<std::mutex> lock(controlMutex_);
    auto it = ownedBuffers_.find(id);
    return (it != ownedBuffers_.end()) ? it->second.get() : nullptr;
}

std::string Engine::getBufferName(int id) const
{
    std::lock_guard<std::mutex> lock(controlMutex_);
    auto it = bufferNames_.find(id);
    return (it != bufferNames_.end()) ? it->second : "";
}

std::vector<std::pair<int, std::string>> Engine::getBuffers() const
{
    std::lock_guard<std::mutex> lock(controlMutex_);
    std::vector<std::pair<int, std::string>> result;
    for (const auto& kv : ownedBuffers_)
    {
        auto nameIt = bufferNames_.find(kv.first);
        std::string name = (nameIt != bufferNames_.end()) ? nameIt->second : "";
        result.push_back({kv.first, name});
    }
    return result;
}

} // namespace squeeze
