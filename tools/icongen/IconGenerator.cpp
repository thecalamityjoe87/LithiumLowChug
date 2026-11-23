// lowchug-icongen: writes the app icon and the installers' artwork, all drawn
// from the same logo code as the Linux installer (src/ui/Logo.cpp).
//
//   lowchug-icongen <output-folder>
//
// It writes:
//   icon-<n>.png                     the app icon at 16 to 1024 px
//   installer-side.png, -2x.png      the Windows wizard's welcome/finish panel (164 x 314)
//   installer-small.png, -2x.png     the Windows wizard's top-right picture (55 x 55)
//   installer-mac-background.png     the macOS Installer's background (icon bottom left)
//
// scripts/make-icons.sh runs it and converts the results into the .ico and
// .bmp files the installers need. Its output is committed, so the build only
// compiles this tool; run the script by hand after changing the logo.

#include "LithiumFonts.h"
#include "ui/Logo.h"
#include "ui/Theme.h"

#include <iostream>

using namespace lowchug;

namespace
{
    bool writePng (const juce::Image& image, const juce::File& file)
    {
        file.deleteFile();
        juce::FileOutputStream out (file);
        juce::PNGImageFormat png;
        return out.openedOk() && png.writeImageToStream (image, out);
    }

    // A font straight from the embedded font data. (The theme's font
    // functions would work too, but this tool has no LookAndFeel and wants
    // nothing else from the UI code.)
    juce::Font font (const char* data, int size, float height)
    {
        return juce::Font (juce::FontOptions (juce::Typeface::createSystemTypefaceFor (data, (size_t) size)).withPointHeight (height));
    }

    // Letter-spaced text, centered on x (the wordmark style): each glyph is
    // placed by hand with `tracking` (in em) of extra space after it.
    void drawTracked (juce::Graphics& g, const juce::String& text, const juce::Font& f, float tracking, float centerX, float baseline)
    {
        juce::GlyphArrangement glyphs;
        glyphs.addLineOfText (f, text, 0.0f, baseline);

        const auto extra = tracking * f.getHeight();
        const auto width = glyphs.getBoundingBox (0, -1, true).getWidth() + extra * (float) (text.length() - 1);
        auto x = centerX - width * 0.5f;

        for (int i = 0; i < glyphs.getNumGlyphs(); ++i)
        {
            auto glyph = glyphs.getGlyph (i);
            glyph.moveBy (x - glyph.getLeft(), 0.0f);
            glyph.draw (g);
            x += glyph.getRight() - glyph.getLeft() + extra;
        }
    }

    // The Windows wizard's tall picture: the mark over the wordmark
    // (LITHIUM, LOWCHUG, GUITAR DETUNER), and the company at the bottom.
    juce::Image sidePanel (float scale)
    {
        using namespace theme;
        const auto w = (int) std::round (164 * scale), h = (int) std::round (314 * scale);
        juce::Image image (juce::Image::RGB, w, h, true);
        juce::Graphics g (image);

        const juce::ColourGradient gradient (colors::raisedHigh, 0.0f, 0.0f, colors::background, 0.0f, (float) h, false);
        g.setGradientFill (gradient);
        g.fillAll();

        const auto mark = 92.0f * scale;
        const auto center = juce::Point<float> ((float) w * 0.5f, (float) h * 0.36f);
        logo::drawMark (g, juce::Rectangle<float> (mark, mark).withCentre (center), colors::accent, juce::FillType (gradient));

        using namespace LithiumFonts;
        const auto x = (float) w * 0.5f;
        const auto baseline = center.y + mark * 0.5f + 35.0f * scale;
        g.setColour (colors::text);
        drawTracked (g, "LITHIUM", font (BarlowCondensedBold_ttf, BarlowCondensedBold_ttfSize, 14.0f * scale),
                     wordmark::tracking, x, baseline);
        g.setColour (colors::textMuted);
        drawTracked (g, "LOWCHUG", font (BarlowCondensedMedium_ttf, BarlowCondensedMedium_ttfSize, 28.0f * scale),
                     wordmark::tracking, x, baseline + 27.0f * scale);
        drawTracked (g, "GUITAR DETUNER", font (BarlowCondensedMedium_ttf, BarlowCondensedMedium_ttfSize, 11.0f * scale),
                     wordmark::tracking, x, baseline + 45.0f * scale);
        g.setFont (font (BarlowRegular_ttf, BarlowRegular_ttfSize, 12.0f * scale));
        g.drawText ("Calamity Audio", juce::Rectangle<float> (0.0f, (float) h - 34.0f * scale, (float) w, 16.0f * scale),
                    juce::Justification::centred);
        return image;
    }

    // The macOS Installer draws its background under the left-hand step
    // list; the icon sits at the bottom left of it.
    juce::Image macBackground()
    {
        juce::Image image (juce::Image::ARGB, 620, 418, true);
        juce::Graphics g (image);
        g.drawImageAt (logo::renderIcon (128), 46, 418 - 128 - 34);
        return image;
    }
}

int main (int argc, char* argv[])
{
    if (argc != 2)
    {
        std::cerr << "usage: lowchug-icongen <output-folder>" << std::endl;
        return 1;
    }

    const auto folder = juce::File::getCurrentWorkingDirectory().getChildFile (argv[1]);
    folder.createDirectory();
    bool ok = true;

    for (const auto size : { 16, 24, 32, 48, 64, 128, 256, 512, 1024 })
        ok &= writePng (logo::renderIcon (size), folder.getChildFile ("icon-" + juce::String (size) + ".png"));

    ok &= writePng (sidePanel (1.0f), folder.getChildFile ("installer-side.png"));
    ok &= writePng (sidePanel (2.0f), folder.getChildFile ("installer-side-2x.png"));
    ok &= writePng (logo::renderIcon (55), folder.getChildFile ("installer-small.png"));
    ok &= writePng (logo::renderIcon (110), folder.getChildFile ("installer-small-2x.png"));
    ok &= writePng (macBackground(), folder.getChildFile ("installer-mac-background.png"));

    std::cout << (ok ? "Wrote the icons to " : "Couldn't write all the icons to ") << folder.getFullPathName() << std::endl;
    return ok ? 0 : 1;
}
