#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <memory>
namespace wm { class WingEngine; }

class WingMachineProcessor : public juce::AudioProcessor
{
public:
    WingMachineProcessor();
    ~WingMachineProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Wing Machine"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 6.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    juce::String getStatusText() const;
    juce::AudioProcessorValueTreeState apvts;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    std::unique_ptr<wm::WingEngine> engine;
    mutable juce::SpinLock engineLock;
    double lastBpm = 120.0;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WingMachineProcessor)
};
