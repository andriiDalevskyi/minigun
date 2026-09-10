// Minigun drum sampler - Copyright (C) 2026 Andrii Dalevskyi (Dallas Audio)
// SPDX-License-Identifier: AGPL-3.0-or-later
// This file is part of Minigun, released under the GNU AGPL v3.0 or later; see LICENSE.

#include "EngineKit.h"

namespace minigun
{

std::shared_ptr<const EngineKit> EngineKit::build (const Kit& kit, SampleLoader& loader)
{
    auto result = std::make_shared<EngineKit>();

    for (int i = 0; i < kNumPads; ++i)
    {
        auto& src = kit.pads[(size_t) i];
        auto& dst = result->pads[(size_t) i];

        dst.note = src.note;
        dst.mode = src.mode;
        dst.chokeGroup = src.chokeGroup;
        dst.volumeDb = src.volumeDb;
        dst.pan = src.pan;
        dst.pitchSemitones = src.pitchSemitones;
        dst.attackMs = src.attackMs;
        dst.decayMs = src.decayMs;
        dst.releaseMs = src.releaseMs;
        dst.output = src.output;
        dst.outputMode = src.outputMode;
        dst.monoSum = src.monoSum;
        dst.velocityToVolume = src.velocityToVolume;

        dst.layers.reserve (src.layers.size());
        for (auto& srcLayer : src.layers)
        {
            EngineLayer layer;
            layer.lo = srcLayer.lo;
            layer.hi = srcLayer.hi;
            layer.samples.reserve (srcLayer.samples.size());

            for (auto& ref : srcLayer.samples)
            {
                if (auto loaded = loader.load (ref.file))
                    layer.samples.push_back (std::move (loaded));
                // missing/unreadable samples are skipped in the engine snapshot
            }

            dst.layers.push_back (std::move (layer));
        }
    }

    return result;
}

} // namespace minigun
