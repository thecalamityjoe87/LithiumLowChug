#pragma once

#include <algorithm>
#include <cmath>
#include <functional>

namespace lowchug::dsp
{

/**
    Ramps a value linearly to a new target over a fixed time, so parameter
    changes don't click. Lands EXACTLY on the target when the ramp ends (some
    code paths test for exact 0 or 1 to stay bit-transparent).

    Realtime-safe: no allocation, just arithmetic.
*/
class LinearSmoother
{
public:
    /** rampSamples = how many samples a full change takes. Call before use. */
    void setRampLength (int rampSamples) { rampLength = std::max (1, rampSamples); }

    /** Jumps straight to `value` (use after prepare/reset). */
    void snapTo (float value)
    {
        current = target = value;
        stepsLeft = 0;
    }

    void setTarget (float newTarget)
    {
        // Deliberately exact: only an actual change should restart the ramp.
        // (std::equal_to says so explicitly and keeps -Wfloat-equal quiet.)
        if (std::equal_to<float>() (newTarget, target))
            return;
        target = newTarget;
        stepsLeft = rampLength;
        step = (target - current) / (float) rampLength;
    }

    /** Advances one sample and returns the new value. */
    float next()
    {
        if (stepsLeft > 0)
        {
            if (--stepsLeft == 0)
                current = target;
            else
                current += step;
        }
        return current;
    }

    /** Advances n samples at once (for control-rate smoothing). */
    float skip (int n)
    {
        if (stepsLeft > 0)
        {
            if (n >= stepsLeft)
            {
                stepsLeft = 0;
                current = target;
            }
            else
            {
                stepsLeft -= n;
                current += step * (float) n;
            }
        }
        return current;
    }

    float getCurrent() const { return current; }
    float getTarget() const { return target; }
    bool isSettled() const { return stepsLeft == 0; }

private:
    float current = 0.0f, target = 0.0f, step = 0.0f;
    int stepsLeft = 0, rampLength = 1;
};

} // namespace lowchug::dsp
