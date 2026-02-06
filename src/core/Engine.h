#pragma once

#include "core/Graph.h"
#include "core/Node.h"
#include "core/Scheduler.h"

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_devices/juce_audio_devices.h>

#include <atomic>
#include <memory>
#include <vector>

namespace squeeze {

struct GraphSnapshot {
    struct NodeSlot {
        Node* node;
        int audioSourceIndex;
        int midiSourceIndex;
        bool isAudioLeaf;  // true if no other node reads this node's audio output
    };

    std::vector<NodeSlot> slots;
    std::vector<juce::AudioBuffer<float>> audioOutputs;
    std::vector<juce::MidiBuffer> midiOutputs;
    juce::AudioBuffer<float> silenceBuffer;
    juce::MidiBuffer emptyMidi;
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

    // Control thread: graph management
    void updateGraph(const Graph& graph);

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

private:
    static GraphSnapshot* buildSnapshot(const Graph& graph,
                                        double sampleRate, int blockSize);

    Scheduler& scheduler_;
    GraphSnapshot* activeSnapshot_ = nullptr;
    std::atomic<double> sampleRate_{0.0};
    std::atomic<int> blockSize_{0};
    std::atomic<bool> running_{false};
};

} // namespace squeeze
