#include "TuningPanel.h"

#include "LithiumLookAndFeel.h"

namespace lowchug
{

namespace
{
using namespace theme;

// The dial's geometry, in its own 276 x 204 area (at 1x).
constexpr float centerX = 138.0f, centerY = 104.0f;
constexpr float arcRadius = 52.0f;              // the value arc
constexpr float tickInner = 58.0f, tickOuter = 64.0f;
constexpr float nameRadius = 84.0f;             // the tuning names
constexpr float skirtRadius = 42.0f;            // the knob
constexpr float sweep = juce::MathConstants<float>::pi * 0.75f; // 135 degrees either side of 12 o'clock

constexpr int padding = space::lg;
constexpr int sourceColumnWidth = 128;
constexpr int sourceButtonHeight = 44;
constexpr const char* sourceLabels[] = { "Standard", "Drop D", "Custom" };

juce::String formatSemitones (int st)
{
    return (st > 0 ? "+" : "") + juce::String (st) + " st";
}

/** "Eb Standard" -> "Eb", "Drop C" -> "C", "Octave Down" -> "OCT". */
juce::String shortName (int targetIndex)
{
    if (targetIndex == tunings::octaveDownIndex)
        return "OCT";
    return juce::String (std::string (tunings::targets[(size_t) targetIndex].name)).replace (" Standard", "").replace ("Drop ", "");
}
} // namespace

//==============================================================================
/** The knob in the middle of the dial: a Slider (value = notch number) drawn
    as the Lithium machined cap. */
class TuningDial::Knob final : public juce::Slider
{
public:
    Knob()
    {
        setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        // JUCE measures rotary angles clockwise from 12 o'clock, from start
        // to end: 225 degrees (7:30) round to 495 degrees (4:30).
        setRotaryParameters (juce::MathConstants<float>::twoPi - sweep, juce::MathConstants<float>::twoPi + sweep, true);
        setMouseDragSensitivity (200); // about 25 px of drag per notch
        setTitle ("Tune down to");
    }

    bool showsCustom = false; // Hear is Custom: dim the pointer

    void paint (juce::Graphics& g) override
    {
        const auto params = getRotaryParameters();
        const auto proportion = (float) valueToProportionOfLength (getValue());
        const auto angle = params.startAngleRadians + proportion * (params.endAngleRadians - params.startAngleRadians);
        const auto c = getLocalBounds().toFloat().getCentre();

        LithiumLookAndFeel::drawMachinedCap (g, c, skirtRadius, angle, isEnabled() && ! showsCustom, isMouseOverOrDragging());

        if (hasKeyboardFocus (false))
        {
            g.setColour (colors::focus);
            g.drawEllipse (juce::Rectangle<float> (skirtRadius * 2.0f + 3.0f, skirtRadius * 2.0f + 3.0f).withCentre (c),
                           (float) size::focusRing);
        }
    }
};

/** A tuning's name beside its notch; click it to jump there. */
class TuningDial::NameButton final : public juce::Button
{
public:
    explicit NameButton (int target)
        : juce::Button (juce::String (std::string (tunings::targets[(size_t) target].name))), text (shortName (target))
    {
        setTooltip ("Hear " + getName());
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }

