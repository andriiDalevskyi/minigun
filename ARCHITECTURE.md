# Minigun — lightweight VST3 drum sampler (JUCE 8)

Fixed spec agreed with the user. Implementers: follow this file exactly; do not add features.

## Toolchain
- JUCE 8.0.4 at `external/JUCE` (git clone, not a submodule).
- CMake ≥ 3.22, MSVC 2022 Build Tools (x64), Windows SDK 10.0.26100.
- Configure: `cmake -S . -B build -G "Visual Studio 17 2022" -A x64`
- Build: `cmake --build build --config Release --target Minigun_VST3 Minigun_Standalone`
- Targets: VST3 + Standalone. `COPY_PLUGIN_AFTER_BUILD FALSE` — copying to `C:\Program Files\Common Files\VST3` needs admin rights; do it manually (xcopy from an elevated shell).
- C++20. Warnings as errors OFF. `JUCE_USE_MP3AUDIOFORMAT=1`, `JUCE_USE_OGGVORBIS=1`, `JUCE_USE_FLAC=1`, `JUCE_USE_WINDOWS_MEDIA_FORMAT=1`, `JUCE_WEB_BROWSER=0`, `JUCE_USE_CURL=0`, `JUCE_VST3_CAN_REPLACE_VST2=0`.
- Plugin: company "Dallas Audio", code `Dlas`, plugin code `Mngn`, `IS_SYNTH TRUE`, `NEEDS_MIDI_INPUT TRUE`, `IS_MIDI_EFFECT FALSE`. Outputs: Main + 16 aux stereo (see v0.2).

## Source layout
```
CMakeLists.txt
src/
  PluginProcessor.h/.cpp        (owner of model + engine, state save/load)
  PluginEditor.h/.cpp           (top-level layout, 1200x720 fixed)
  Model/KitModel.h              (SHARED CONTRACT — data structs + JSON, see below; already written)
  Model/KitStore.h/.cpp         (save kit folder with sample copy, load kit folder)
  Engine/LoadedSample.h         (decoded audio in memory)
  Engine/SampleLoader.h/.cpp    (file -> LoadedSample, cache by absolute path)
  Engine/EngineKit.h/.cpp       (immutable snapshot of Kit + LoadedSamples for the audio thread)
  Engine/Voice.h/.cpp           (one playing sample: resampling, pitch, ADR, gain/pan)
  Engine/SamplerEngine.h/.cpp   (32 voices, note->pads, layer select, RR/Random, choke, preview)
  UI/MinigunLookAndFeel.h/.cpp  (colours, fonts, knob/button drawing)
  UI/PadComponent.h/.cpp        (one pad: states empty/loaded/selected/hit/dragover)
  UI/PadGrid.h/.cpp             (4x4 grid, MPC order: pad 1 bottom-left, pad 13 top-left)
  UI/PadEditorPanel.h/.cpp      (name, note, RR/RND, choke, 6 knobs, MIDI learn)
  UI/LayerEditor.h/.cpp         (velocity range bar with draggable dividers + layer rows with sample chips)
  UI/BrowserPanel.h/.cpp        (file tree/list, preview toggle, waveform, "Add to Lx")
  UI/HeaderBar.h/.cpp           (logo, kit LCD, Save/Load kit, MIDI LED, meter, master knob)
```

## Data model (src/Model/KitModel.h — the contract)
See the header. Summary:
- `Kit` has `name`, `folder` (juce::File, may be invalid for unsaved kits), `pads[16]`.
- `Pad`: `name`, `note` (0..127, default 36+index), `mode` (RoundRobin|Random), `chokeGroup` (0 = none, 1..8), `volumeDb` (-60..+12, default 0), `pan` (-1..1), `pitchSemitones` (-12..12, float), `attackMs` (0..500), `decayMs` (-1 = full sample, else 1..5000), `releaseMs` (1..2000, default 120), `layers` (1..8 `VelocityLayer`).
- `VelocityLayer`: `lo`, `hi` (1..127, layers sorted ascending, contiguous, non-overlapping; the UI keeps them valid), `samples` (vector of `SampleRef`).
- `SampleRef`: `file` (absolute juce::File). In JSON written relative to kit folder when inside it, else absolute.
- JSON (kit.json) example:
```json
{ "format": "minigun-kit", "version": 1, "name": "808 Basic",
  "pads": [ { "index": 0, "name": "Kick", "note": 36, "mode": "rr", "choke": 0,
              "volumeDb": 0.0, "pan": 0.0, "pitch": 0.0, "attackMs": 0.0, "decayMs": -1, "releaseMs": 120,
              "layers": [ { "lo": 1, "hi": 127, "samples": ["samples/kick_01.wav", "samples/kick_02.wav"] } ] } ] }
```
Pads with no layers/samples are still written (so names/notes persist).

