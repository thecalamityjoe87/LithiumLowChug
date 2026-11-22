#include "Controls.h"

namespace lowchug
{

//==============================================================================
UserSlider::UserSlider()
{
    // A knob dragged up/down or left/right, with no text box (the value is
    // drawn by LabeledKnob).
    setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);

    // onValueChange runs after the attachment (a Slider::Listener) has
    // written the parameter, so by the time onUserChange runs the parameter
    // already holds the new value.
    onValueChange = [this]
    {
        if (userChanging && onUserChange)
            onUserChange();
    };
}

bool UserSlider::keyPressed (const juce::KeyPress& key)
{
    const juce::ScopedValueSetter<bool> changing (userChanging, true);
    return Slider::keyPressed (key);
}

//==============================================================================
LabeledKnob::LabeledKnob (juce::AudioProcessorValueTreeState& state, const char* parameterID,
                            const juce::String& captionText, std::optional<float> origin, int size)
    : caption (captionText.toUpperCase()),
      dialSize (size),
      attachment (state, parameterID, slider)
{
    addAndMakeVisible (slider);

    if (origin)
        slider.getProperties().set ("origin", *origin);

    // Double-click returns the knob to the parameter's default.
    if (auto* param = state.getParameter (parameterID))
        slider.setDoubleClickReturnValue (true, param->convertFrom0to1 (param->getDefaultValue()));

    // The value text below the dial follows the knob.
    slider.onValueChange = [this, previous = slider.onValueChange]
    {
        if (previous)
            previous();
        repaint();
    };
}

void LabeledKnob::paint (juce::Graphics& g)
{
    auto area = getLocalBounds().toFloat();
    const auto alpha = isEnabled() ? 1.0f : 0.5f;

    g.setColour (theme::colors::textMuted.withMultipliedAlpha (alpha));
    theme::drawTrackedText (g, caption, theme::label (theme::type::caption, theme::Weight::semibold), 0.12f,
                            area.removeFromTop (14.0f), juce::Justification::centred);

    g.setColour (theme::colors::text.withMultipliedAlpha (alpha));
    g.setFont (theme::mono (theme::type::small, theme::Weight::medium));
    g.drawText (slider.getTextFromValue (slider.getValue()), area.removeFromBottom (16.0f),
                juce::Justification::centred, false);
}

void LabeledKnob::resized()
{
    auto area = getLocalBounds();
    area.removeFromTop (18);
    area.removeFromBottom (20);
    slider.setBounds (area.withSizeKeepingCentre (dialSize, dialSize));
}

//==============================================================================
namespace
{
juce::Font tabFont() { return theme::label (theme::type::caption, theme::Weight::semibold); }
constexpr float tabTracking = 0.12f;
} // namespace

