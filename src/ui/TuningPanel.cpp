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

/** A notch's name beside it (a tuning, or a semitone count); click it to
    jump there. */
class TuningDial::NameButton final : public juce::Button
{
public:
    NameButton (const juce::String& fullName, const juce::String& shortText, const juce::String& tooltip)
        : juce::Button (fullName), text (shortText)
    {
        setTooltip (tooltip);
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
        if (notch != selectedNotch())
            pickNotch (notch);
    };
    knob->textFromValueFunction = [this] (double value) // what screen readers say
    {
        const auto* name = nameOf (juce::roundToInt (value));
        return name != nullptr ? name->getName() : juce::String();
    };

    for (int i = 0; i < (int) tunings::targets.size(); ++i)
    {
        const auto full = juce::String (std::string (tunings::targets[(size_t) i].name));
        auto* name = names.add (new NameButton (full, shortName (i), "Hear " + full));
        name->onClick = [this, i] { controller.pickTarget (i); };
        addChildComponent (name);
    }

    for (int k = 0; k < numShiftNotches; ++k)
    {
        const auto st = shiftOfNotch (k);
        const auto full = st == 0 ? juce::String ("No shift")
                                  : (st > 0 ? "Up " : "Down ") + juce::String (std::abs (st))
                                        + (std::abs (st) == 1 ? " semitone" : " semitones");
        auto* name = shiftNames.add (new NameButton (full, (st > 0 ? "+" : "") + juce::String (st), full));
        name->onClick = [this, st] { controller.pickShift (st); };
        addChildComponent (name);
    }

    refresh();
}

TuningDial::~TuningDial() = default;

int TuningDial::getNumNotches() const noexcept
{
    return counting ? numShiftNotches : (int) detents.size();
}

int TuningDial::getShownNotch() const
{
    return juce::roundToInt (knob->getValue());
}

TuningDial::NameButton* TuningDial::nameOf (int notch) const
{
    if (notch < 0 || notch >= getNumNotches())
        return nullptr;
    return counting ? shiftNames[notch] : names[detents[(size_t) notch]];
}

void TuningDial::pickNotch (int notch)
{
    if (notch < 0 || notch >= getNumNotches())
        return;
    if (counting)
        controller.pickShift (shiftOfNotch (notch));
    else
        controller.pickTarget (detents[(size_t) notch]);
}

float TuningDial::angleOf (int notch) const
{
    const auto n = getNumNotches();
    return n > 1 ? -sweep + 2.0f * sweep * (float) notch / (float) (n - 1) : -sweep;
}

int TuningDial::selectedNotch() const
{
    if (counting)
    {
        // Every shift the parameter allows has a notch.
        const auto notch = notchOfShift (controller.getShiftSemitones());
        return notch >= 0 && notch < numShiftNotches ? notch : -1;
    }
    const auto it = std::find (detents.begin(), detents.end(), controller.getDisplayedTarget());
    return it == detents.end() ? -1 : (int) std::distance (detents.begin(), it);
}

int TuningDial::nearestNotch() const
{
    const auto heard = controller.getShiftSemitones();
    if (counting)
        return juce::jlimit (0, numShiftNotches - 1, notchOfShift (heard));
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
    // order (highest first, octave down last). Playing in Custom there are
    // no names to offer, so the dial counts semitones down instead.
    const auto source = controller.getSource();
    counting = source == tunings::Source::custom;
    detents.clear();
    if (! counting)
        for (int i = 0; i < (int) tunings::targets.size(); ++i)
            if (i != tunings::customTargetIndex && tunings::isReachable (source, i))
                detents.push_back (i);

    const auto selected = selectedNotch();
    const auto n = getNumNotches();

    knob->setEnabled (n > 0);
    knob->setRange (0.0, std::max (1, n - 1), 1.0);
    knob->setValue (selected >= 0 ? selected : (n > 0 ? nearestNotch() : 0), juce::dontSendNotification);
    knob->setTitle (counting ? "Shift" : "Tune down to");
    knob->showsCustom = selected < 0;
    knob->repaint();

    for (auto* name : names)
        name->setVisible (false);
    for (auto* name : shiftNames)
        name->setVisible (false);
    for (int k = 0; k < n; ++k)
    {
        auto* name = nameOf (k);
        name->setVisible (true);
        name->setToggleState (k == selected, juce::dontSendNotification);
    }

    resized();
    repaint();
}

