#include "PluginEditor.h"

namespace lowchug
{

LowChugEditor::LowChugEditor (LowChugProcessor& p)
    : AudioProcessorEditor (p), view (p)
{
    // Set on the editor, the LookAndFeel applies to every child component.
    setLookAndFeel (&lookAndFeel);
    addAndMakeVisible (view);

    // Resizable by the host's window (first true) and with JUCE's own
    // corner handle (second true, for hosts that can't resize plugins).
    setResizable (true, true);
    setResizeLimits (theme::windowWidth * 3 / 4, theme::windowHeight * 3 / 4,
                     theme::windowWidth * 2, theme::windowHeight * 2);
    getConstrainer()->setFixedAspectRatio ((double) theme::windowWidth / (double) theme::windowHeight);

    // Must come last: setSize() calls resized(), which needs the view.
    setSize (juce::roundToInt (theme::windowWidth * theme::defaultScale),
             juce::roundToInt (theme::windowHeight * theme::defaultScale));
}

LowChugEditor::~LowChugEditor()
{
    // Components keep a pointer to their LookAndFeel; clear it before the
    // LookAndFeel member is destroyed.
    setLookAndFeel (nullptr);
}

void LowChugEditor::resized()
{
    // Lay out at 1x, then scale the whole view to fill the window.
    const auto scale = (float) getWidth() / (float) theme::windowWidth;
    view.setBounds (0, 0, theme::windowWidth, theme::windowHeight);
    view.setTransform (juce::AffineTransform::scale (scale));
}

} // namespace lowchug
