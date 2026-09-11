// Minigun drum sampler - Copyright (C) 2026 Andrii Dalevskyi (Dallas Audio)
// SPDX-License-Identifier: AGPL-3.0-or-later
// This file is part of Minigun, released under the GNU AGPL v3.0 or later; see LICENSE.

#pragma once
#include "LoadedSample.h"
#include "SampleLoader.h"
#include "../Model/KitModel.h"
#include <memory>
#include <vector>

namespace minigun
{

/** Immutable, audio-thread-safe snapshot of a Kit with decoded samples resolved.
    Built on the message thread (build() touches juce::File via the SampleLoader) and
    then published to the audio thread as a std::shared_ptr<const EngineKit>. */
struct EngineLayer
{
    int lo = 1;
    int hi = 127;
    std::vector<std::shared_ptr<const LoadedSample>> samples; // missing/unreadable samples omitted
};

struct EnginePad
{
    int note = 36;
    PlayMode mode = PlayMode::RoundRobin;
    int chokeGroup = 0;
    float volumeDb = 0.0f;
    float pan = 0.0f;
    float pitchSemitones = 0.0f;
    float attackMs = 0.0f;
    float decayMs = -1.0f;
    float releaseMs = 120.0f;
    int output = 0;
    OutputMode outputMode = OutputMode::Stereo;
    bool monoSum = true;
    bool velocityToVolume = true;
    float rndPitchCents = 0.0f; // already resolved: trigger mode substitutes its own amounts at build() time
    float rndVolDb = 0.0f;
    std::vector<EngineLayer> layers;
};

struct EngineKit
{
    std::array<EnginePad, kNumPads> pads;

    /** Builds a new immutable snapshot from the message-thread Kit model, resolving each
        SampleRef via the (cached) SampleLoader. Missing samples are skipped. */
    static std::shared_ptr<const EngineKit> build (const Kit& kit, SampleLoader& loader);
};

} // namespace minigun
