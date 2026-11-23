#include "Logo.h"

#include "Theme.h"

namespace lowchug::logo
{

namespace
{
    // The mark is designed in a 100 x 100 box (the same numbers as the
    // mockup on the design canvas). This maps that box onto the best square
    // in `area`, centered.
    juce::AffineTransform boxTo (juce::Rectangle<float> area)
    {
        const auto side = std::min (area.getWidth(), area.getHeight());
        const auto square = area.withSizeKeepingCentre (side, side);
        return juce::AffineTransform::scale (side / 100.0f).translated (square.getX(), square.getY());
    }

    // The flat sign's stroke width, in the 100-unit box.
    constexpr float flatWidth = 9.0f;
}

juce::Path pick (juce::Rectangle<float> area)
{
    // A standard pick: a wide, rounded top tapering to a rounded point at
    // the bottom. Four cubic Bezier curves, symmetric about x = 50.
    juce::Path p;
    p.startNewSubPath (50.0f, 96.0f);
    p.cubicTo (42.0f, 96.0f, 8.0f, 52.0f, 6.0f, 32.0f);
    p.cubicTo (4.0f, 12.0f, 26.0f, 4.0f, 50.0f, 4.0f);
    p.cubicTo (74.0f, 4.0f, 96.0f, 12.0f, 94.0f, 32.0f);
    p.cubicTo (92.0f, 52.0f, 58.0f, 96.0f, 50.0f, 96.0f);
    p.closeSubPath();
    p.applyTransform (boxTo (area));
    return p;
}

juce::Path flat (juce::Rectangle<float> area)
{
    // The stem, then the bowl from halfway down the stem back to its foot.
    juce::Path p;
    p.startNewSubPath (35.0f, 16.0f);
    p.lineTo (35.0f, 72.0f);
    p.startNewSubPath (35.0f, 48.0f);
    p.cubicTo (47.0f, 36.0f, 65.0f, 36.0f, 65.0f, 49.0f);
    p.cubicTo (65.0f, 60.0f, 51.0f, 66.0f, 35.0f, 72.0f);
    p.applyTransform (boxTo (area));
    return p;
}

void drawMark (juce::Graphics& g, juce::Rectangle<float> area, juce::Colour pickColor, const juce::FillType& behind)
{
    g.setColour (pickColor);
    g.fillPath (pick (area));

    // Stroke widths aren't transformed with the path, so scale it by hand.
    const auto side = std::min (area.getWidth(), area.getHeight());
    g.setFillType (behind);
    g.strokePath (flat (area), juce::PathStrokeType (flatWidth * side / 100.0f, juce::PathStrokeType::curved,
                                                     juce::PathStrokeType::rounded));
}

juce::Image renderIcon (int size)
{
    using namespace theme;

    // ARGB with clearImage = true: starts fully transparent, so the corners
    // outside the rounded tile stay see-through.
    juce::Image image (juce::Image::ARGB, size, size, true);
    juce::Graphics g (image);

    const auto s = (float) size;
    const auto tile = juce::Rectangle<float> (s, s).reduced (s * 0.04f);
    const auto corner = s * 0.2f;

    // Raised-panel blue at the top to the app background at the bottom, as
    // Lithium Drums' icon.
    const juce::ColourGradient gradient (colors::raisedHigh, tile.getTopLeft(), colors::background, tile.getBottomLeft(), false);
    g.setGradientFill (gradient);
    g.fillRoundedRectangle (tile, corner);
    g.setColour (colors::borderStrong);
    g.drawRoundedRectangle (tile.reduced (s * 0.006f), corner, std::max (1.0f, s * 0.012f));

    // The mark at 66% of the tile. The flat is stroked with the tile's own
    // gradient, so it reads as a hole through the pick.
    drawMark (g, tile.withSizeKeepingCentre (s * 0.66f, s * 0.66f), colors::accent, juce::FillType (gradient));
    return image;
}

} // namespace lowchug::logo
