// Minigun drum sampler - Copyright (C) 2026 Andrii Dalevskyi (Dallas Audio)
// SPDX-License-Identifier: AGPL-3.0-or-later
// This file is part of Minigun, released under the GNU AGPL v3.0 or later; see LICENSE.

#pragma once
#include <juce_core/juce_core.h>
#include <array>
#include <vector>

namespace minigun
{

constexpr int kNumPads = 16;
constexpr int kMaxLayers = 8;

enum class PlayMode { RoundRobin, Random };

/** Stereo = normal panned output. MonoLeft/MonoRight = the pad's signal is folded to mono
    and written to only one channel (L or R) of its output bus; see Pad::monoSum. */
enum class OutputMode { Stereo, MonoLeft, MonoRight };

struct SampleRef
{
    juce::File file;     // absolute path
    bool missing = false; // set by the loader when the file could not be read

    bool operator== (const SampleRef& o) const noexcept { return file == o.file; }
};

struct VelocityLayer
{
    int lo = 1;    // 1..127
    int hi = 127;  // lo..127
    std::vector<SampleRef> samples;

    bool contains (int velocity) const noexcept { return velocity >= lo && velocity <= hi; }
};

struct Pad
{
    juce::String name;                 // empty = "Empty" shown in UI
    int note = 36;                     // 0..127
    PlayMode mode = PlayMode::RoundRobin;
    int chokeGroup = 0;                // 0 = none, 1..8
    float volumeDb = 0.0f;             // -60..+12
    float pan = 0.0f;                  // -1..1
    float pitchSemitones = 0.0f;       // -12..12
    float attackMs = 0.0f;             // 0..500
    float decayMs = -1.0f;             // -1 = full sample, else 1..5000
    float releaseMs = 120.0f;          // 1..2000
    int output = 0;                    // 0 = Main, 1..16 = aux stereo bus
    OutputMode outputMode = OutputMode::Stereo;
    bool monoSum = true;               // mono modes only: true = sum L+R of a stereo sample, false = pick the matching side
    std::vector<VelocityLayer> layers; // sorted by lo ascending, contiguous 1..127 when non-empty

    bool isEmpty() const noexcept
    {
        for (auto& l : layers)
            if (! l.samples.empty()) return false;
        return true;
    }

    int totalSamples() const noexcept
    {
        int n = 0;
        for (auto& l : layers) n += (int) l.samples.size();
        return n;
    }

    /** Returns the layer index for a velocity, or the nearest layer if none contains it. -1 if no layers. */
    int layerForVelocity (int velocity) const noexcept
    {
        if (layers.empty()) return -1;
        for (int i = 0; i < (int) layers.size(); ++i)
            if (layers[(size_t) i].contains (velocity)) return i;
        int best = 0, bestDist = 1000;
        for (int i = 0; i < (int) layers.size(); ++i)
        {
            auto& l = layers[(size_t) i];
            int d = velocity < l.lo ? l.lo - velocity : velocity - l.hi;
            if (d < bestDist) { bestDist = d; best = i; }
        }
        return best;
    }
};

struct Kit
{
    juce::String name { "Untitled Kit" };
    juce::File folder;                 // invalid until saved/loaded
    std::array<Pad, kNumPads> pads;

    Kit()
    {
        for (int i = 0; i < kNumPads; ++i)
            pads[(size_t) i].note = 36 + i;
    }

    int loadedPadCount() const noexcept
    {
        int n = 0;
        for (auto& p : pads) if (! p.isEmpty()) ++n;
        return n;
    }

    int totalSamples() const noexcept
    {
        int n = 0;
        for (auto& p : pads) n += p.totalSamples();
        return n;
    }

    //==============================================================================
    // JSON. Paths are written relative to `relativeTo` when the file is inside it,
    // otherwise absolute. Pass an invalid File to always write absolute paths.

