#!/usr/bin/env python3
"""Writes THIRD-PARTY-NOTICES.txt: the copyright notice and license text of
every third-party component compiled into Lithium LowChug or shipped with it.

MIT, BSD, zlib, Apache, ISC, libpng, IJG and OFL licenses all ask for their
notice and license text to go along with binary copies, so the packaging
scripts put this file next to the plugins. It is generated from the license
files of the dependencies CMake fetched, so it follows their versions.

The list is what the release binaries really contain (checked against the
built VST3 and LV2 with `strings`), not everything JUCE ships: JUCE's FLAC,
Ogg Vorbis and libopusenc code isn't in them, and neither is LZMA. If a new
JUCE version or module adds a library, add it here.

Usage: scripts/third-party-notices.py <deps folder> <output file>
       (the deps folder is build/_deps, or build/<preset>/_deps for a build
       configured without the presets)
"""

import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent

# (name, what it does in Lithium LowChug, license, license file). Paths are
# relative to the deps folder unless they start with "assets/".
JUCE = "juce-src/modules/"
FORMATS = JUCE + "juce_audio_processors_headless/format_types/"
COMPONENTS = [
    ("JUCE", "application and plugin framework", "GNU AGPLv3 (see LICENSE)",
     None),
    ("VST3 SDK (Steinberg)", "the VST3 plugin format", "MIT",
     FORMATS + "VST3_SDK/LICENSE.txt"),
    ("LV2", "the LV2 plugin format", "ISC",
     FORMATS + "LV2_SDK/lv2/COPYING"),
    ("AudioUnitSDK (Apple)", "the AU plugin format, macOS only", "Apache-2.0",
     JUCE + "juce_audio_plugin_client/AU/AudioUnitSDK/LICENSE.txt"),
    ("Signalsmith Linear", "the pitch shifter's FFTs", "MIT",
     "signalsmith-linear-src/LICENSE.txt"),
    ("Signalsmith Stretch", "the reference pitch engine in the DSP library", "MIT",
     "signalsmith-stretch-src/LICENSE.txt"),
    ("libogg", "Ogg container support in JUCE's audio formats", "BSD-3-Clause",
     JUCE + "juce_audio_formats/codecs/ogg/COPYING"),
    ("Opus", "Opus audio support in JUCE's audio formats", "BSD-3-Clause",
     JUCE + "juce_audio_formats/codecs/opus/opus/COPYING"),
    ("opusfile", "Opus audio support in JUCE's audio formats", "BSD-3-Clause",
     JUCE + "juce_audio_formats/codecs/opus/opusfile/COPYING"),
    ("zlib", "compression", "Zlib",
     JUCE + "juce_core/zip/zlib/LICENSE"),
    ("HarfBuzz", "text shaping", "MIT (modern variant)",
     JUCE + "juce_graphics/fonts/harfbuzz/COPYING"),
    ("SheenBidi", "bidirectional text", "Apache-2.0",
     JUCE + "juce_graphics/unicode/sheenbidi/LICENSE"),
    ("libpng", "PNG images", "libpng-2.0",
     JUCE + "juce_graphics/image_formats/pnglib/LICENSE"),
    ("libjpeg (Independent JPEG Group)", "JPEG images", "IJG",
     JUCE + "juce_graphics/image_formats/jpglib/README"),
    ("libwebp", "WebP images", "BSD-3-Clause",
     JUCE + "juce_graphics/image_formats/libwebp/COPYING"),
    ("LunaSVG", "SVG drawing", "MIT",
     JUCE + "juce_graphics/drawables/lunasvg/LICENSE"),
    ("PlutoVG", "SVG drawing", "MIT",
     JUCE + "juce_graphics/drawables/lunasvg/plutovg/LICENSE"),
    ("Barlow and Barlow Condensed fonts", "interface text", "SIL OFL 1.1",
     "assets/fonts/OFL-Barlow.txt"),
    ("IBM Plex Mono font", "interface numbers", "SIL OFL 1.1",
     "assets/fonts/OFL-IBMPlexMono.txt"),
]


def extract_ijg(text: str) -> str:
    """The IJG README is long; its LEGAL ISSUES section is the license."""
    start = text.find("LEGAL ISSUES\n============")
    end = text.find("\nREFERENCES\n==========", start)
    return text[start:end].strip() + "\n" if start >= 0 and end > start else text


def main() -> int:
    if len(sys.argv) != 3:
        print(__doc__, file=sys.stderr)
        return 2

    deps = pathlib.Path(sys.argv[1])
    out = pathlib.Path(sys.argv[2])
    rule = "=" * 78 + "\n"
    parts = ["Lithium LowChug: third-party notices\n", rule,
             "Lithium LowChug is Copyright (C) 2026 Isaac Joseph and is licensed under the\n"
             "GNU Affero General Public License, version 3 (see LICENSE). It includes\n"
             "the following third-party components.\n\n"]

    for name, use, license_name, _ in COMPONENTS:
        parts.append(f"  {name}: {use} ({license_name})\n")

    missing = []

    for name, _, license_name, path in COMPONENTS:
        if path is None:
            continue

        file = ROOT / path if path.startswith("assets/") else deps / path
        if not file.is_file():
            missing.append(str(file))
            continue

        text = file.read_text(encoding="utf-8", errors="replace")
        if name.startswith("libjpeg"):
            text = extract_ijg(text)
        parts += ["\n", rule, f"{name} ({license_name})\n\n", text.rstrip() + "\n"]

    if missing:
        print("License files not found (configure the build first):\n  " + "\n  ".join(missing), file=sys.stderr)
        return 1

    out.write_text("".join(parts), encoding="utf-8")
    print(f"Wrote {out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
