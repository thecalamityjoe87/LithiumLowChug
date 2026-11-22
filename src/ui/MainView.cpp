#include "MainView.h"

#include "plugin/Parameters.h"

namespace lowchug
{

namespace
{
using namespace theme;

// The four cards, at 1x.
const juce::Rectangle<int> tuningCard { space::page, headerHeight + 20, 452, 236 };
const juce::Rectangle<int> readoutCard { tuningCard.getRight() + space::panelGap, tuningCard.getY(),
                                         windowWidth - space::page - (tuningCard.getRight() + space::panelGap), 236 };
const juce::Rectangle<int> knobsCard { space::page, tuningCard.getBottom() + space::panelGap, 452,
                                       windowHeight - space::page - (tuningCard.getBottom() + space::panelGap) };

constexpr const char* comingSoonTip = "Coming in the next version.";

juce::RangedAudioParameter& param (LowChugProcessor& p, const char* id)
{
    return *p.getState().getParameter (id);
}
} // namespace

MainView::MainView (LowChugProcessor& p)
    : processor (p),
      controller (p),
      header (p),
      tuning (controller),
      readout (p, controller),
      shiftKnob (p.getState(), params::shift, "Shift", 0.0f),
      fineKnob (p.getState(), params::fine, "Fine", 0.0f),
      mixKnob (p.getState(), params::mix, "Mix"),
      outputKnob (p.getState(), params::output, "Output", 0.0f),
      modeAttachment (param (p, params::mode), [this] (float value)
      {
          // Light the segment for the current mode (0 = Live, 1 = Studio).
          for (int i = 0; i < 2; ++i)
              modeButtons[(size_t) i].setToggleState (juce::roundToInt (value) == i, juce::dontSendNotification);
      }),
      formantAttachment (p.getState(), params::formant, formantButton),
      tightKnob (p.getState(), params::tight, "Tight", std::nullopt, size::knobSmall),
      controlsHelp ("Controls", {
          "Shift: the shift in semitones. It follows the dial; turning it by hand sets your own shift (Custom).",
          "Fine: nudges the pitch up or down by up to 50 cents, to match a band tuned slightly off.",
          "Mix: how much of the shifted guitar you hear. 100 % is shifted only; lower blends in your guitar as played.",
          "Output: the output level. Double-click any knob to reset it." }),
      qualityHelp ("Quality (coming soon)", {
          "These arrive in the next update.",
          "Live / Studio: Live has the lowest latency (21 ms), for playing. Studio sounds cleaner in the lows with "
          "43 ms of latency, for recording and mixing.",
          "Formant: keeps your guitar sounding like itself as you tune down, rather than darker and boomier.",
          "Tight: tightens the low end on big drops." })
{
    addAndMakeVisible (header);
    addAndMakeVisible (tuning);
    addAndMakeVisible (readout);

    for (auto* knob : { &shiftKnob, &fineKnob, &mixKnob, &outputKnob })
        addAndMakeVisible (*knob);

    shiftKnob.getSlider().setTooltip ("Shift by hand, in semitones. Moving it switches Hear to Custom.");
    fineKnob.getSlider().setTooltip ("Fine tuning, in cents (hundredths of a semitone).");
    mixKnob.getSlider().setTooltip ("How much of the shifted guitar you hear. 100 % is fully shifted.");
    outputKnob.getSlider().setTooltip ("Output level.");

    // The parameter sync. Whenever source, target, shift or fine change
    // (from anywhere), both cards redraw from the parameters...
    controller.onChange = [this]
    {
        tuning.refresh();
        readout.repaint();
    };

    // ...and when the USER turns Shift, Hear becomes Custom. UserSlider tells
    // a hand-made change from the knob merely following its parameter, so
    // automation or a Hear click moving the knob never triggers this.
    shiftKnob.getSlider().onUserChange = [this] { controller.shiftMovedByHand(); };

    // Coming soon.
    const char* modeNames[] = { "Live", "Studio" };
    for (int i = 0; i < 2; ++i)
    {
        auto& b = modeButtons[(size_t) i];
        b.setButtonText (modeNames[i]);
        b.getProperties().set ("segment", true);
        b.setEnabled (false);
        b.setTooltip (comingSoonTip);
        addAndMakeVisible (b);
    }
    modeAttachment.sendInitialUpdate();

    formantButton.setClickingTogglesState (true);
    formantButton.setEnabled (false);
    formantButton.setTooltip (comingSoonTip);
    addAndMakeVisible (formantButton);

    tightKnob.setEnabled (false);
    tightKnob.getSlider().setTooltip (comingSoonTip);
    addAndMakeVisible (tightKnob);

    addAndMakeVisible (controlsHelp);
    addAndMakeVisible (qualityHelp);

    header.onShowAbout = [this] { showAbout (true); };
    about.onClose = [this] { showAbout (false); };
    addChildComponent (about);
}

void MainView::showAbout (bool show)
{
    about.setVisible (show);
    if (show)
    {
        about.toFront (false);
        about.grabKeyboardFocus(); // for Escape
    }
}

juce::Rectangle<int> MainView::getKnobsCard() const { return knobsCard; }

juce::Rectangle<int> MainView::getComingSoonCard() const
{
    return { readoutCard.getX(), knobsCard.getY(), readoutCard.getWidth(), knobsCard.getHeight() };
}

void MainView::resized()
{
    header.setBounds (0, 0, windowWidth, headerHeight);
    tuning.setBounds (tuningCard);
    readout.setBounds (readoutCard);
    about.setBounds (getLocalBounds());

    // Each card's "?" sits 10 px in from its top-right corner.
    const auto helpAt = [] (juce::Rectangle<int> card)
    {
        return juce::Rectangle<int> (card.getRight() - 10 - HelpButton::size, card.getY() + 10, HelpButton::size, HelpButton::size);
    };
    controlsHelp.setBounds (helpAt (getKnobsCard()));
    qualityHelp.setBounds (helpAt (getComingSoonCard()));

    // The four knobs, spread evenly across their card.
    const auto knobs = getKnobsCard();
    LabeledKnob* row[] = { &shiftKnob, &fineKnob, &mixKnob, &outputKnob };
    for (int i = 0; i < 4; ++i)
    {
        auto* knob = row[i];
        const auto size = knob->getPreferredSize();
        const auto centerX = knobs.getX() + juce::roundToInt (knobs.getWidth() * (i + 0.5f) / 4.0f);
        knob->setBounds (size.withCentre ({ centerX, knobs.getCentreY() }));
    }

    // Coming soon: Mode and Formant on the left, Tight on the right.
    const auto soon = getComingSoonCard().reduced (space::lg, space::md);
    const auto tightSize = tightKnob.getPreferredSize();
    tightKnob.setBounds (tightSize.withPosition (soon.getRight() - tightSize.getWidth(), soon.getY() + 24));

    auto column = soon.withTrimmedTop (32).withRight (tightKnob.getX() - space::md);
    auto modeSlot = column.removeFromTop (30).reduced (2);
    modeButtons[0].setBounds (modeSlot.removeFromLeft (modeSlot.getWidth() / 2 - 1));
    modeSlot.removeFromLeft (2);
    modeButtons[1].setBounds (modeSlot);
    column.removeFromTop (space::sm);
    formantButton.setBounds (column.removeFromTop (28));
}

void MainView::paint (juce::Graphics& g)
{
    g.fillAll (colors::background);

    drawCard (g, getKnobsCard().toFloat(), colors::panel);

    // Coming soon: a dashed outline (not a solid card) says "not here yet".
    const auto soon = getComingSoonCard().toFloat();
    juce::Path outline;
    outline.addRoundedRectangle (soon.reduced (0.5f), cornerRadius);
    juce::Path dashed;
    const float dashes[] = { 4.0f, 3.0f };
    juce::PathStrokeType (1.0f).createDashedStroke (dashed, outline, dashes, 2);
    g.setColour (colors::borderStrong);
    g.fillPath (dashed);

    auto top = soon.reduced ((float) space::lg, (float) space::md).removeFromTop (16.0f);
    drawCaption (g, "QUALITY", top);

    const auto pillFont = label (10.5f, Weight::bold);
    const auto pillWidth = getTrackedTextWidth ("COMING SOON", pillFont, 0.12f) + 18.0f;
    const auto pill = top.withRight ((float) qualityHelp.getX() - 6.0f).removeFromRight (pillWidth);
    g.setColour (colors::borderStrong);
    g.drawRoundedRectangle (pill.reduced (0.5f), pill.getHeight() * 0.5f, 1.0f);
    g.setColour (colors::textMuted);
    drawTrackedText (g, "COMING SOON", pillFont, 0.12f, pill, juce::Justification::centred);

    // The slot behind the Live / Studio segments.
    drawSegmentSlot (g, modeButtons[0].getBounds().getUnion (modeButtons[1].getBounds()).expanded (2).toFloat());
}

} // namespace lowchug
