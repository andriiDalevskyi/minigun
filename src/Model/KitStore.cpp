#include "KitStore.h"

namespace minigun
{

juce::File KitStore::defaultKitsRoot()
{
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("Minigun Kits");
}

juce::File KitStore::resolveCopyDestination (const juce::File& samplesDir, const juce::File& sourceFile)
{
    auto baseName = sourceFile.getFileNameWithoutExtension();
    auto ext = sourceFile.getFileExtension();

    auto candidate = samplesDir.getChildFile (sourceFile.getFileName());
    int suffix = 2;
    while (candidate.existsAsFile())
    {
        // Same file already there (by full path match after copy) -> caller checks that
        // before calling this; here we just need a free name for a *different* file.
        candidate = samplesDir.getChildFile (baseName + "_" + juce::String (suffix) + ext);
        ++suffix;
    }
    return candidate;
}

bool KitStore::saveKit (Kit& kit, const juce::File& folder)
{
    auto samplesDir = folder.getChildFile ("samples");
    if (! samplesDir.createDirectory().wasOk())
        return false;

    for (auto& pad : kit.pads)
    {
        for (auto& layer : pad.layers)
        {
            for (auto& sampleRef : layer.samples)
            {
                auto& src = sampleRef.file;

                // Already inside folder/samples -> leave as-is.
                if (src.isAChildOf (samplesDir))
                    continue;

                if (! src.existsAsFile())
                    continue; // missing sample: nothing to copy, leave the (broken) reference

                // Same path already present under samplesDir with identical content path? We
                // only skip the copy when the exact same absolute file has already been placed
                // there in this save pass (tracked implicitly: if a file with the same name and
                // same full path already resolved to a location inside samplesDir, src would
                // already be a child of samplesDir and we'd have continued above). So here src
                // is always outside samplesDir and needs copying, unless a file with the same
                // name inside samplesDir is in fact the very same file (e.g. resaving into a
                // folder that already contains an identically named copy from a previous save).
                auto sameNameExisting = samplesDir.getChildFile (src.getFileName());
                juce::File dest;

                if (sameNameExisting.existsAsFile()
                    && sameNameExisting.getSize() == src.getSize()
                    && sameNameExisting.hasIdenticalContentTo (src))
                {
                    dest = sameNameExisting; // same content: reuse, skip copy
                }
                else
                {
                    dest = resolveCopyDestination (samplesDir, src);
                    if (! src.copyFileTo (dest))
                        continue; // leave original reference on failure
                }

                sampleRef.file = dest;
            }
        }
    }

    kit.folder = folder;

    auto json = kit.toJsonString (folder);
    return folder.getChildFile ("kit.json").replaceWithText (json);
}

bool KitStore::loadKit (const juce::File& folder, Kit& outKit)
{
    auto jsonFile = folder.getChildFile ("kit.json");
    if (! jsonFile.existsAsFile())
        return false;

    auto text = jsonFile.loadFileAsString();
    Kit fresh;
    if (! fresh.fromJsonString (text, folder))
        return false;

    fresh.folder = folder;
    outKit = std::move (fresh);
    return true;
}

} // namespace minigun
