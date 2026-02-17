#include "core/Engine.h"
#include "core/Logger.h"
#include "core/OutputNode.h"

#include <algorithm>
#include <cstring>

namespace squeeze {

// ═══════════════════════════════════════════════════════════════════
// Construction / Destruction
// ═══════════════════════════════════════════════════════════════════

Engine::Engine(double sampleRate, int blockSize)
    : sampleRate_(sampleRate), blockSize_(blockSize)
{
    auto outputNode = std::make_unique<OutputNode>();
    outputNodeId_ = nextNodeId_++;
    SQ_INFO("Engine: created output node id=%d sr=%.0f bs=%d", outputNodeId_, sampleRate, blockSize);

    Node* raw = outputNode.get();
    nodes_[outputNodeId_] = {"output", std::move(outputNode)};
    graph_.addNode(outputNodeId_, raw);
    raw->prepare(sampleRate_, blockSize_);

    buildAndSwapSnapshot();
}

Engine::~Engine()
{
    delete activeSnapshot_;
    activeSnapshot_ = nullptr;
    commandQueue_.collectGarbage();
    SQ_INFO("Engine: destroyed");
}

std::string Engine::getVersion() const
{
    return "0.2.0";
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

// ═══════════════════════════════════════════════════════════════════
// Node management
// ═══════════════════════════════════════════════════════════════════

int Engine::addNode(const std::string& name, std::unique_ptr<Node> node)
{
    std::lock_guard<std::mutex> lock(controlMutex_);
    collectGarbage();

    if (!node)
    {
        SQ_WARN("Engine::addNode: null node pointer");
        return -1;
    }

    int id = nextNodeId_++;
    SQ_DEBUG("Engine::addNode: id=%d name=%s", id, name.c_str());

    Node* raw = node.get();
    nodes_[id] = {name, std::move(node)};
    graph_.addNode(id, raw);

    raw->prepare(sampleRate_, blockSize_);

    buildAndSwapSnapshot();
    return id;
}

bool Engine::removeNode(int nodeId)
{
    std::lock_guard<std::mutex> lock(controlMutex_);
    collectGarbage();

    if (nodeId == outputNodeId_)
    {
        SQ_WARN("Engine::removeNode: cannot remove output node id=%d", nodeId);
        return false;
    }

    auto it = nodes_.find(nodeId);
    if (it == nodes_.end())
    {
        SQ_DEBUG("Engine::removeNode: id=%d not found", nodeId);
        return false;
    }

    SQ_DEBUG("Engine::removeNode: id=%d name=%s", nodeId, it->second.name.c_str());
    graph_.removeNode(nodeId);
    nodes_.erase(it);

    buildAndSwapSnapshot();
    return true;
}

Node* Engine::getNode(int nodeId) const
{
    std::lock_guard<std::mutex> lock(controlMutex_);
    auto it = nodes_.find(nodeId);
    if (it == nodes_.end()) return nullptr;
    return it->second.node.get();
}

std::string Engine::getNodeName(int nodeId) const
{
    std::lock_guard<std::mutex> lock(controlMutex_);
    auto it = nodes_.find(nodeId);
    if (it == nodes_.end()) return "";
    return it->second.name;
}

int Engine::getOutputNodeId() const
{
    return outputNodeId_;
}

int Engine::getNodeCount() const
{
    std::lock_guard<std::mutex> lock(controlMutex_);
    return static_cast<int>(nodes_.size());
}

std::vector<std::pair<int, std::string>> Engine::getNodes() const
{
    std::lock_guard<std::mutex> lock(controlMutex_);
    std::vector<std::pair<int, std::string>> result;
    result.reserve(nodes_.size());
    for (const auto& pair : nodes_)
        result.emplace_back(pair.first, pair.second.name);
    return result;
}

std::vector<int> Engine::getExecutionOrder() const
{
    std::lock_guard<std::mutex> lock(controlMutex_);
    return graph_.getExecutionOrder();
}

// ═══════════════════════════════════════════════════════════════════
// Connection management
// ═══════════════════════════════════════════════════════════════════

int Engine::connect(int srcNode, const std::string& srcPort,
                    int dstNode, const std::string& dstPort,
                    std::string& error)
{
    std::lock_guard<std::mutex> lock(controlMutex_);
    collectGarbage();

    PortAddress src{srcNode, PortDirection::output, srcPort};
    PortAddress dst{dstNode, PortDirection::input, dstPort};
    int connId = graph_.connect(src, dst, error);

    if (connId >= 0)
        buildAndSwapSnapshot();

    return connId;
}

bool Engine::disconnect(int connectionId)
{
    std::lock_guard<std::mutex> lock(controlMutex_);
    collectGarbage();

    bool result = graph_.disconnect(connectionId);
    if (result)
        buildAndSwapSnapshot();
    return result;
}

std::vector<Connection> Engine::getConnections() const
{
    std::lock_guard<std::mutex> lock(controlMutex_);
    return graph_.getConnections();
}

// ═══════════════════════════════════════════════════════════════════
// Parameters
// ═══════════════════════════════════════════════════════════════════

float Engine::getParameter(int nodeId, const std::string& name) const
{
    std::lock_guard<std::mutex> lock(controlMutex_);
    auto it = nodes_.find(nodeId);
    if (it == nodes_.end()) return 0.0f;
    return it->second.node->getParameter(name);
}

bool Engine::setParameter(int nodeId, const std::string& name, float value)
{
    std::lock_guard<std::mutex> lock(controlMutex_);
    auto it = nodes_.find(nodeId);
    if (it == nodes_.end()) return false;
    SQ_DEBUG("Engine::setParameter: node=%d param=%s value=%f", nodeId, name.c_str(), value);
    it->second.node->setParameter(name, value);
    return true;
}

std::string Engine::getParameterText(int nodeId, const std::string& name) const
{
    std::lock_guard<std::mutex> lock(controlMutex_);
    auto it = nodes_.find(nodeId);
    if (it == nodes_.end()) return "";
    return it->second.node->getParameterText(name);
}

std::vector<ParameterDescriptor> Engine::getParameterDescriptors(int nodeId) const
{
    std::lock_guard<std::mutex> lock(controlMutex_);
    auto it = nodes_.find(nodeId);
    if (it == nodes_.end()) return {};
    return it->second.node->getParameterDescriptors();
}

// ═══════════════════════════════════════════════════════════════════
// Transport stubs (tier 7 — commands sent but no handler on audio thread)
// ═══════════════════════════════════════════════════════════════════

void Engine::transportPlay()
{
    std::lock_guard<std::mutex> lock(controlMutex_);
    collectGarbage();
    Command cmd;
    cmd.type = Command::Type::transportPlay;
    commandQueue_.sendCommand(cmd);
    SQ_DEBUG("Engine::transportPlay");
}

void Engine::transportStop()
{
    std::lock_guard<std::mutex> lock(controlMutex_);
    collectGarbage();
    Command cmd;
    cmd.type = Command::Type::transportStop;
    commandQueue_.sendCommand(cmd);
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
    SQ_DEBUG("Engine::transportSeekBeats: %f", beats);
}

void Engine::transportSetLoopPoints(double startBeats, double endBeats)
{
    std::lock_guard<std::mutex> lock(controlMutex_);
    collectGarbage();
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
    Command cmd;
    cmd.type = Command::Type::setLooping;
    cmd.intValue1 = enabled ? 1 : 0;
    commandQueue_.sendCommand(cmd);
    SQ_DEBUG("Engine::transportSetLooping: %s", enabled ? "true" : "false");
}

// --- Transport queries (stubs return defaults) ---

double Engine::getTransportPosition() const { return 0.0; }
double Engine::getTransportTempo() const { return 120.0; }
bool Engine::isTransportPlaying() const { return false; }

// ═══════════════════════════════════════════════════════════════════
// Event scheduling stubs (tier 7 — always return false)
// ═══════════════════════════════════════════════════════════════════

bool Engine::scheduleNoteOn(int /*nodeId*/, double /*beatTime*/,
                            int /*channel*/, int /*note*/, float /*velocity*/)
{
    SQ_DEBUG("Engine::scheduleNoteOn: stub (no EventScheduler yet)");
    return false;
}

bool Engine::scheduleNoteOff(int /*nodeId*/, double /*beatTime*/,
                             int /*channel*/, int /*note*/)
{
    SQ_DEBUG("Engine::scheduleNoteOff: stub (no EventScheduler yet)");
    return false;
}

bool Engine::scheduleCC(int /*nodeId*/, double /*beatTime*/,
                        int /*channel*/, int /*ccNum*/, int /*ccVal*/)
{
    SQ_DEBUG("Engine::scheduleCC: stub (no EventScheduler yet)");
    return false;
}

bool Engine::scheduleParamChange(int /*nodeId*/, double /*beatTime*/,
                                 const std::string& /*paramName*/, float /*value*/)
{
    SQ_DEBUG("Engine::scheduleParamChange: stub (no EventScheduler yet)");
    return false;
}

// ═══════════════════════════════════════════════════════════════════
// GraphSnapshot — build and swap
// ═══════════════════════════════════════════════════════════════════

void Engine::buildAndSwapSnapshot()
{
    auto* snapshot = new GraphSnapshot();

    snapshot->executionOrder = graph_.getExecutionOrder();
    auto connections = graph_.getConnections();

    // Build fan-in lists from connections
    for (const auto& conn : connections)
    {
        // Look up source port to determine signal type
        auto srcIt = nodes_.find(conn.source.nodeId);
        if (srcIt == nodes_.end()) continue;

        auto srcPorts = srcIt->second.node->getOutputPorts();
        SignalType sigType = SignalType::audio;
        for (const auto& p : srcPorts)
        {
            if (p.name == conn.source.portName)
            {
                sigType = p.signalType;
                break;
            }
        }

        GraphSnapshot::FanIn fanIn{conn.source.nodeId};
        if (sigType == SignalType::audio)
            snapshot->audioFanIn[conn.dest.nodeId].push_back(fanIn);
        else
            snapshot->midiFanIn[conn.dest.nodeId].push_back(fanIn);
    }

    // Allocate per-node buffers
    int bs = blockSize_;
    for (int nodeId : snapshot->executionOrder)
    {
        auto it = nodes_.find(nodeId);
        if (it == nodes_.end()) continue;

        Node* node = it->second.node.get();
        auto inPorts = node->getInputPorts();
        auto outPorts = node->getOutputPorts();

        int inChannels = 0;
        int outChannels = 0;
        for (const auto& p : inPorts)
            if (p.signalType == SignalType::audio)
                inChannels = std::max(inChannels, p.channels);
        for (const auto& p : outPorts)
            if (p.signalType == SignalType::audio)
                outChannels = std::max(outChannels, p.channels);

        auto& bufs = snapshot->nodeBuffers[nodeId];
        bufs.inputAudio.setSize(std::max(inChannels, 1), bs);
        bufs.inputAudio.clear();
        bufs.outputAudio.setSize(std::max(outChannels, 1), bs);
        bufs.outputAudio.clear();

        snapshot->midiBufferMap[nodeId] = &bufs.inputMidi;
    }

    SQ_DEBUG("Engine::buildAndSwapSnapshot: %d nodes in execution order",
             static_cast<int>(snapshot->executionOrder.size()));

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
            auto* newSnapshot = static_cast<GraphSnapshot*>(cmd.ptr);
            auto* old = activeSnapshot_;
            activeSnapshot_ = newSnapshot;
            if (old)
                commandQueue_.sendGarbage(GarbageItem::wrap(old));
            SQ_TRACE_RT("Engine: swapped snapshot");
            break;
        }
        // Transport commands are no-ops until Transport lands
        case Command::Type::transportPlay:
        case Command::Type::transportStop:
        case Command::Type::transportPause:
        case Command::Type::setTempo:
        case Command::Type::setTimeSignature:
        case Command::Type::seekSamples:
        case Command::Type::seekBeats:
        case Command::Type::setLoopPoints:
        case Command::Type::setLooping:
            SQ_TRACE_RT("Engine: transport command %s (stub)",
                         commandTypeName(cmd.type));
            break;
    }
}

