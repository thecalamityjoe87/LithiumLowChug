#pragma once

#include <vector>

namespace lowchug::dsp
{

/**
    Finds the pitch of a single note (one guitar string), for the tuner.

    The YIN method (de Cheveigné and Kawahara, 2002): compare the signal with
    delayed copies of itself; the shortest delay at which it matches itself
    well is one period of the note. A normalization step (the "cumulative
    mean normalized difference") stops it from picking a delay of zero, and
    a parabola through the best delay and its neighbors gives the period to
    a fraction of a sample, which is what makes it accurate to a cent.

    It handles one note at a time; chords have no single pitch. Plain C++,
    no JUCE. prepare() allocates; detect() does not.
*/
class PitchDetector
{
public:
    struct Result
    {
        float frequency = 0.0f;   // Hz, 0 if no clear pitch
        float clarity = 0.0f;     // 0..1, how periodic the signal is
    };

    /** Sets the range of pitches to look for and sizes the buffers.
        The window detect() needs is getWindowSize() samples. */
    void prepare (double sampleRate, float minFrequency = 60.0f, float maxFrequency = 1000.0f);

    int getWindowSize() const noexcept { return windowSize; }

    /** Analyzes the newest getWindowSize() samples (oldest first). */
    Result detect (const float* samples);

private:
    double sampleRate = 0.0;
    int minLag = 0, maxLag = 0, windowSize = 0, integration = 0;
    std::vector<float> difference;
};

} // namespace lowchug::dsp
