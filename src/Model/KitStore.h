// Minigun drum sampler - Copyright (C) 2026 Andrii Dalevskyi (Dallas Audio)
// SPDX-License-Identifier: AGPL-3.0-or-later
// This file is part of Minigun, released under the GNU AGPL v3.0 or later; see LICENSE.

#pragma once
#include "KitModel.h"

namespace minigun
{

/** Reads/writes a kit folder on disk (kit.json + samples/ subfolder).
    Message-thread only. */
class KitStore
{
public:
    /** Copies every sample not already under folder/samples/ into it (handling filename
        collisions), rewrites the kit's SampleRef::file entries to the new locations,
        sets kit.folder = folder, and writes folder/kit.json with paths relative to folder.
        Returns false on I/O failure (folder could not be created, json could not be written). */
    static bool saveKit (Kit& kit, const juce::File& folder);

    /** Reads folder/kit.json and resolves relative sample paths against folder.
        Returns false if kit.json is missing or invalid. */
    static bool loadKit (const juce::File& folder, Kit& outKit);

    /** File::getSpecialLocation(userDocumentsDirectory)/"Minigun Kits" */
    static juce::File defaultKitsRoot();

private:
    static juce::File resolveCopyDestination (const juce::File& samplesDir, const juce::File& sourceFile);
};

} // namespace minigun
