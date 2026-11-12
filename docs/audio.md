# Audio

BRICK plays sound as a stream of 16-bit PCM frames. The layers are the same
as for the display and the network: an interface the application uses,
portable classes in `libs/core` that produce and pump the samples, and one
output per platform.

```text
application        PcmPlayer  <-  IPcmSource: ToneSource, WavSource (IByteStorage: MemoryBytes, flash, ...)
                       |                                                    (libs/core, portable)
interface          IPcmOutput                                               (brick/interfaces/audio)
                       |
platforms          I2sPcmOutput (+ II2sCodec: Es8311Codec)   ESP32-P4 panels, ESP32-S3 panel
                   DacPcmOutput                              CYD (classic ESP32, 8-bit DAC)
                   PcPcmOutput                               PC simulator (counts frames, no sound)
```

Nothing blocks: `IPcmOutput::write()` copies what fits into the DMA buffers
and returns, and `PcmPlayer::update()`, called from the loop, tops them up.
A board starts its output only when a sound plays and the application stops
it when nothing has played for a while (the amplifier is off between sounds).

The older `IAudioOutput` / `IAudioPlayer` (8-bit samples written one by one
from a timer, `PeriodicAudioPlayer`, `WavDecoder` into an `AudioBuffer`) and
`IBuzzer` / `BeepSequence` stay for the PWM output and buzzers.

## Interface (`brick/interfaces/audio/IPcmOutput.h`)

| Member | Meaning |
|---|---|
| `start(PcmFormat)` | opens the output at a sample rate and 1 or 2 channels; again with another format reconfigures it |
| `write(frames, count, written)` | copies up to `count` frames (interleaved for stereo) without waiting; `Status::busy` when nothing fit |
| `set_volume(percent)` / `volume()` | 0..100 %; kept while the output is stopped |
| `stop()` | silence, amplifier off, buffers released |
| `format()` | the running format; `sample_rate == 0` when stopped |

## Core (`brick/core/audio/PcmSource.h`)

- `IPcmSource`: `format()`, `read(frames, capacity, count)` (`Status::no_data`
  at the end), `rewind()`.
- `ToneSource(frequency, duration_ms, rate, Waveform::sine | square,
  amplitude %)`: a tone from a 256-entry sine table with 5 ms fades in and out
  (no clicks).
- `WavSource(IByteStorage&, address, size)`: a PCM WAV (8 or 16-bit, mono or
  stereo) read in pieces from any byte storage: `MemoryBytes` for a file
  compiled into the firmware, a flash partition, an EEPROM. `open()` checks
  the header and skips chunks other than `fmt ` and `data`.
- `PcmPlayer(IPcmOutput&)`: `play(source)` (restarts the output only when the
  format differs), `update()` (256 frames per piece while the output takes
  them), `stop()`, `playing()`, `frames_sent()`.

```cpp
brick::core::audio::MemoryBytes chime_bytes(kChimeWav, sizeof(kChimeWav));
brick::core::audio::WavSource   chime(chime_bytes);
brick::core::audio::PcmPlayer   player(board.audio());

chime.open();
player.play(chime);
// loop:
player.update();
if (!player.playing() && idle_for_a_second)
    board.audio().stop();
```

## ESP32 outputs

### `I2sPcmOutput` (ESP32-P4, ESP32-S3)

An `i2s_std` channel (16-bit Philips, MCLK = 256 x rate, `auto_clear`: an
empty queue plays silence). It can drive:

- a codec (`II2sCodec`: `configure(rate)`, `set_volume()`, `set_muted()`),
  which then sets the volume; on start the output configures the codec,
  turns the amplifier on (`amplifier_gpio`), waits 60 ms and unmutes;
- an I2S amplifier without a codec: the volume scales the samples.

`Es8311Codec` is the ES8311 on the JC1060 and JC8012 panels (8 to 48 kHz,
MCLK = 256 x rate). Its volume is the DAC gain in 0.5 dB steps: 0 % mutes,
1..100 % is -59.5..-10 dB. Louder makes the panels' NS4150 amplifier clip.
The codec talks over `II2cRegisters`: `LegacyI2cRegisters` (the legacy I2C
driver, shared with the JC1060's GT911) or `MasterI2cRegisters` (an
`i2c_master` bus, shared with the JC8012's GSL3680). They are separate source
files because ESP-IDF does not link both I2C drivers into one application.
`Es8311I2sAudio<Registers>` bundles registers, codec and output for a board.

### `DacPcmOutput` (classic ESP32)

The ESP32's 8-bit DAC with DMA (`dac_continuous`, APLL clock, so any rate
from about 650 Hz). Stereo is mixed to mono and the volume scales the
samples (`full_scale` caps the level at volume 100).

The DMA runs in a ring from `start()` to `stop()`. `write()` puts samples into
a ring buffer (2048 samples) and the DMA interrupt copies them into the
buffer that just played. When no samples are queued, the interrupt repeats the
last level, so the output goes silent instead of replaying old buffers. `start()` and
`stop()` ramp between 0 V and the mid level in 20 ms, so the amplifier does
not click. (The driver's synchronous `dac_continuous_write()` with a zero
timeout takes a whole DMA buffer for every small write, lets the DMA stop
between writes and logs an error each time it is full; it is not used.)

## Boards

Each board has `audio()` when its features set `audio = true`
(`brick::boards::common::audio_enabled<Features>`).

| Board | Output | Wiring | Default volume |
|---|---|---|---|
| JC1060 7″ (ESP32-P4) | `I2sPcmOutput` + ES8311 + NS4150 | MCLK 13, BCLK 12, LRCK 10, data 9; codec 0x18 on the touch I2C (GPIO7/8); amplifier GPIO11 | 80 % (-20 dB) |
| JC8012 10″ (ESP32-P4) | `I2sPcmOutput` + ES8311 + NS4150 | as JC1060; codec on the GSL3680's `i2c_master` bus; amplifier GPIO20 | 80 % (-20 dB) |
| ESP32-S3 4″ 480x480 | `I2sPcmOutput`, I2S amplifier | BCLK 1, LRCK 2, data 40 | 60 % |
| CYD 2.8″ (ESP32) | `DacPcmOutput` | GPIO26 (DAC 2) into the speaker connector's amplifier | 60 % |
| PC simulator | `PcPcmOutput` | takes frames at the sample rate (100 ms buffer), counts them and the peak; no sound | — |

On the JC1060 and JC8012 the codec shares the touch screen's I2C bus, so the
board needs `touch = true` for audio (checked at compile time).

## Tested

`audio_smoke` in brick-test-apps (scale, 440 Hz tone, WAV chime, volume
steps) on all four boards: the P4 panels play clean over the whole volume
range, the S3 panel too; the CYD plays clean with rare faint clicks from its
8-bit DAC and amplifier. The output takes the frames at the sample rate
(22050 Hz: 21950..22070 Hz measured over a sound).
