#pragma once

#include "core/Bus.h"
#include "core/CommandQueue.h"
#include "core/MidiRouter.h"
#include "core/Processor.h"
#include "core/Source.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace squeeze {

struct MixerSnapshot {
    struct SourceEntry {
        Source* source;
        Processor* generator;
        std::vector<Processor*> chainProcessors;
        juce::AudioBuffer<float> buffer;
        juce::MidiBuffer midiBuffer;
        Bus* outputBus;
        std::vector<Send> sends;
    };
    struct BusEntry {
        Bus* bus;
        std::vector<Processor*> chainProcessors;
        juce::AudioBuffer<float> buffer;
        std::vector<Send> sends;
        Bus* outputBus;
    };
    std::vector<SourceEntry> sources;
    std::vector<BusEntry> buses; // dependency order, master last
};

class Engine {
public:
    Engine(double sampleRate, int blockSize);
    ~Engine();

    std::string getVersion() const;
    double getSampleRate() const;
    int getBlockSize() const;

    // --- Source management (control thread) ---
    Source* addSource(const std::string& name, std::unique_ptr<Processor> generator);
    bool removeSource(Source* src);
    Source* getSource(int handle) const;
    std::vector<Source*> getSources() const;
    int getSourceCount() const;

    // --- Bus management (control thread) ---
    Bus* addBus(const std::string& name);
    bool removeBus(Bus* bus);
    Bus* getBus(int handle) const;
    std::vector<Bus*> getBuses() const;
    int getBusCount() const;
    Bus* getMaster() const;

    // --- Routing (control thread) ---
    void route(Source* src, Bus* bus);
    int sendFrom(Source* src, Bus* bus, float levelDb);
    void removeSend(Source* src, int sendId);
    void setSendLevel(Source* src, int sendId, float levelDb);

    bool busRoute(Bus* from, Bus* to);
    int busSend(Bus* from, Bus* to, float levelDb);
    void busRemoveSend(Bus* bus, int sendId);
    void busSendLevel(Bus* bus, int sendId, float levelDb);

    // --- Insert chains (control thread) ---
    Processor* sourceAppend(Source* src, std::unique_ptr<Processor> p);
    Processor* sourceInsert(Source* src, int index, std::unique_ptr<Processor> p);
    void sourceRemove(Source* src, int index);
    int sourceChainSize(Source* src) const;

    Processor* busAppend(Bus* bus, std::unique_ptr<Processor> p);
    Processor* busInsert(Bus* bus, int index, std::unique_ptr<Processor> p);
    void busRemove(Bus* bus, int index);
    int busChainSize(Bus* bus) const;

    // --- Parameters (control thread, by processor handle) ---
    float getParameter(int procHandle, const std::string& name) const;
    bool setParameter(int procHandle, const std::string& name, float value);
    std::string getParameterText(int procHandle, const std::string& name) const;
    std::vector<ParamDescriptor> getParameterDescriptors(int procHandle) const;

    // --- Processor lookup ---
    Processor* getProcessor(int procHandle) const;

    // --- Metering (any thread) ---
    float busPeak(Bus* bus) const;
    float busRMS(Bus* bus) const;

    // --- Batching (control thread) ---
    void batchBegin();
    void batchCommit();

    // --- Transport forwarding (control thread) ---
    void transportPlay();
    void transportStop();
    void transportPause();
    void transportSetTempo(double bpm);
    void transportSetTimeSignature(int numerator, int denominator);
    void transportSeekSamples(int64_t samples);
    void transportSeekBeats(double beats);
    void transportSetLoopPoints(double startBeats, double endBeats);
    void transportSetLooping(bool enabled);

    // --- Transport query ---
    double getTransportPosition() const;
    double getTransportTempo() const;
    bool isTransportPlaying() const;

    // --- Event scheduling stubs ---
    bool scheduleNoteOn(int sourceHandle, double beatTime, int channel, int note, float velocity);
    bool scheduleNoteOff(int sourceHandle, double beatTime, int channel, int note);
    bool scheduleCC(int sourceHandle, double beatTime, int channel, int ccNum, int ccVal);
    bool scheduleParamChange(int procHandle, double beatTime, const std::string& paramName, float value);

    // --- Audio processing (audio thread) ---
    void processBlock(float* const* outputChannels, int numChannels, int numSamples);

    // --- Accessors ---
    MidiRouter& getMidiRouter();

    // --- Testing ---
    void render(int numSamples);

private:
    mutable std::mutex controlMutex_;

    std::vector<std::unique_ptr<Source>> sources_;
    std::vector<std::unique_ptr<Bus>> buses_;
    Bus* master_ = nullptr;

    int nextHandle_ = 1;
    std::unordered_map<int, Processor*> processorRegistry_;

    MixerSnapshot* activeSnapshot_ = nullptr;

    CommandQueue commandQueue_;
    MidiRouter midiRouter_;

    double sampleRate_;
    int blockSize_;

    bool batching_ = false;
    bool snapshotDirty_ = false;

    void collectGarbage();
    void buildAndSwapSnapshot();
    void maybeRebuildSnapshot();
    void handleCommand(const Command& cmd);
    bool wouldCreateCycle(Bus* from, Bus* to) const;
    int assignHandle();
    void registerProcessor(Processor* p);
    void unregisterProcessor(Processor* p);
};

} // namespace squeeze
