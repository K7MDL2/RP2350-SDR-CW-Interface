@echo off
echo Starting build - run cmake
cmake -S . -B build-audio-duplex-cdc-midi -G Ninja -DRP2350_USB_AUDIO_DUPLEX_CDC_MIDI=ON
REM cmake -S . -B build -G Ninja -DRP2350_USB_AUDIO_DUPLEX_CDC_MIDI=ON

echo " "
echo Do Compile
cmake --build build-audio-duplex-cdc-midi
REM cmake --build build

echo Build Done