void TuningDial::step (int notches)
{
    const auto n = getNumNotches();
    if (n == 0)
        return;
    // Off the notches (Hear is Custom), the first step lands on the nearest one.
    const auto selected = selectedNotch();
    pickNotch (selected >= 0 ? juce::jlimit (0, n - 1, selected + notches) : nearestNotch());
}

void TuningDial::resized()
{
    const juce::Point<float> c { centerX, centerY };
    knob->setBounds (juce::Rectangle<float> (skirtRadius * 2.0f + 6.0f, skirtRadius * 2.0f + 6.0f).withCentre (c).toNearestInt());

    // Shifts are short and there are more of them, so their boxes are
    // smaller (15 notches sit about 28 px apart round the dial).
    const auto box = counting ? juce::Rectangle<float> (28.0f, 22.0f) : juce::Rectangle<float> (38.0f, 26.0f);
    for (int k = 0; k < getNumNotches(); ++k)
        nameOf (k)->setBounds (box.withCentre (c.getPointOnCircumference (nameRadius, angleOf (k))).toNearestInt());
}

void TuningDial::paint (juce::Graphics& g)
{
    const juce::Point<float> c { centerX, centerY };
    const auto n = getNumNotches();
    const auto selected = selectedNotch();
    const auto stroke = [] (float w) { return juce::PathStrokeType (w, juce::PathStrokeType::curved, juce::PathStrokeType::rounded); };

    drawCaption (g, counting ? "SHIFT" : "TUNE DOWN TO", { 0.0f, 0.0f, 120.0f, 13.0f });

    // One tick per notch, the selected one lit.
    for (int k = 0; k < n; ++k)
    {
        juce::Path tick;
        tick.startNewSubPath (c.getPointOnCircumference (tickInner, angleOf (k)));
        tick.lineTo (c.getPointOnCircumference (tickOuter, angleOf (k)));
        g.setColour (k == selected ? colors::accent : colors::knobTicks);
        g.strokePath (tick, stroke (2.0f));
    }

    // The groove, and the arc from "no shift" to the selected notch: from
    // the top tuning down, or (when counting) from 0 either way.
    juce::Path track;
    track.addCentredArc (c.x, c.y, arcRadius, arcRadius, 0.0f, -sweep, sweep, true);
    g.setColour (colors::knobTrack);
    g.strokePath (track, stroke (5.0f));

    const auto origin = counting ? notchOfShift (0) : 0;
    if (selected >= 0 && selected != origin)
    {
        juce::Path value;
        value.addCentredArc (c.x, c.y, arcRadius, arcRadius, 0.0f,
                             angleOf (std::min (origin, selected)), angleOf (std::max (origin, selected)), true);
        g.setColour (colors::accent.withMultipliedAlpha (colors::controlGlow));
        g.strokePath (value, stroke (11.0f));
        g.setColour (colors::accent);
        g.strokePath (value, stroke (5.0f));
    }

    // The shift under the dial: "-2 st", or "Custom -5 st" between named
    // tunings. When counting, every shift is your own, so just "-2 st".
    const auto heard = formatSemitones (controller.getShiftSemitones());
    g.setFont (mono (13.0f, Weight::medium));
    g.setColour (selected >= 0 ? colors::accent : colors::textMuted);
    g.drawText (selected >= 0 || counting ? heard : "Custom " + heard,
                juce::Rectangle<float> (0.0f, 166.0f, (float) getWidth(), 18.0f), juce::Justification::centred, false);
}

//==============================================================================
TuningPanel::TuningPanel (TuningController& c)
    : controller (c),
      dial (c),
      help ("Tuning", {
          "Playing in: how your guitar is actually tuned. Choose Custom for any other tuning (Drop C, open tunings ...).",
          "Tune down to: the tuning you want to hear. LowChug shifts the whole guitar, so it offers the tunings it "
          "can reach from yours: standard tunings from Standard, drop tunings from Drop D, and an octave down from either.",
          "Turn the dial clockwise to go lower, or click a name to jump to it.",
          "Playing in Custom, the dial counts semitones instead: from +2 (up, like a capo) through 0 down to -12 "
          "(an octave down). Use Fine for anything in between." })
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
