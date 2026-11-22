#pragma once

// Minimal WAV reader/writer for the evaluation harness (keeps the harness
// free of JUCE so it builds in seconds). Reads 16/24/32-bit PCM and 32-bit
// float, mono or multichannel; writes 24-bit PCM.

#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

namespace lowchug::eval
{

struct AudioFile
{
    double sampleRate = 48000.0;
    std::vector<std::vector<float>> channels;

    size_t numSamples() const { return channels.empty() ? 0 : channels[0].size(); }
};

inline bool readWav (const std::string& path, AudioFile& out, std::string& error)
{
    std::ifstream file (path, std::ios::binary);
    if (! file)
    {
        error = "cannot open " + path;
        return false;
    }

    std::vector<uint8_t> data ((std::istreambuf_iterator<char> (file)), {});
    auto u16 = [&] (size_t i) { return uint16_t (data[i] | (data[i + 1] << 8)); };
    auto u32 = [&] (size_t i) { return uint32_t (u16 (i) | (uint32_t (u16 (i + 2)) << 16)); };

    if (data.size() < 12 || std::memcmp (data.data(), "RIFF", 4) != 0 || std::memcmp (data.data() + 8, "WAVE", 4) != 0)
    {
        error = "not a RIFF/WAVE file";
        return false;
    }

    int format = 0, numChannels = 0, bits = 0;
    size_t pos = 12;
    while (pos + 8 <= data.size())
    {
        const uint32_t chunkSize = u32 (pos + 4);
        const size_t body = pos + 8;
        if (std::memcmp (data.data() + pos, "fmt ", 4) == 0)
        {
            format = u16 (body);
            numChannels = u16 (body + 2);
            out.sampleRate = u32 (body + 4);
            bits = u16 (body + 14);
            if (format == 0xFFFE && chunkSize >= 26) // WAVE_FORMAT_EXTENSIBLE: real format is in the GUID
                format = u16 (body + 24);
        }
        else if (std::memcmp (data.data() + pos, "data", 4) == 0)
        {
            if (numChannels == 0)
            {
                error = "data before fmt chunk";
                return false;
            }
            const size_t bytesPerSample = (size_t) bits / 8;
            const size_t available = std::min<size_t> (chunkSize, data.size() - body);
            const size_t frames = available / (bytesPerSample * (size_t) numChannels);
            out.channels.assign ((size_t) numChannels, std::vector<float> (frames));

            for (size_t f = 0; f < frames; ++f)
            {
                for (int c = 0; c < numChannels; ++c)
                {
                    const size_t i = body + (f * (size_t) numChannels + (size_t) c) * bytesPerSample;
                    float v = 0.0f;
                    if (format == 3 && bits == 32)
                        std::memcpy (&v, data.data() + i, 4);
                    else if (format == 1 && bits == 16)
                        v = float (int16_t (u16 (i))) / 32768.0f;
                    else if (format == 1 && bits == 24)
                        v = float (int32_t (uint32_t (data[i] << 8 | data[i + 1] << 16 | data[i + 2] << 24)) >> 8) / 8388608.0f;
                    else if (format == 1 && bits == 32)
                        v = float (int32_t (u32 (i))) / 2147483648.0f;
                    else
                    {
                        error = "unsupported WAV format " + std::to_string (format) + "/" + std::to_string (bits) + " bit";
                        return false;
                    }
                    out.channels[(size_t) c][f] = v;
                }
            }
            return true;
        }
        pos = body + chunkSize + (chunkSize & 1);
    }

    error = "no data chunk";
    return false;
}

inline bool writeWav24 (const std::string& path, const std::vector<std::vector<float>>& channels, double sampleRate)
{
    std::ofstream file (path, std::ios::binary);
    if (! file || channels.empty())
        return false;

    const uint32_t numChannels = (uint32_t) channels.size();
    const uint32_t frames = (uint32_t) channels[0].size();
    const uint32_t dataBytes = frames * numChannels * 3;

    auto put16 = [&] (uint16_t v) { file.put (char (v & 0xff)); file.put (char (v >> 8)); };
    auto put32 = [&] (uint32_t v) { put16 (uint16_t (v & 0xffff)); put16 (uint16_t (v >> 16)); };

    file.write ("RIFF", 4); put32 (36 + dataBytes); file.write ("WAVE", 4);
    file.write ("fmt ", 4); put32 (16); put16 (1); put16 ((uint16_t) numChannels);
    put32 ((uint32_t) sampleRate); put32 ((uint32_t) sampleRate * numChannels * 3);
    put16 ((uint16_t) (numChannels * 3)); put16 (24);
    file.write ("data", 4); put32 (dataBytes);

    for (uint32_t f = 0; f < frames; ++f)
    {
        for (uint32_t c = 0; c < numChannels; ++c)
        {
            const float clamped = std::fmax (-1.0f, std::fmin (1.0f, channels[c][f]));
            const int32_t v = (int32_t) std::lround (clamped * 8388607.0f);
            file.put (char (v & 0xff)); file.put (char ((v >> 8) & 0xff)); file.put (char ((v >> 16) & 0xff));
        }
    }
    return bool (file);
}

} // namespace lowchug::eval
