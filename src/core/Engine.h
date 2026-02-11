#pragma once

#include "core/Buffer.h"
#include "core/Graph.h"
#include "core/MidiRouter.h"
#include "core/Node.h"
#include "core/PerfMonitor.h"
#include "core/PluginCache.h"
#include "core/Scheduler.h"
#include "core/EventQueue.h"
#include "core/Transport.h"

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace squeeze {

class PluginNode;
class SamplerNode;

struct GraphSnapshot {
    struct NodeSlot {
        Node* node;
        int nodeId;
        int audioSourceIndex;
        bool isAudioLeaf;  // true if no other node reads this node's audio output
    };

    std::vector<NodeSlot> slots;
    std::vector<juce::AudioBuffer<float>> audioOutputs;
    std::vector<juce::MidiBuffer> midiBuffers;  // one per node, populated by MidiRouter
    juce::AudioBuffer<float> silenceBuffer;
    juce::MidiBuffer scratchMidi;
};

class Engine : public juce::AudioIODeviceCallback {
public:
    explicit Engine(Scheduler& scheduler);
    ~Engine() override;

    // Control thread: device management
    bool start(double sampleRate = 44100.0, int blockSize = 512);
    void stop();
    bool isRunning() const;
    double getSampleRate() const;
    int getBlockSize() const;

    // Plugin cache
    bool loadPluginCache(const std::string& xmlPath);
    std::vector<std::string> getAvailablePluginNames() const;
    const juce::PluginDescription* findPluginByName(const std::string& name) const;

    // Node management (control thread)
    int addNode(std::unique_ptr<Node> node, const std::string& name);
    bool removeNode(int id);
    Node* getNode(int id) const;
    std::string getNodeName(int id) const;
    std::vector<std::pair<int, std::string>> getNodes() const;

    // Plugin instantiation
    int addPlugin(const std::string& name, std::string& errorMessage);

    // MIDI routing
    MidiRouter& getMidiRouter();

    // Sampler management
    int addSampler(const std::string& name, int maxVoices, std::string& errorMessage);
    bool setSamplerBuffer(int nodeId, int bufferId);

    // Graph topology (audio connections only)
    int connect(int srcId, const std::string& srcPort,
                int dstId, const std::string& dstPort, std::string& error);
    bool disconnect(int connId);
    std::vector<Connection> getConnections() const;

    // Push internal graph to audio thread
    void updateGraph();
    // Legacy overload for tests with external graphs
    void updateGraph(const Graph& graph);

    // Parameters (index-based)
    bool setParameter(int nodeId, int paramIndex, float value);
    float getParameter(int nodeId, int paramIndex) const;

    // Parameters (name-based convenience)
    bool setParameterByName(int nodeId, const std::string& name, float value);
    float getParameterByName(int nodeId, const std::string& name) const;

    // Parameter discovery + display
    std::vector<ParameterDescriptor> getParameterDescriptors(int nodeId) const;
    std::string getParameterText(int nodeId, int paramIndex) const;

    // Buffer management (control thread)
    int loadBuffer(const std::string& filePath, std::string& errorMessage);
    int createBuffer(int numChannels, int lengthInSamples, double sampleRate,
                     const std::string& name, std::string& errorMessage);
    bool removeBuffer(int id);
    Buffer* getBuffer(int id) const;
    std::string getBufferName(int id) const;
    std::vector<std::pair<int, std::string>> getBuffers() const;

    // Processing (public for testing without a device)
    void processBlock(juce::AudioBuffer<float>& outputBuffer,
                      juce::MidiBuffer& outputMidi,
                      int numSamples);

    // JUCE AudioIODeviceCallback
    void audioDeviceIOCallbackWithContext(
        const float* const* inputChannelData, int numInputChannels,
        float* const* outputChannelData, int numOutputChannels,
        int numSamples,
        const juce::AudioIODeviceCallbackContext& context) override;
    void audioDeviceAboutToStart(juce::AudioIODevice* device) override;
    void audioDeviceStopped() override;

    // For testing: set sample rate/block size without opening a device
    void prepareForTesting(double sampleRate, int blockSize);

    // Transport control (sends commands via Scheduler to audio thread)
    void transportPlay();
    void transportStop();
    void transportPause();
    void transportSetTempo(double bpm);
    void transportSetTimeSignature(int numerator, int denominator);
    void transportSetPositionInSamples(int64_t samples);
    void transportSetPositionInBeats(double beats);
    void transportSetLoopPoints(double startBeats, double endBeats);
    void transportSetLooping(bool enabled);
    Transport& getTransport();
    EventQueue& getEventQueue();

    // Performance monitoring
    PerfMonitor& getPerfMonitor();
    PerfSnapshot getPerfSnapshot();

    // Access internal graph (for LuaBindings / testing)
    Graph& getGraph();

private:
    static GraphSnapshot* buildSnapshot(const Graph& graph,
                                        double sampleRate, int blockSize);

    // Locked helpers (must be called with controlMutex_ held)
    int addNodeLocked(std::unique_ptr<Node> node, const std::string& name);
    void updateGraphLocked();
    void updateGraphLocked(const Graph& graph);

    mutable std::mutex controlMutex_;
    Scheduler& scheduler_;
    static constexpr int kMaxResolvedEvents = 512;

    PerfMonitor perfMonitor_;
    Transport transport_;
    EventQueue eventQueue_;
    MidiRouter midiRouter_;
    juce::AudioDeviceManager deviceManager_;
    GraphSnapshot* activeSnapshot_ = nullptr;
    std::atomic<double> sampleRate_{0.0};
    std::atomic<int> blockSize_{0};
    std::atomic<bool> running_{false};

    // Node/graph ownership
    Graph graph_;
    PluginCache cache_;
    juce::AudioPluginFormatManager formatManager_;
    std::unordered_map<int, std::unique_ptr<Node>> ownedNodes_;
    std::unordered_map<int, std::string> nodeNames_;
    std::vector<std::unique_ptr<Node>> pendingDeletions_;
    std::vector<std::unique_ptr<Buffer>> pendingBufferDeletions_;

    // Buffer ownership
    juce::AudioFormatManager audioFormatManager_;
    std::unordered_map<int, std::unique_ptr<Buffer>> ownedBuffers_;
    std::unordered_map<int, std::string> bufferNames_;
    int nextBufferId_ = 0;
};

} // namespace squeeze
