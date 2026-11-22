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
TuningChip::TuningChip (const juce::String& name, const juce::String& bigText, const juce::String& smallText)
    : Button (name), big (bigText), small (smallText)
{
    setTitle (name); // what screen readers announce
    setClickingTogglesState (false); // the parameter decides which chip is lit
}

void TuningChip::paintButton (juce::Graphics& g, bool isMouseOver, bool isButtonDown)
{
    using namespace theme::colors;

    const auto bounds = getLocalBounds().toFloat().reduced (0.5f);
    const auto on = getToggleState();

    auto fill = on ? accent : raised;
    if (isButtonDown)
        fill = fill.brighter (0.12f);
    else if (isMouseOver)
        fill = on ? accent.brighter (0.08f) : raisedHigh;

    g.setColour (fill);
    g.fillRoundedRectangle (bounds, theme::cornerRadius);
    g.setColour (on ? fill : (isMouseOver ? borderStrong : border));
    g.drawRoundedRectangle (bounds, theme::cornerRadius, 1.0f);

    auto textArea = bounds.reduced (2.0f, 6.0f);
    g.setColour (on ? background : theme::colors::text); // plain "text" would be Button::text
    g.setFont (theme::label (big.length() > 3 ? 15.0f : 19.0f, theme::Weight::semibold));
    g.drawText (big, textArea.removeFromTop (textArea.getHeight() * 0.58f), juce::Justification::centredBottom, false);

    g.setColour (on ? background.withAlpha (0.8f) : textMuted);
    g.setFont (theme::mono (10.5f));
    g.drawText (small, textArea, juce::Justification::centredTop, false);

    if (hasKeyboardFocus (false))
    {
        g.setColour (focus);
        g.drawRoundedRectangle (bounds.reduced (1.0f), theme::cornerRadius, (float) theme::size::focusRing);
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