TabButton::TabButton (const juce::String& name) : Button (name)
{
    setClickingTogglesState (false); // the owner decides which tab is selected
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

int TabButton::getIdealWidth() const
{
    return juce::roundToInt (theme::getTrackedTextWidth (getName().toUpperCase(), tabFont(), tabTracking)) + 2;
}

void TabButton::paintButton (juce::Graphics& g, bool isMouseOver, bool)
{
    using namespace theme;
    const auto on = getToggleState();
    auto area = getLocalBounds().toFloat();

    g.setColour (on ? colors::text : (isMouseOver ? colors::text.withAlpha (0.85f) : colors::textMuted));
    drawTrackedText (g, getName().toUpperCase(), tabFont(), tabTracking, area.withTrimmedBottom (3.0f),
                     juce::Justification::centredLeft);
    if (on)
    {
        g.setColour (colors::accent);
        g.fillRect (area.removeFromBottom (2.0f));
    }
}

//==============================================================================
InfoButton::InfoButton() : Button ("About")
{
    setTooltip ("About Lithium LowChug");
}

void InfoButton::paintButton (juce::Graphics& g, bool isMouseOver, bool isDown)
{
    using namespace theme::colors;
    const auto bounds = getLocalBounds().toFloat().reduced (1.0f);

    g.setColour (isDown ? raisedHigh : (isMouseOver ? raised : panel));
    g.fillEllipse (bounds);
    g.setColour (isMouseOver ? accent : borderStrong);
    g.drawEllipse (bounds, 1.0f);

    // The "i": a dot over a short rounded bar.
    const auto c = bounds.getCentre();
    g.setColour (isMouseOver ? theme::colors::text : textMuted);
    g.fillEllipse (juce::Rectangle<float> (3.0f, 3.0f).withCentre ({ c.x, c.y - 5.5f }));
    g.fillRoundedRectangle (juce::Rectangle<float> (2.6f, 9.0f).withCentre ({ c.x, c.y + 2.5f }), 1.0f);

    if (hasKeyboardFocus (false))
    {
        g.setColour (focus);
        g.drawEllipse (bounds, (float) theme::size::focusRing);
    }
}

//==============================================================================
namespace
{
/** Where call-outs go: the main view (the editor's child, which the editor
    scales as a whole), so they scale with the rest of the UI. */
juce::Component* findOverlayParent (juce::Component& c)
{
    for (auto* p = &c; p != nullptr; p = p->getParentComponent())
        if (dynamic_cast<juce::AudioProcessorEditor*> (p->getParentComponent()) != nullptr)
            return p;
    return c.getTopLevelComponent();
}

/** A "?" in a ring, centered by its ink: a font's text box has room for
    ascenders and descenders, which would leave the mark sitting high. */
void drawHelpMark (juce::Graphics& g, juce::Rectangle<float> ring, bool highlighted)
{
    g.setColour (highlighted ? theme::colors::accent : theme::colors::textMuted);
    g.drawEllipse (ring.reduced (0.75f), 1.3f);

    juce::GlyphArrangement mark;
    mark.addLineOfText (theme::body (10.5f, theme::Weight::bold), "?", 0.0f, 0.0f);
    const auto ink = mark.getBoundingBox (0, -1, true);
    mark.moveRangeOfGlyphs (0, -1, ring.getCentreX() - ink.getCentreX(), ring.getCentreY() - ink.getCentreY());
    mark.draw (g);
}
} // namespace

HelpPopover::HelpPopover (const juce::String& heading, const juce::StringArray& paragraphs)
{
    using namespace theme;
    constexpr int width = 340;

    // One block of styled text: the heading, then each paragraph after a
    // short gap. TextLayout wraps it to the width and tells us its height.
    juce::AttributedString text;
    text.append (heading, body (14.5f, Weight::semibold), colors::text);

    for (const auto& p : paragraphs)
    {
        text.append ("\n", body (14.5f), colors::text);
        text.append ("\n", body (6.0f), colors::text); // the gap between paragraphs
        text.append (p, body (type::small), colors::text.withAlpha (0.88f));
    }

    text.setWordWrap (juce::AttributedString::byWord);
    layout.createLayout (text, (float) (width - 2 * padding));
    setSize (width, juce::roundToInt (layout.getHeight()) + 2 * padding);
}

void HelpPopover::paint (juce::Graphics& g)
{
    layout.draw (g, getLocalBounds().reduced (padding).toFloat());
}

void HelpPopover::show (juce::Component& from, juce::Rectangle<int> target, const juce::String& heading,
                        const juce::StringArray& paragraphs)
{
    if (auto* parent = findOverlayParent (from))
        juce::CallOutBox::launchAsynchronously (std::make_unique<HelpPopover> (heading, paragraphs),
                                                parent->getLocalArea (&from, target), parent);
}

HelpButton::HelpButton (juce::String h, juce::StringArray p)
    : juce::Button ("Help: " + h), heading (std::move (h)), paragraphs (std::move (p))
{
    setTooltip ("What " + heading + " does");
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
    setWantsKeyboardFocus (false);
    onClick = [this] { HelpPopover::show (*this, getLocalBounds(), heading, paragraphs); };
}

void HelpButton::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    drawHelpMark (g, getLocalBounds().toFloat().withSizeKeepingCentre (14.0f, 14.0f), highlighted || down);
}

//==============================================================================
void drawCard (juce::Graphics& g, juce::Rectangle<float> area, juce::Colour fill)
{
    g.setColour (fill);
    g.fillRoundedRectangle (area, theme::cornerRadius);
    g.setColour (theme::colors::border);
    g.drawRoundedRectangle (area.reduced (0.5f), theme::cornerRadius, 1.0f);
}

void drawSegmentSlot (juce::Graphics& g, juce::Rectangle<float> area)
{
    drawCard (g, area, theme::colors::readoutBox);
}

} // namespace lowchug
