#pragma once

#include "Theme.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace lowchug
{

/** The embedded fonts, loaded once and shared by everything that needs them.

    Held through juce::SharedResourcePointer: the first holder creates it and
    the last one to go away deletes it. That keeps the typefaces alive exactly
    as long as some UI exists, and gone before JUCE shuts down (a plain static
    would outlive JUCE's font system when the plugin is unloaded). */
struct EmbeddedFonts
{
    EmbeddedFonts();
    ~EmbeddedFonts();

    juce::Typeface::Ptr barlowRegular, barlowMedium, barlowSemiBold;
    juce::Typeface::Ptr condensedMedium, condensedSemiBold, condensedBold;
    juce::Typeface::Ptr monoRegular, monoMedium;

    /** How many EmbeddedFonts exist right now (0 or 1). The tests use it to
        check that closing the editor really releases everything. */
    static int getLiveCount();
};

/**
    The Lithium family look, shared with Lithium Drums: the machined knob,
    flat rounded buttons, Barlow everywhere.

    A LookAndFeel is JUCE's way of skinning the stock widgets: instead of
    subclassing every Slider and Button to paint them differently, the
    widgets ask their LookAndFeel to draw them. Setting it once on the editor
    makes every child component use it.

    Extra per-widget options are passed as component properties (a small
    key/value map every Component has), so no custom widget classes are needed:
      - Slider "origin" (a value in the slider's range): where the value arc
        starts, e.g. 0 ct on Fine, 0 dB on Output. Default: the
        start of the range.
      - Button "segment" = true: draws as one segment of a segmented control
        (no outline, transparent when off, accent when on).
      - Button "danger" = true: lights red instead of the accent when on
        (Bypass).
*/
class LithiumLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    LithiumLookAndFeel();

    juce::Typeface::Ptr getTypefaceForFont (const juce::Font&) override;

    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour& backgroundColour,
                               bool isMouseOverButton, bool isButtonDown) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool isMouseOverButton, bool isButtonDown) override;
    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;

    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height, float sliderPosProportional,
                           float rotaryStartAngle, float rotaryEndAngle, juce::Slider&) override;

    /** The machined knob's skirt, cap and pointer (no scale or arc), centered
        on `center`. Shared by the knobs and the tuning dial. `angle` is in
        radians, clockwise from 12 o'clock. */
    static void drawMachinedCap (juce::Graphics&, juce::Point<float> center, float skirtRadius, float angle,
                                 bool enabled, bool highlighted);

    void drawCallOutBoxBackground (juce::CallOutBox&, juce::Graphics&, const juce::Path&, juce::Image&) override;
    float getCallOutBoxCornerSize (const juce::CallOutBox&) override { return theme::cornerRadius + 2.0f; }

    juce::Font getPopupMenuFont() override;
    void drawPopupMenuBackground (juce::Graphics&, int width, int height) override;
    void drawPopupMenuSectionHeader (juce::Graphics&, const juce::Rectangle<int>&, const juce::String&) override;

    void drawTooltip (juce::Graphics&, const juce::String& text, int width, int height) override;
    juce::Rectangle<int> getTooltipBounds (const juce::String& text, juce::Point<int> screenPos,
                                           juce::Rectangle<int> parentArea) override;

    /** The keyboard-focus outline, drawn the same on every kind of control. */
    static void drawFocusRing (juce::Graphics&, juce::Rectangle<float>, float cornerRadius);

private:
    juce::SharedResourcePointer<EmbeddedFonts> fonts;
};

} // namespace lowchug
