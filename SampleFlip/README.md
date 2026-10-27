# SAMPLE FLIP — VST3 instrument (prototype)

Windows 64-bit JUCE project. Loads a sample, divides it into 16 equal slices, triggers chops via MIDI, and generates a looping 16-step pattern. Lo-fi/house selection changes the generated rhythmic slice pattern. Includes pitch, speed, filter, saturation, bit reduction and swing controls.

## Build
Install Visual Studio 2022 with **Desktop development with C++**, Git, and CMake. Internet required for first build to fetch JUCE 8.0.10. Run `BUILD_WINDOWS.bat`. Copy the resulting `Sample Flip.vst3` bundle to `C:\Program Files\Common Files\VST3\` and rescan plugins in Ableton. Alternatively run the Standalone target from CMake.

## Use
Insert on a MIDI track. Drop an audio file onto the interface, select a style, click GENERATE FLIP then PLAY SEQUENCE, or play MIDI notes 36–51. The sample is cut into 16 equal-length slices, not transient-detected. The playback engine uses resampling: changing pitch also changes slice duration; it is **not** independent high-quality time-stretch. The sequencer runs on its own BPM and does not yet sync to DAW transport. The plugin does not generate drums or export audio itself; record its output in your DAW. Load WAV/AIFF for most predictable results.

This is an experimental prototype, not a production-tested binary. The Windows build must be compiled on a Windows machine with the tools above.