    void paintButton (juce::Graphics& g, bool isMouseOver, bool) override
    {
        const auto on = getToggleState();
        g.setColour (on ? colors::accent : (isMouseOver ? colors::text : colors::textMuted));
        g.setFont (label (on ? 17.0f : 14.0f, on ? Weight::bold : Weight::semibold));
        g.drawText (text, getLocalBounds(), juce::Justification::centred, false);
    }

private:
    juce::String text;
};

//==============================================================================
TuningDial::TuningDial (TuningController& c)
    : controller (c), knob (std::make_unique<Knob>())
{
    addAndMakeVisible (*knob);

    // Only the USER changes the knob with a notification: refresh() moves it
    // with dontSendNotification. So every onValueChange is a turn by hand.
    knob->onValueChange = [this]
    {
        const auto notch = juce::roundToInt (knob->getValue());
        if (notch >= 0 && notch < (int) detents.size() && notch != selectedNotch())
            controller.pickTarget (detents[(size_t) notch]);
    };
    knob->textFromValueFunction = [this] (double value) // what screen readers say
    {
        const auto notch = juce::roundToInt (value);
        return notch >= 0 && notch < (int) detents.size() ? names[detents[(size_t) notch]]->getName() : juce::String();
    };

    for (int i = 0; i < (int) tunings::targets.size(); ++i)
    {
        auto* name = names.add (new NameButton (i));
        name->onClick = [this, i] { controller.pickTarget (i); };
        addChildComponent (name);
    }

    refresh();
}

TuningDial::~TuningDial() = default;

float TuningDial::angleOf (int notch) const
{
    const auto n = (int) detents.size();
    return n > 1 ? -sweep + 2.0f * sweep * (float) notch / (float) (n - 1) : -sweep;
}

int TuningDial::selectedNotch() const
{
    const auto it = std::find (detents.begin(), detents.end(), controller.getDisplayedTarget());
    return it == detents.end() ? -1 : (int) std::distance (detents.begin(), it);
}

int TuningDial::nearestNotch() const
{
    const auto heard = controller.getShiftSemitones();
    int best = 0;
    for (int k = 0; k < (int) detents.size(); ++k)
        if (std::abs (tunings::targets[(size_t) detents[(size_t) k]].offset - heard)
            < std::abs (tunings::targets[(size_t) detents[(size_t) best]].offset - heard))
            best = k;
    return best;
}

void TuningDial::refresh()
{
    // The notches: the named targets reachable from the source, in list
    // order (highest first, octave down last). None when playing in Custom.
    const auto source = controller.getSource();
    detents.clear();
    if (source != tunings::Source::custom)
        for (int i = 0; i < (int) tunings::targets.size(); ++i)
            if (i != tunings::customTargetIndex && tunings::isReachable (source, i))
                detents.push_back (i);

    const auto selected = selectedNotch();
    const auto n = (int) detents.size();

    knob->setEnabled (n > 0);
    knob->setRange (0.0, std::max (1, n - 1), 1.0);
    knob->setValue (selected >= 0 ? selected : (n > 0 ? nearestNotch() : 0), juce::dontSendNotification);
    knob->showsCustom = selected < 0;
    knob->repaint();

    for (auto* name : names)
        name->setVisible (false);
    for (int k = 0; k < n; ++k)
    {
        auto* name = names[detents[(size_t) k]];
        name->setVisible (true);
        name->setToggleState (k == selected, juce::dontSendNotification);
    }

    resized();
    repaint();
}

void TuningDial::step (int notches)
{
    if (detents.empty())
        return;
    // Out of Custom, the first step lands on the nearest tuning.
    const auto selected = selectedNotch();
    const auto target = selected >= 0 ? juce::jlimit (0, (int) detents.size() - 1, selected + notches) : nearestNotch();
    controller.pickTarget (detents[(size_t) target]);
}

void TuningDial::resized()
{
    const juce::Point<float> c { centerX, centerY };
    knob->setBounds (juce::Rectangle<float> (skirtRadius * 2.0f + 6.0f, skirtRadius * 2.0f + 6.0f).withCentre (c).toNearestInt());

    for (int k = 0; k < (int) detents.size(); ++k)
        names[detents[(size_t) k]]->setBounds (juce::Rectangle<float> (38.0f, 26.0f)
                                                   .withCentre (c.getPointOnCircumference (nameRadius, angleOf (k)))
                                                   .toNearestInt());
}

void TuningDial::paint (juce::Graphics& g)
{
    const juce::Point<float> c { centerX, centerY };
    const auto n = (int) detents.size();
    const auto selected = selectedNotch();
    const auto stroke = [] (float w) { return juce::PathStrokeType (w, juce::PathStrokeType::curved, juce::PathStrokeType::rounded); };

    drawCaption (g, "TUNE DOWN TO", { 0.0f, 0.0f, 120.0f, 13.0f });

    // One tick per notch, the selected one lit.
    for (int k = 0; k < n; ++k)
    {
        juce::Path tick;
        tick.startNewSubPath (c.getPointOnCircumference (tickInner, angleOf (k)));
        tick.lineTo (c.getPointOnCircumference (tickOuter, angleOf (k)));
        g.setColour (k == selected ? colors::accent : colors::knobTicks);
        g.strokePath (tick, stroke (2.0f));
    }

    // The groove, and the arc from the top tuning down to the selected one.
    juce::Path track;
    track.addCentredArc (c.x, c.y, arcRadius, arcRadius, 0.0f, -sweep, sweep, true);
    g.setColour (colors::knobTrack);
    g.strokePath (track, stroke (5.0f));

    if (selected > 0)
    {
        juce::Path value;
        value.addCentredArc (c.x, c.y, arcRadius, arcRadius, 0.0f, -sweep, angleOf (selected), true);
        g.setColour (colors::accent.withMultipliedAlpha (colors::controlGlow));
        g.strokePath (value, stroke (11.0f));
        g.setColour (colors::accent);
        g.strokePath (value, stroke (5.0f));
    }

    // The shift under the dial: "-2 st", or "Custom -5 st".
    const auto heard = formatSemitones (controller.getShiftSemitones());
    g.setFont (mono (13.0f, Weight::medium));
    g.setColour (selected >= 0 ? colors::accent : colors::textMuted);
    g.drawText (n == 0 ? juce::String ("Custom") : (selected >= 0 ? heard : "Custom " + heard),
                juce::Rectangle<float> (0.0f, 166.0f, (float) getWidth(), 18.0f), juce::Justification::centred, false);
}

//==============================================================================
TuningPanel::TuningPanel (TuningController& c)
    : controller (c),
      dial (c),
      help ("Tuning", {
          "Playing in: how your guitar is actually tuned. Choose Custom for any other tuning.",
          "Tune down to: the tuning you want to hear. LowChug shifts the whole guitar, so it offers the tunings it "
          "can reach from yours: standard tunings from Standard, drop tunings from Drop D, and an octave down from either.",
          "Turn the dial clockwise to go lower, or click a name to jump to it. Turning the Shift knob by hand switches "
          "to Custom; the dial then snaps back to the nearest tuning when you next use it." })
{
    for (int i = 0; i < 3; ++i)
    {
        auto& b = sourceButtons[(size_t) i];
        b.setButtonText (sourceLabels[i]);
        b.setTitle (juce::String ("Playing in ") + sourceLabels[i]);
        b.onClick = [this, i] { controller.pickSource (static_cast<tunings::Source> (i)); };
        addAndMakeVisible (b);
    }

    addAndMakeVisible (dial);
    addAndMakeVisible (help);
    refresh();
}

void TuningPanel::refresh()
{
    const auto source = controller.getSource();
    for (int i = 0; i < 3; ++i)
        sourceButtons[(size_t) i].setToggleState (static_cast<int> (source) == i, juce::dontSendNotification);
    dial.refresh();
}

void TuningPanel::resized()
{
    auto column = juce::Rectangle<int> (padding, padding + 21, sourceColumnWidth, 3 * sourceButtonHeight + 2 * space::sm);
    for (auto& b : sourceButtons)
    {
        b.setBounds (column.removeFromTop (sourceButtonHeight));
        column.removeFromTop (space::sm);
    }

    dial.setBounds (padding + sourceColumnWidth + space::lg, padding, 276, 204);
    help.setBounds (getWidth() - 10 - HelpButton::size, 10, HelpButton::size, HelpButton::size);
}

void TuningPanel::paint (juce::Graphics& g)
{
    drawCard (g, getLocalBounds().toFloat(), colors::panel);
    drawCaption (g, "PLAYING IN", { (float) padding, (float) padding, (float) sourceColumnWidth, 13.0f });
}

} // namespace lowchug
