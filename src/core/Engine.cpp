#include "core/Engine.h"
#include "core/Logger.h"

#include <unordered_map>
#include <unordered_set>

namespace squeeze {

Engine::Engine(Scheduler& scheduler)
    : scheduler_(scheduler)
{
}

Engine::~Engine()
{
    stop();
    delete activeSnapshot_;
}

bool Engine::start(double sampleRate, int blockSize)
{
    SQ_LOG("start: sr=%.0f bs=%d", sampleRate, blockSize);
    sampleRate_.store(sampleRate);
    blockSize_.store(blockSize);
    running_.store(true);
    return true;
}

void Engine::stop()
{
    SQ_LOG("stop");
    running_.store(false);
}

bool Engine::isRunning() const { return running_.load(); }
double Engine::getSampleRate() const { return sampleRate_.load(); }
int Engine::getBlockSize() const { return blockSize_.load(); }

void Engine::prepareForTesting(double sampleRate, int blockSize)
{
    sampleRate_.store(sampleRate);
    blockSize_.store(blockSize);
}

void Engine::updateGraph(const Graph& graph)
{
    SQ_LOG("updateGraph: %d nodes", graph.getNodeCount());
    double sr = sampleRate_.load();
    int bs = blockSize_.load();

    auto* snapshot = buildSnapshot(graph, sr, bs);

    Command cmd;
    cmd.type = Command::Type::swapGraph;
    cmd.ptr = snapshot;
    scheduler_.sendCommand(cmd);
}

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
        int midiSrc = -1;

        // Find incoming connections for this node
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
                    int srcIndex = idToIndex[conn.source.nodeId];
                    if (p.signalType == SignalType::audio)
                        audioSrc = srcIndex;
                    else if (p.signalType == SignalType::midi)
                        midiSrc = srcIndex;
                    break;
                }
            }
        }

        snap->slots.push_back({node, audioSrc, midiSrc, false});

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
        snap->midiOutputs.emplace_back();
        snap->midiOutputs.back().ensureSize(256);
    }

    snap->silenceBuffer.setSize(maxChannels, blockSize);
    snap->silenceBuffer.clear();

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

void Engine::processBlock(juce::AudioBuffer<float>& outputBuffer,
                          juce::MidiBuffer& outputMidi,
                          int numSamples)
{
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
                    cmd.node->setParameterByIndex(cmd.paramIndex, cmd.paramValue);
                break;
            }
        }
    });

    // 2. If no snapshot, output silence
    if (!activeSnapshot_ || activeSnapshot_->slots.empty())
    {
        outputBuffer.clear();
        outputMidi.clear();
        return;
    }

    auto& snap = *activeSnapshot_;

    // 3. Process each node in execution order
    for (int i = 0; i < (int)snap.slots.size(); ++i)
    {
        auto& slot = snap.slots[i];

        // Resolve inputs
        juce::AudioBuffer<float>& audioIn =
            (slot.audioSourceIndex >= 0)
                ? snap.audioOutputs[slot.audioSourceIndex]
                : snap.silenceBuffer;

        juce::MidiBuffer& midiIn =
            (slot.midiSourceIndex >= 0)
                ? snap.midiOutputs[slot.midiSourceIndex]
                : snap.emptyMidi;

        // Clear outputs
        snap.audioOutputs[i].clear();
        snap.midiOutputs[i].clear();

        // Process
        ProcessContext ctx{audioIn, snap.audioOutputs[i],
                          midiIn, snap.midiOutputs[i],
                          numSamples};
        slot.node->process(ctx);
    }

    // 4. Sum all audio leaf nodes to device output
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
    sampleRate_.store(device->getCurrentSampleRate());
    blockSize_.store(device->getCurrentBufferSizeSamples());
    running_.store(true);
    SQ_LOG("audioDeviceAboutToStart: sr=%.0f bs=%d",
           device->getCurrentSampleRate(),
           device->getCurrentBufferSizeSamples());
}

void Engine::audioDeviceStopped()
{
    SQ_LOG("audioDeviceStopped");
    running_.store(false);
}

} // namespace squeeze
