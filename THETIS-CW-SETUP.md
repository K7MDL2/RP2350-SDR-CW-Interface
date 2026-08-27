# Using the CW keyer with Thetis

## Configure MIDI

1. Connect the RP2350 interface and start Thetis.
2. Open **Setup > Serial/Network/Midi CAT > MIDI**.
3. Select **RP2350 SDR MIDI**, enable MIDI, and open **Configure MIDI**.
4. Import `thetis-keyer-note17.m2c`, or map MIDI note 17 manually as:
   - control type: **Button**;
   - command: **CW Straight Key** (called **Key Down** in some Thetis versions);
   - minimum value: `0`;
   - maximum value: `127`.
5. Save the mapping and click **Apply**.

The keyer sends note 17 with value `127` for key-down and value `0` for
key-up. Thetis may display both events as **Note On**; a Note On event with
value zero is the standard MIDI equivalent of Note Off.

Do not map MIDI note 18 in Thetis. The firmware provides note 18 as a
momentary PTT signal for applications such as piHPSDR, but Thetis offers a MOX
toggle rather than a matching momentary PTT command.

## Configure CW transmission

1. Select **CWL** or **CWU**.
2. Open **Setup > DSP > CW** and disable **Iambic** when the RP2350 performs
   the keying.
3. Enable the Thetis sidetone if required.
4. Select **Semi Break-In** and start with a delay of about 250 to 400 ms.
5. Leave **MOX off**. Semi Break-In automatically switches Thetis to transmit
   on key-down and back to receive after the delay.
6. Set a low Drive level for the first test and use a dummy load.

The sidetone heard through the computer can have some latency. This does not
necessarily indicate poor RF keying, and the keyer should continue to work at
higher CW speeds.

## Test with RX2 and VAC2

RX2 can be used to see and hear the transmitted CW signal:

1. Enable **RX2** and tune it to the transmitted signal.
2. Enable **VAC2** and select the laptop speakers as its playback destination.
3. Keep the RX2 input safely isolated from the transmitter. Use a dummy load
   and only enough received leakage or attenuation for a clean test signal.
4. Press the key. The Thetis TX indication and RF power meter should respond,
   while the CW signal appears on the RX2 display and is heard through VAC2.
5. Release the key. Thetis returns to receive after the Semi Break-In delay.

VAC2 runs directly through the computer and not through the RP2350 audio
output. A small audible delay is therefore normal. If RX2/VAC2 becomes silent
during transmit, check the Thetis duplex and VAC2 mute-on-transmit settings;
available options depend on the radio hardware and Thetis version.

> **RF safety:** never connect the transmitter output directly to an RX2 input.
> Use a dummy load, suitable attenuation, and adequate isolation to prevent
> receiver damage.