## Portability rule (critical, user-stressed)
`KitStore::saveKit(kit, folder)`:
1. Create `folder/samples/`.
2. For each SampleRef not already inside `folder/samples/`: copy the file there. Filename collision with a *different* file → append `_2`, `_3`. Same path already present → skip copy.
3. Update `kit.pads[*].layers[*].samples[*].file` to the new location, set `kit.folder = folder`.
4. Write `folder/kit.json` with relative paths.
`KitStore::loadKit(folder)` reads kit.json, resolves relative paths against folder. Missing files are kept in the model (flagged `missing` at load time by SampleLoader) so the user can see and fix them.
Default kits root: `File::getSpecialLocation(userDocumentsDirectory)/"Minigun Kits"`. "Save kit" = FileChooser to pick/create a folder (browse for directory) under that root by default; if the kit already has a folder, save there directly without a dialog (Shift-click or a "Save as…" isn't required in v0.1).

## Processor ↔ UI contract (PluginProcessor.h)
- `Kit& getKit()` — message-thread model. After ANY edit the UI calls `processor.kitEdited()` which: (a) triggers async sample loading for new files via `SampleLoader` (loads synchronously on the message thread — files are drum hits, small; keep it simple), (b) builds a new `std::shared_ptr<const EngineKit>` and publishes it to the audio thread via `juce::SpinLock`-guarded swap (audio thread uses `ScopedTryLock`; on failure keeps the previous snapshot), (c) calls `sendChangeMessage()` (processor is a `juce::ChangeBroadcaster`) so all panels refresh.
- `int getSelectedPad()` / `setSelectedPad(int)` — selection lives in the processor so it survives editor reopen.
- `void triggerPad(int padIndex, int velocity)` — from UI click (velocity from click Y within the pad: top = 127, bottom = 1). Enqueued to a lock-free `juce::AbstractFifo` of MIDI-like events consumed in processBlock.
- `void previewFile(const juce::File&)` / `void stopPreview()` — browser preview: load via SampleLoader, publish as `std::shared_ptr<const LoadedSample>` (same SpinLock swap); engine plays it on a dedicated preview voice at 0 dB, bypassing pads. Preview is played through the plugin output.
- `int getAndClearPadHit(int padIndex)` — returns velocity of the last hit since the last call (audio thread writes `std::atomic<int> padHits[16]` on every trigger; UI polls at 30 Hz to light pads).
- `void armMidiLearn(int padIndex)` / `bool isLearnArmed()` — next note-on assigns its note to that pad (engine sets atomic `learnedNote`, message-thread timer in processor applies it and calls kitEdited()).
- `juce::AudioProcessorValueTreeState apvts` with ONE parameter: `master` (gain, dB, -60..+6, default 0). Everything else lives in the Kit (not automatable in v0.1).
- `getStateInformation` → XML root `<Minigun>` with `master` attribute and a child `kit` holding the kit JSON string (absolute paths + `folder` attribute). `setStateInformation` restores and calls kitEdited().
- Output level: `std::atomic<float> outputPeakL/R` updated per block for the header meter.

## Engine behaviour
- `EngineKit` (immutable): per pad → per layer → vector<shared_ptr<const LoadedSample>> (missing samples skipped), plus a copy of pad params; per-pad `std::atomic<int> rrCounter` lives in `SamplerEngine`, not in the snapshot.
- `LoadedSample`: `juce::AudioBuffer<float>` (1 or 2 channels), `sampleRate`, `file`.
- Note-on (channel ignored): for EVERY pad whose `note` matches (several pads may share a note): pick layer where `lo <= vel <= hi` (if none — nearest layer); pick sample by mode (RR: counter++ % n; Random: juce::Random); if `chokeGroup != 0` release all voices whose pad has the same choke group; start a voice. Note-off is ignored (one-shot).
- Voice: playback ratio = `sample.sampleRate / hostSampleRate * 2^(pitch/12)`; linear interpolation; mono samples duplicated to both channels; gain = `Decibels::decibelsToGain(volumeDb) * velocity/127` (linear velocity curve); constant-power pan. Envelope: attack (linear ramp), then hold until `decayMs` elapsed (or until sample end when -1), then release ramp (`releaseMs`); choke → immediate release ramp. Voice ends when sample ends or release finishes. 32 voices; steal the oldest.
- Master gain applied after summing. Peak metering per block.
- processBlock must not allocate, lock (except ScopedTryLock), or touch juce::File.

## UI (match design/Main.dc.html exactly — colours, sizes, layout)
Palette: body gradient `#23262b → #191b1f`; panel `#1f2226` border `#2c3036`; LCD `#0e1011` text amber `#f2a33a`; teal accent `#38c7c1`; text `#d6d3cc`; label `#8b8f97`; pad rubber `#e6e3dd → #d3cfc8`; empty pad `#d2cfc9 → #bfbbb4`; hit pad `#ffd08a → #f2a33a` with amber glow; selected = 2 px teal outline offset 3 px; LED strip on pad = layer count (max 8 LEDs, 10x4 px).
Fonts: JUCE default sans (bold, condensed look via `withExtraKerningFactor` not needed) — use `juce::FontOptions` with the default typeface; sizes per the mockup. Uppercase labels 11 px with letter spacing.
Layout (1200x720, fixed, no resize): header 56 px; body padding 20/24; pad area 508 px wide (4×118 + 3×12 gap); middle column 360 px (PadEditorPanel above, LayerEditor filling the rest); BrowserPanel 264 px on the right; footer 28 px with kit folder path and "N voices · CPU".
Interactions:
- Pad click → `triggerPad`, and selects the pad. Drag files from Explorer onto a pad → all dropped files become ONE new layer on that pad (if the pad is empty, that layer spans 1..127; else append a top layer splitting the previous top range in half). Pad also accepts drags from the BrowserPanel (same behaviour).
- PadEditorPanel: name (editable TextEditor styled as LCD), note LCD with ‹ › buttons (also mouse wheel), RR/RND segmented toggle, choke LCD with ‹ › (—,1..8), MIDI learn button (lit while armed), 6 rotary knobs Vol/Pan/Pitch/Atk/Dec/Rel with value labels ("Full" for decay -1, "C" for pan 0).
- LayerEditor: range bar 34 px; dividers (teal 3 px) draggable, min layer width 1; "+ Layer" splits the top layer; rows per layer (top layer first) with sample chips (`filename ×`), "+ add" chip opens a FileChooser (multi-select), right-click on a layer row → "Delete layer" (merges its range into the neighbour). Clicking a chip previews that sample.
- BrowserPanel: root chooser (‹ up, path LCD), `juce::FileListComponent` or a custom list styled like the mockup; single click on an audio file → preview when Preview toggle is on; double-click → add to the selected pad's selected layer (the "Add to Lx" button does the same; Lx = layer selected in LayerEditor, default top). Waveform thumbnail (`juce::AudioThumbnail`) + "44.1k · 24b · 0.82 s" info line. Remembers last directory in `juce::PropertiesFile` (app name "Minigun").
- HeaderBar: MINIGUN wordmark, kit LCD (name · N pads · N smp), Save kit / Load kit buttons, MIDI LED (lit 100 ms after any note), 8-segment stereo peak meter, master knob bound to apvts `master`.
- Timer 30 Hz in PluginEditor: pad hit glow (decay ~150 ms), meter, MIDI LED.
- Editor is a `juce::FileDragAndDropTarget` on pads; dragged audio extensions: wav aif aiff flac ogg mp3 wma.

## v0.2 additions (2026-09-09, user feedback)

### Multi-output routing
- Buses: bus 0 "Main" stereo + 16 aux stereo buses "Out 1".."Out 16", ALL enabled by default (`withOutput (name, stereo, true)`): REAPER activates exactly as many buses as the track has channel pairs and never activates a bus that is disabled by default. Footer shows "outs N/17" = buses currently enabled by the host. `isBusesLayoutSupported`: Main must be stereo; every aux bus must be stereo or disabled; no inputs.
- `Pad::output` (int, 0 = Main, 1..16 = aux bus) — JSON key `"output"`, default 0. `EnginePad::output` mirrors it.
- Engine: each voice renders into the bus of its pad. If that aux bus is disabled in the current layout (host did not enable it) the voice falls back to Main. Preview voice always renders to Main. Master gain applies to every bus. Peak meter = max over all enabled buses.
- processBlock: clear the whole buffer, then use `getBusBuffer (buffer, false, busIndex)` for each enabled bus (AudioBuffer with ≤32 channels uses preallocated channel-pointer storage — no heap allocation; still never allocate anything else on the audio thread).
- UI: PadEditorPanel gets an "OUT" LCD stepper (‹ ›, mouse wheel) between CHOKE and LEARN: shows "MAIN" or "OUT n". Standalone shows only Main.

### Drag & drop onto the velocity layer editor
- `LayerEditor` implements `juce::FileDragAndDropTarget` (Explorer) and `juce::DragAndDropTarget` (browser rows; description format = the `juce::var` array of paths that `PadComponent::filesFromDragDescription` parses — make that helper and `hasAudioExtension` public static and reuse them).
- Drop target resolution: over a layer row → that layer; over a range-bar segment → that layer; anywhere else in the panel → the selected layer (if the pad has no layers, a new full-range layer is created). While dragging, the target row/segment is highlighted with a 2px dashed teal outline; the whole panel gets a faint teal border when the drop would go to the selected layer.
- The drop calls `std::function<void (int layerIndex, juce::Array<juce::File>)> onFilesDroppedOnLayer`; `PluginEditor` handles it via `handleAddToLayer (files, layerIndex)` (existing logic incl. auto-naming, `kitEdited()`, `refreshAll()`).

## v0.3 additions (2026-09-10, user feedback): pad editor controls & mono outputs

### Model (KitModel.h)
- `enum class OutputMode { Stereo, MonoLeft, MonoRight }` and `Pad::outputMode` (default Stereo). JSON key `"outMode"`: `"stereo" | "monoL" | "monoR"` (missing → stereo).
- `Pad::monoSum` (bool, default true). JSON key `"monoSum"`. Meaning: when the pad goes to a MONO channel and the sample is stereo, `true` = sum both sample channels ((L+R)*0.5), `false` = take only the sample channel matching the side (L for MonoLeft, R for MonoRight). Ignored for stereo output and for mono samples.
- `Pad::output` keeps its meaning (0 = Main bus, 1..16 = aux bus). Channel numbering shown to the user: Main = "Main", bus n = channels (2n-1, 2n) → labels "Out 1-2", "Out 3-4", … "Out 31-32"; mono labels "Out 1", "Out 2", … (odd = L side of the pair, even = R side).

### Engine
- `EnginePad` mirrors `outputMode` and `monoSum`; `Voice::start` receives them.
- Voice rendering: Stereo → unchanged (constant-power pan). MonoLeft/MonoRight → pan is ignored; the mono signal (per `monoSum`, see above; mono samples are used as-is) is written with the voice gain into channel 0 (MonoLeft) or channel 1 (MonoRight) of the pad's bus buffer only. Fallback to Main when the bus is unusable keeps the mono behaviour (writes to Main's L or R channel).
- Preview stays stereo on Main.