    juce::var toJson (const juce::File& relativeTo) const
    {
        auto* root = new juce::DynamicObject();
        root->setProperty ("format", "minigun-kit");
        root->setProperty ("version", 1);
        root->setProperty ("name", name);

        juce::Array<juce::var> padArr;
        for (int i = 0; i < kNumPads; ++i)
        {
            auto& p = pads[(size_t) i];
            auto* po = new juce::DynamicObject();
            po->setProperty ("index", i);
            po->setProperty ("name", p.name);
            po->setProperty ("note", p.note);
            po->setProperty ("mode", p.mode == PlayMode::Random ? "random" : "rr");
            po->setProperty ("choke", p.chokeGroup);
            po->setProperty ("volumeDb", p.volumeDb);
            po->setProperty ("pan", p.pan);
            po->setProperty ("pitch", p.pitchSemitones);
            po->setProperty ("attackMs", p.attackMs);
            po->setProperty ("decayMs", p.decayMs);
            po->setProperty ("releaseMs", p.releaseMs);
            po->setProperty ("output", p.output);
            po->setProperty ("outMode", p.outputMode == OutputMode::MonoLeft  ? "monoL"
                                       : p.outputMode == OutputMode::MonoRight ? "monoR"
                                                                                : "stereo");
            po->setProperty ("monoSum", p.monoSum);

            juce::Array<juce::var> layerArr;
            for (auto& l : p.layers)
            {
                auto* lo = new juce::DynamicObject();
                lo->setProperty ("lo", l.lo);
                lo->setProperty ("hi", l.hi);
                juce::Array<juce::var> files;
                for (auto& s : l.samples)
                {
                    if (relativeTo.isDirectory() && s.file.isAChildOf (relativeTo))
                        files.add (s.file.getRelativePathFrom (relativeTo).replaceCharacter ('\\', '/'));
                    else
                        files.add (s.file.getFullPathName());
                }
                lo->setProperty ("samples", files);
                layerArr.add (juce::var (lo));
            }
            po->setProperty ("layers", layerArr);
            padArr.add (juce::var (po));
        }
        root->setProperty ("pads", padArr);
        return juce::var (root);
    }

    /** Parses kit JSON. Relative sample paths are resolved against `relativeTo`. Returns false on format error. */
    bool fromJson (const juce::var& root, const juce::File& relativeTo)
    {
        if (! root.isObject()) return false;
        if (root.getProperty ("format", "").toString() != "minigun-kit") return false;

        Kit fresh;
        fresh.name = root.getProperty ("name", "Untitled Kit").toString();
        fresh.folder = relativeTo;

        if (auto* padArr = root.getProperty ("pads", juce::var()).getArray())
        {
            for (auto& pv : *padArr)
            {
                int idx = (int) pv.getProperty ("index", -1);
                if (idx < 0 || idx >= kNumPads) continue;
                auto& p = fresh.pads[(size_t) idx];
                p.name = pv.getProperty ("name", "").toString();
                p.note = juce::jlimit (0, 127, (int) pv.getProperty ("note", 36 + idx));
                p.mode = pv.getProperty ("mode", "rr").toString() == "random" ? PlayMode::Random : PlayMode::RoundRobin;
                p.chokeGroup = juce::jlimit (0, 8, (int) pv.getProperty ("choke", 0));
                p.volumeDb = juce::jlimit (-60.0f, 12.0f, (float) pv.getProperty ("volumeDb", 0.0f));
                p.pan = juce::jlimit (-1.0f, 1.0f, (float) pv.getProperty ("pan", 0.0f));
                p.pitchSemitones = juce::jlimit (-12.0f, 12.0f, (float) pv.getProperty ("pitch", 0.0f));
                p.attackMs = juce::jlimit (0.0f, 500.0f, (float) pv.getProperty ("attackMs", 0.0f));
                p.decayMs = (float) pv.getProperty ("decayMs", -1.0f);
                if (p.decayMs >= 0.0f) p.decayMs = juce::jlimit (1.0f, 5000.0f, p.decayMs); else p.decayMs = -1.0f;
                p.releaseMs = juce::jlimit (1.0f, 2000.0f, (float) pv.getProperty ("releaseMs", 120.0f));
                p.output = juce::jlimit (0, 16, (int) pv.getProperty ("output", 0));
                {
                    auto modeStr = pv.getProperty ("outMode", "stereo").toString();
                    p.outputMode = modeStr == "monoL" ? OutputMode::MonoLeft
                                 : modeStr == "monoR" ? OutputMode::MonoRight
                                                       : OutputMode::Stereo;
                }
                p.monoSum = (bool) pv.getProperty ("monoSum", true);

                p.layers.clear();
                if (auto* layerArr = pv.getProperty ("layers", juce::var()).getArray())
                {
                    for (auto& lv : *layerArr)
                    {
                        if ((int) p.layers.size() >= kMaxLayers) break;
                        VelocityLayer l;
                        l.lo = juce::jlimit (1, 127, (int) lv.getProperty ("lo", 1));
                        l.hi = juce::jlimit (l.lo, 127, (int) lv.getProperty ("hi", 127));
                        if (auto* files = lv.getProperty ("samples", juce::var()).getArray())
                        {
                            for (auto& fv : *files)
                            {
                                auto s = fv.toString();
                                if (s.isEmpty()) continue;
                                SampleRef ref;
                                if (juce::File::isAbsolutePath (s))
                                    ref.file = juce::File (s);
                                else if (relativeTo.isDirectory())
                                    ref.file = relativeTo.getChildFile (s);
                                else
                                    continue;
                                l.samples.push_back (ref);
                            }
                        }
                        p.layers.push_back (std::move (l));
                    }
                }
                std::sort (p.layers.begin(), p.layers.end(), [] (auto& a, auto& b) { return a.lo < b.lo; });
            }
        }

        *this = std::move (fresh);
        return true;
    }

