#pragma once

#include <juce_graphics/juce_graphics.h>

// The Lithium LowChug logo mark (a guitar pick, point down, with a flat sign
// cut out of it: a guitar tuned flat) and the app icon built from it. The
// icon generator (tools/icongen) and the Linux installer both draw it from
// here, so the icon and the installer's logo can't drift apart.
//
// Plain juce_graphics, no component code: safe to use off the message thread
// for rendering into an Image. The plugin's own editor doesn't use it (its
// header shows only the wordmark).

namespace lowchug::logo
{

/** The pick's outline, filling the best square in `area`. */
juce::Path pick (juce::Rectangle<float> area);

/** The flat sign's center line (an open path), in the same square as pick().
    It is drawn as a thick round stroke; see drawMark(). */
juce::Path flat (juce::Rectangle<float> area);

/** The mark: the pick in `pickColor`, with the flat sign "cut out" of it.

    The cut-out is really the flat sign stroked with `behind`, the fill of
    whatever the mark sits on. Pass the same fill as the background (a plain
    color, or the same gradient) and it looks exactly like a hole. A
    juce::FillType can hold either a color or a gradient. */
void drawMark (juce::Graphics&, juce::Rectangle<float> area, juce::Colour pickColor, const juce::FillType& behind);

/** The app icon at `size` x `size` pixels: the mark on a dark rounded tile,
    transparent outside the tile. */
juce::Image renderIcon (int size);

} // namespace lowchug::logo
