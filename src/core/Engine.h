#pragma once

#include "core/Buffer.h"
#include "core/Graph.h"
#include "core/Node.h"
#include "core/PluginCache.h"
#include "core/Scheduler.h"

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace squeeze {

class PluginNode;
class MidiInputNode;

struct GraphSnapshot {
    struct NodeSlot {
        Node* node;
        int audioSourceIndex;
        struct MidiSource { int slotIndex; int channelFilter; };
        std::vector<MidiSource> midiSources;
        bool isAudioLeaf;  // true if no other node reads this node's audio output
    };

    std::vector<NodeSlot> slots;
    std::vector<juce::AudioBuffer<float>> audioOutputs;
    std::vector<juce::MidiBuffer> midiOutputs;
    juce::AudioBuffer<float> silenceBuffer;
    juce::MidiBuffer emptyMidi;
    juce::MidiBuffer filteredMidi;  // scratch buffer for channel filtering
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

    // MIDI input management
    std::vector<std::string> getAvailableMidiInputs() const;
    int addMidiInput(const std::string& deviceName, std::string& errorMessage);
    void autoLoadMidiInputs();

    struct MidiRefreshResult {
        std::vector<std::string> added;
        std::vector<std::string> removed;
    };
    MidiRefreshResult refreshMidiInputs();

    // Graph topology
    int connect(int srcId, const std::string& srcPort,
                int dstId, const std::string& dstPort, std::string& error,
                int midiChannel = 0);
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

    // Access internal graph (for LuaBindings / testing)
    Graph& getGraph();

private:
    static GraphSnapshot* buildSnapshot(const Graph& graph,
                                        double sampleRate, int blockSize);

    Scheduler& scheduler_;
    juce::AudioDeviceManager deviceManager_;
    GraphSnapshot* activeSnapshot_ = nullptr;
    std::atomic<double> sampleRate_{0.0};
    std::atomic<int> blockSize_{0};
    std::atomic<bool> running_{false};

    // Node/graph ownership (moved from LuaBindings)
    Graph graph_;
    PluginCache cache_;
    juce::AudioPluginFormatManager formatManager_;
    std::unordered_map<int, std::unique_ptr<Node>> ownedNodes_;
    std::unordered_map<int, std::string> nodeNames_;
    std::unordered_map<std::string, int> midiDeviceNodes_;
    std::vector<std::unique_ptr<Node>> pendingDeletions_;

    // Buffer ownership
    juce::AudioFormatManager audioFormatManager_;
    std::unordered_map<int, std::unique_ptr<Buffer>> ownedBuffers_;
    std::unordered_map<int, std::string> bufferNames_;
    int nextBufferId_ = 0;
};

} // namespace squeeze
