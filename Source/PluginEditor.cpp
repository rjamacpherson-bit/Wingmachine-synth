#include "PluginEditor.h"

WingMachineEditor::WingMachineEditor (WingMachineProcessor& p)
    : juce::AudioProcessorEditor (&p), proc (p), generic (p)
{
    title.setText ("Wing Machine  v0.8.2   (Feather Synth)", juce::dontSendNotification);
    title.setFont (juce::Font (juce::FontOptions (18.0f))); title.setColour (juce::Label::textColourId, juce::Colour (0xffe8eef2));
    status.setFont (juce::Font (juce::FontOptions (13.0f))); status.setColour (juce::Label::textColourId, juce::Colour (0xff55c6d2));
    addAndMakeVisible (title); addAndMakeVisible (status); addAndMakeVisible (generic);
    setResizable (true, true); setResizeLimits (420, 300, 900, 1400); setSize (520, 760);
    startTimerHz (4);
}

void WingMachineEditor::paint (juce::Graphics& g) { g.fillAll (juce::Colour (0xff0e161c)); }

void WingMachineEditor::resized()
{
    auto r = getLocalBounds().reduced (8);
    title.setBounds (r.removeFromTop (26)); status.setBounds (r.removeFromTop (22)); generic.setBounds (r);
}

void WingMachineEditor::timerCallback() { status.setText (proc.getStatusText(), juce::dontSendNotification); }
