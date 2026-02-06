#include "core/PluginNode.h"
#include "core/Logger.h"

namespace squeeze {

PluginNode::PluginNode(std::unique_ptr<juce::AudioProcessor> processor,
                       int inputChannels, int outputChannels,
                       bool acceptsMidi)
    : processor_(std::move(processor))
    , name_(processor_->getName())
    , inputChannels_(inputChannels)
    , outputChannels_(outputChannels)
    , acceptsMidi_(acceptsMidi)
{
    buildParameterMap();
}

std::unique_ptr<PluginNode> PluginNode::create(
    const juce::PluginDescription& description,
    juce::AudioPluginFormatManager& formatManager,
    double sampleRate, int blockSize,
    juce::String& errorMessage)
{
    auto instance = formatManager.createPluginInstance(
        description, sampleRate, blockSize, errorMessage);

    if (!instance)
    {
        SQ_LOG("PluginNode::create failed: %s", errorMessage.toRawUTF8());
        return nullptr;
    }

    bool midi = instance->acceptsMidi();
    int numIn = description.numInputChannels;
    int numOut = description.numOutputChannels;

    auto node = std::make_unique<PluginNode>(
        std::move(instance), numIn, numOut, midi);

    SQ_LOG("PluginNode::create: %s (%din/%dout, midi=%d)",
           description.name.toRawUTF8(), numIn, numOut, (int)midi);
    return node;
}

void PluginNode::prepare(double sampleRate, int blockSize)
{
    SQ_LOG("PluginNode::prepare: %s sr=%.0f bs=%d",
           name_.toRawUTF8(), sampleRate, blockSize);
    processor_->prepareToPlay(sampleRate, blockSize);
}

void PluginNode::process(ProcessContext& context)
{
    // For effects: copy input audio to output (in-place processing)
    // For instruments: clear output (plugin generates audio from scratch)
    if (inputChannels_ > 0)
    {
        int channels = std::min(context.outputAudio.getNumChannels(),
                                context.inputAudio.getNumChannels());
        for (int ch = 0; ch < channels; ++ch)
            context.outputAudio.copyFrom(ch, 0, context.inputAudio,
                                         ch, 0, context.numSamples);
    }
    else
    {
        context.outputAudio.clear();
    }

    // Copy input MIDI to output MIDI (JUCE processes MIDI in-place)
    context.outputMidi.clear();
    context.outputMidi.addEvents(context.inputMidi, 0, context.numSamples, 0);

    // Delegate to the plugin
    processor_->processBlock(context.outputAudio, context.outputMidi);
}

void PluginNode::release()
{
    processor_->releaseResources();
}

std::vector<PortDescriptor> PluginNode::getInputPorts() const
{
    std::vector<PortDescriptor> ports;

    if (inputChannels_ > 0)
        ports.push_back({"in", PortDirection::input, SignalType::audio, inputChannels_});

    if (acceptsMidi_)
        ports.push_back({"midi_in", PortDirection::input, SignalType::midi, 1});

    return ports;
}

std::vector<PortDescriptor> PluginNode::getOutputPorts() const
{
    std::vector<PortDescriptor> ports;

    if (outputChannels_ > 0)
        ports.push_back({"out", PortDirection::output, SignalType::audio, outputChannels_});

    ports.push_back({"midi_out", PortDirection::output, SignalType::midi, 1});

    return ports;
}

std::vector<std::string> PluginNode::getParameterNames() const
{
    std::vector<std::string> names;
    for (const auto& [name, index] : paramNameToIndex_)
        names.push_back(name);
    return names;
}

float PluginNode::getParameter(const std::string& name) const
{
    auto it = paramNameToIndex_.find(name);
    if (it == paramNameToIndex_.end())
        return 0.0f;

    auto& params = processor_->getParameters();
    if (it->second < params.size())
        return params[it->second]->getValue();

    return 0.0f;
}

void PluginNode::setParameter(const std::string& name, float value)
{
    auto it = paramNameToIndex_.find(name);
    if (it == paramNameToIndex_.end())
        return;

    auto& params = processor_->getParameters();
    if (it->second < params.size())
        params[it->second]->setValue(value);
}

const juce::String& PluginNode::getName() const
{
    return name_;
}

juce::AudioProcessor* PluginNode::getProcessor()
{
    return processor_.get();
}

juce::AudioPluginInstance* PluginNode::getPluginInstance()
{
    return dynamic_cast<juce::AudioPluginInstance*>(processor_.get());
}

void PluginNode::buildParameterMap()
{
    auto& params = processor_->getParameters();
    for (int i = 0; i < params.size(); ++i)
    {
        auto name = params[i]->getName(256).toStdString();
        paramNameToIndex_[name] = i;
    }
}

} // namespace squeeze
