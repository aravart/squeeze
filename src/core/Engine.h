#pragma once

#include "core/CommandQueue.h"
#include "core/Graph.h"
#include "core/MidiRouter.h"
#include "core/Node.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace squeeze {

struct GraphSnapshot {
    std::vector<int> executionOrder;

    struct NodeBuffers {
        juce::AudioBuffer<float> inputAudio;
        juce::AudioBuffer<float> outputAudio;
        juce::MidiBuffer inputMidi;
        juce::MidiBuffer outputMidi;
    };
    std::unordered_map<int, NodeBuffers> nodeBuffers;

    struct FanIn {
        int sourceNodeId;
    };
    std::unordered_map<int, std::vector<FanIn>> audioFanIn;
    std::unordered_map<int, std::vector<FanIn>> midiFanIn;

    std::unordered_map<int, juce::MidiBuffer*> midiBufferMap;
};

class Engine {
public:
    Engine();
    ~Engine();

    std::string getVersion() const;

    // --- Node management (control thread) ---
    int addNode(const std::string& name, std::unique_ptr<Node> node);
    bool removeNode(int nodeId);
    Node* getNode(int nodeId) const;
    std::string getNodeName(int nodeId) const;
    int getOutputNodeId() const;
    int getNodeCount() const;
    std::vector<std::pair<int, std::string>> getNodes() const;
    std::vector<int> getExecutionOrder() const;

    // --- Connection management (control thread) ---
    int connect(int srcNode, const std::string& srcPort,
                int dstNode, const std::string& dstPort,
                std::string& error);
    bool disconnect(int connectionId);
    std::vector<Connection> getConnections() const;

    // --- Parameters (control thread) ---
    float getParameter(int nodeId, const std::string& name) const;
    bool setParameter(int nodeId, const std::string& name, float value);
    std::string getParameterText(int nodeId, const std::string& name) const;
    std::vector<ParameterDescriptor> getParameterDescriptors(int nodeId) const;

    // --- Transport forwarding (control thread, stubs for tier 7) ---
    void transportPlay();
    void transportStop();
    void transportPause();
    void transportSetTempo(double bpm);
    void transportSetTimeSignature(int numerator, int denominator);
    void transportSeekSamples(int64_t samples);
    void transportSeekBeats(double beats);
    void transportSetLoopPoints(double startBeats, double endBeats);
    void transportSetLooping(bool enabled);

    // --- Transport query (control thread, stubs for tier 7) ---
    double getTransportPosition() const;
    double getTransportTempo() const;
    bool isTransportPlaying() const;

    // --- Event scheduling (control thread, stubs for tier 7) ---
    bool scheduleNoteOn(int nodeId, double beatTime, int channel, int note, float velocity);
    bool scheduleNoteOff(int nodeId, double beatTime, int channel, int note);
    bool scheduleCC(int nodeId, double beatTime, int channel, int ccNum, int ccVal);
    bool scheduleParamChange(int nodeId, double beatTime, const std::string& paramName, float value);

    // --- Audio processing (audio thread) ---
    void processBlock(float* const* outputChannels, int numChannels, int numSamples);

    // --- Accessors ---
    double getSampleRate() const;
    int getBlockSize() const;

    // --- Testing ---
    void prepareForTesting(double sampleRate, int blockSize);
    void render(int numSamples);

private:
    mutable std::mutex controlMutex_;

    struct NodeEntry {
        std::string name;
        std::unique_ptr<Node> node;
    };
    std::unordered_map<int, NodeEntry> nodes_;
    int nextNodeId_ = 1;
    int outputNodeId_ = -1;

    Graph graph_;
    GraphSnapshot* activeSnapshot_ = nullptr;

    CommandQueue commandQueue_;
    MidiRouter midiRouter_;

    double sampleRate_ = 0.0;
    int blockSize_ = 0;
    bool prepared_ = false;

    void collectGarbage();
    void buildAndSwapSnapshot();
    void handleCommand(const Command& cmd);
};

} // namespace squeeze