    juce::String toJsonString (const juce::File& relativeTo) const
    {
        return juce::JSON::toString (toJson (relativeTo), false);
    }

    bool fromJsonString (const juce::String& text, const juce::File& relativeTo)
    {
        return fromJson (juce::JSON::parse (text), relativeTo);
    }
};

/** Helpers shared by UI code for keeping layers valid. */
namespace layers
{
    /** Splits the top layer's range in half and appends a new empty layer above. Returns new layer index or -1. */
    inline int addLayerOnTop (Pad& pad)
    {
        if ((int) pad.layers.size() >= kMaxLayers) return -1;
        if (pad.layers.empty())
        {
            pad.layers.push_back ({ 1, 127, {} });
            return 0;
        }
        auto& top = pad.layers.back();
        if (top.hi - top.lo < 1) return -1;
        int mid = (top.lo + top.hi) / 2;
        VelocityLayer nl { mid + 1, top.hi, {} };
        top.hi = mid;
        pad.layers.push_back (nl);
        return (int) pad.layers.size() - 1;
    }

    /** Removes a layer and gives its range to the lower neighbour (or upper if it was the lowest). */
    inline void removeLayer (Pad& pad, int index)
    {
        if (index < 0 || index >= (int) pad.layers.size()) return;
        auto removed = pad.layers[(size_t) index];
        pad.layers.erase (pad.layers.begin() + index);
        if (pad.layers.empty()) return;
        if (index > 0) pad.layers[(size_t) index - 1].hi = removed.hi;
        else pad.layers[0].lo = removed.lo;
    }

    /** Moves the divider between layer i and i+1 so that layer i ends at `newHi` (clamped). */
    inline void setDivider (Pad& pad, int i, int newHi)
    {
        if (i < 0 || i + 1 >= (int) pad.layers.size()) return;
        auto& a = pad.layers[(size_t) i];
        auto& b = pad.layers[(size_t) i + 1];
        newHi = juce::jlimit (a.lo, b.hi - 1, newHi);
        a.hi = newHi;
        b.lo = newHi + 1;
    }
}

} // namespace minigun