// ═══════════════════════════════════════════════════════════════════
// processBlock (audio thread)
// ═══════════════════════════════════════════════════════════════════

void Engine::processBlock(float* const* outputChannels, int numChannels, int numSamples)
{
    // 1. Drain pending commands
    commandQueue_.processPending([this](const Command& cmd) { handleCommand(cmd); });

    // 2. If no snapshot, output silence
    if (!activeSnapshot_)
    {
        for (int ch = 0; ch < numChannels; ++ch)
            std::memset(outputChannels[ch], 0, sizeof(float) * static_cast<size_t>(numSamples));
        return;
    }

    // 3. Clear all node MIDI input buffers
    for (auto& pair : activeSnapshot_->nodeBuffers)
        pair.second.inputMidi.clear();

    // 4. Dispatch MIDI via MidiRouter
    midiRouter_.dispatch(activeSnapshot_->midiBufferMap, numSamples);

    // 5. Process nodes in execution order
    for (int nodeId : activeSnapshot_->executionOrder)
    {
        auto bufIt = activeSnapshot_->nodeBuffers.find(nodeId);
        if (bufIt == activeSnapshot_->nodeBuffers.end()) continue;

        auto& bufs = bufIt->second;

        // Clear input audio
        bufs.inputAudio.clear();

        // Sum audio fan-in
        auto fanInIt = activeSnapshot_->audioFanIn.find(nodeId);
        if (fanInIt != activeSnapshot_->audioFanIn.end())
        {
            for (const auto& fanIn : fanInIt->second)
            {
                auto srcBufIt = activeSnapshot_->nodeBuffers.find(fanIn.sourceNodeId);
                if (srcBufIt == activeSnapshot_->nodeBuffers.end()) continue;

                auto& srcOutput = srcBufIt->second.outputAudio;
                int channels = std::min(bufs.inputAudio.getNumChannels(),
                                        srcOutput.getNumChannels());
                int samples = std::min(numSamples, srcOutput.getNumSamples());
                for (int ch = 0; ch < channels; ++ch)
                    bufs.inputAudio.addFrom(ch, 0, srcOutput, ch, 0, samples);
            }
        }

        // Merge MIDI fan-in
        auto midiFanInIt = activeSnapshot_->midiFanIn.find(nodeId);
        if (midiFanInIt != activeSnapshot_->midiFanIn.end())
        {
            for (const auto& fanIn : midiFanInIt->second)
            {
                auto srcBufIt = activeSnapshot_->nodeBuffers.find(fanIn.sourceNodeId);
                if (srcBufIt == activeSnapshot_->nodeBuffers.end()) continue;
                bufs.inputMidi.addEvents(srcBufIt->second.outputMidi, 0, numSamples, 0);
            }
        }

        // Clear output buffers
        bufs.outputAudio.clear();
        bufs.outputMidi.clear();

        // Find node and process
        auto nodeIt = nodes_.find(nodeId);
        if (nodeIt == nodes_.end()) continue;

        ProcessContext ctx{bufs.inputAudio, bufs.outputAudio,
                          bufs.inputMidi, bufs.outputMidi, numSamples};
        nodeIt->second.node->process(ctx);
    }

    // 6. Copy output node's input buffer to output channels
    auto outBufIt = activeSnapshot_->nodeBuffers.find(outputNodeId_);
    if (outBufIt != activeSnapshot_->nodeBuffers.end())
    {
        auto& outInput = outBufIt->second.inputAudio;
        int channels = std::min(numChannels, outInput.getNumChannels());
        int samples = std::min(numSamples, outInput.getNumSamples());
        for (int ch = 0; ch < channels; ++ch)
            std::memcpy(outputChannels[ch], outInput.getReadPointer(ch),
                        sizeof(float) * static_cast<size_t>(samples));
        // Zero any extra output channels
        for (int ch = channels; ch < numChannels; ++ch)
            std::memset(outputChannels[ch], 0, sizeof(float) * static_cast<size_t>(numSamples));
    }
    else
    {
        for (int ch = 0; ch < numChannels; ++ch)
            std::memset(outputChannels[ch], 0, sizeof(float) * static_cast<size_t>(numSamples));
    }
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

// ═══════════════════════════════════════════════════════════════════
// Testing
// ═══════════════════════════════════════════════════════════════════

void Engine::render(int numSamples)
{
    std::lock_guard<std::mutex> lock(controlMutex_);

    // Drain commands synchronously (we're calling processBlock from control thread)
    commandQueue_.processPending([this](const Command& cmd) { handleCommand(cmd); });

    juce::AudioBuffer<float> outputBuffer(2, numSamples);
    outputBuffer.clear();

    float* channels[2] = {outputBuffer.getWritePointer(0),
                          outputBuffer.getWritePointer(1)};
    processBlock(channels, 2, numSamples);

    SQ_TRACE("Engine::render: %d samples", numSamples);
}

} // namespace squeeze
