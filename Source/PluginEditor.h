#pragma once
#include "PluginProcessor.h"

class WingMachineEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit WingMachineEditor (WingMachineProcessor&);
    ~WingMachineEditor() override = default;
    void resized() override;
    void paint (juce::Graphics&) override;
private:
    void timerCallback() override;
    WingMachineProcessor& proc;
    juce::Label title, status;
    juce::GenericAudioProcessorEditor generic;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WingMachineEditor)
};
