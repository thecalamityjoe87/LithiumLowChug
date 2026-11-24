#pragma once

#include <juce_graphics/juce_graphics.h>

// LowChug's visual identity in one place: colors, sizes and fonts.
//
// These are the Lithium family values, taken from Lithium Drums
// (lithium-drums/src/ui/Theme.h) so the plugins look like siblings. Keep the
// two in step: when a family color changes there, change it here too.
// Components take everything from here (or from the LookAndFeel), never from
// literals of their own.

namespace lowchug::theme
{

namespace colors
{
    inline const juce::Colour background   { 0xff0e1318 };
    inline const juce::Colour panel        { 0xff141b22 };
    inline const juce::Colour raised       { 0xff18212a };
    inline const juce::Colour raisedHigh   { 0xff1d2833 };
    inline const juce::Colour border       { 0xff22303b };
    inline const juce::Colour borderStrong { 0xff2a3946 };
    inline const juce::Colour text         { 0xffe6edf2 };
    inline const juce::Colour textMuted    { 0xff93a3b1 };
    inline const juce::Colour accent       { 0xff3cc4d6 };
    inline const juce::Colour warning      { 0xfff07167 };   // things that need attention (bypass, off-target)
    inline const juce::Colour caution      { 0xffe7d27c };   // close, but not quite (the match meter)
    inline const juce::Colour focus        { 0xff3cc4d6 };   // the keyboard-focus outline (the accent)

    // The machined knob: a turned-metal cap on a skirt, an engraved pointer,
    // a tick scale around the value arc, lit from the top left. The same as
    // Lithium Drums and Lithium Studio.
    inline const juce::Colour knobTrack           { 0xff0a0e12 };
    inline const juce::Colour knobTicks           { 0xff3a4c5b };
    inline const juce::Colour knobShadow          { 0x80000000 };
    inline const juce::Colour knobSkirtTop        { 0xff2e3c49 };
    inline const juce::Colour knobSkirtBottom     { 0xff0b1015 };
    inline const juce::Colour knobSkirtEdge       { 0xff06090c };
    inline const juce::Colour knobCapLight        { 0xff7a8895 };
    inline const juce::Colour knobCapMid          { 0xff3e4a56 };
    inline const juce::Colour knobCapDark         { 0xff1b232b };
    inline const juce::Colour knobCapEdge         { 0xff0a0e12 };
    inline const juce::Colour knobSheen           { 0x33ffffff };
    inline const juce::Colour knobRings           { 0x12ffffff };
    inline const juce::Colour knobGroove          { 0xff0b1015 };
    inline const juce::Colour knobGrooveHighlight { 0x38ffffff };

    // The sunken box behind readouts (the "You hear" card, the segmented
    // control's slot).
    inline const juce::Colour readoutBox          { 0xff0a0e12 };

    // The soft glow around knob value arcs, as an alpha.
    inline constexpr float controlGlow = 0.3f;
}

// Layout at 1x. The whole UI is laid out at this size and scaled as one
// (see LowChugEditor), so every size here is in these logical pixels at any
// window size or display scale. 1.6:1, the same shape as Lithium Drums.
inline constexpr int windowWidth = 800;
inline constexpr int windowHeight = 500;

// The window opens 5% bigger than the 1x layout (840 x 525): 800 x 500
// read as a little small on a typical screen.
inline constexpr float defaultScale = 1.05f;
inline constexpr int headerHeight = 64;
inline constexpr float cornerRadius = 6.0f;

/** The spacing scale: every margin, padding and gap is one of these. */
namespace space
{
    inline constexpr int xxs = 2, xs = 4, sm = 8, md = 12, lg = 16, xl = 24, xxl = 32;
    inline constexpr int page = xl;          // the window's outer margin
    inline constexpr int panelGap = md;      // between cards
}

/** Control sizes. */
namespace size
{
    inline constexpr int knob = 56;          // the four main knobs' dials
    inline constexpr int focusRing = 2;      // the keyboard-focus outline
}

/** The type scale, in CSS-style em sizes (see body(), label(), mono()). */
namespace type
{
    inline constexpr float caption = 11.0f;  // section labels on controls, readout captions
    inline constexpr float small = 12.5f;    // secondary text, chips, tags
    inline constexpr float body = 14.0f;     // most text
    inline constexpr float heading = 15.0f;
    inline constexpr float title = 21.0f;
}

/** The family wordmark, all capitals: "LITHIUM" (Barlow Condensed Bold,
    white) over "LOWCHUG" (Barlow Condensed Medium, gray) and "GUITAR
    DETUNER", all with the same tracking as Lithium Drums' wordmark. */
namespace wordmark
{
    inline constexpr float tracking = 0.14f;
    inline constexpr float brandSize = 12.0f;     // "LITHIUM"
    inline constexpr float productSize = 24.0f;   // "LOWCHUG"
    inline constexpr float subtitleSize = 12.0f;  // "GUITAR DETUNER"
}

enum class Weight { regular, medium, semibold, bold };

/** Barlow, for body text. */
juce::Font body (float size, Weight = Weight::regular);

/** Barlow Condensed, for uppercase section labels, buttons and the wordmark. */
juce::Font label (float size, Weight = Weight::semibold);

/** IBM Plex Mono, for numbers. */
juce::Font mono (float size, Weight = Weight::regular);

/** Draws text with extra letter spacing (tracking), in em. */
void drawTrackedText (juce::Graphics&, const juce::String&, juce::Font, float tracking,
                      juce::Rectangle<float> area, juce::Justification);

float getTrackedTextWidth (const juce::String&, juce::Font, float tracking);

/** A section caption ("PLAYING IN", "HEAR"): small, tracked, muted capitals. */
void drawCaption (juce::Graphics&, const juce::String&, juce::Rectangle<float> area,
                  juce::Justification = juce::Justification::centredLeft);

} // namespace lowchug::theme