### UI — PadEditorPanel (replaces the LCD steppers; keep the LCD look via LookAndFeel)
- Row 2: **NOTE** = `juce::ComboBox` with all 128 notes, item text "C1 · 36" style (`noteToShortName + " · " + number`), plus a small `juce::TextEditor` (LCD, 44 px wide, digits only, 0..127) showing the number; editing either updates the other and the model. Mouse wheel over the combo still steps ±1. **CHOKE** = ComboBox ("—", "1".."8"). **LEARN** button unchanged.
- Row 3 (new, adds 34 px to `kPreferredHeight`): **OUT** = ComboBox with 17 items ("Main", "Out 1-2", …, "Out 31-32"); **mode** = 3-button segmented control "ST | L | R" (same style as RR/RND) → outputMode; **SUM** = small toggle button (text "SUM", lit teal when on) → monoSum; the SUM button is enabled only when the mode is L or R. When the mode is mono the OUT combo shows mono labels ("Main L"/"Main R", "Out 1", "Out 4", …) — implement by rebuilding the combo's item texts on mode change (item ids stay 1..17 = bus index + 1).
- Knob row moves down by 34 px. Everything must still fit in the 360-px-wide middle column; LayerEditor simply gets 34 px less.
- LookAndFeel: style `juce::ComboBox` like the LCDs (bg `lcdBg`, 1 px `lcdBorder`, 4 px radius, amber mono text 13 px, small grey chevron on the right; teal border when the popup is open) and the popup menu dark (`panel` background, `text` items, amber/teal highlight). Override `drawComboBox`, `getComboBoxFont`, `positionComboBoxText`, `drawPopupMenuBackground`, `drawPopupMenuItem` (or set the PopupMenu colour ids) so all combos in the plugin share the look.
- Every change → `processor.kitEdited()`; `refresh()` re-reads all controls from the model without firing callbacks (`dontSendNotification`).
