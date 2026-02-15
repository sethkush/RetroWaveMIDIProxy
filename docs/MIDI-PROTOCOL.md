# RetroWaveMIDIProxy MIDI Protocol Specification

Direct Mode control protocol for RetroWave OPL3 hardware via standard MIDI messages.

## Table of Contents

- [Overview](#overview)
- [SysEx Messages](#sysex-messages)
  - [Common Format](#common-format)
  - [Register Write (7-bit)](#register-write-7-bit) — `0x01`
  - [Batch Write (7-bit)](#batch-write-7-bit) — `0x02`
  - [Register Write (8-bit)](#register-write-8-bit) — `0x03`
  - [Batch Write (8-bit)](#batch-write-8-bit) — `0x04`
  - [Patch Dump (Request)](#patch-dump-request) — `0x10`
  - [Patch Load](#patch-load) — `0x11`
  - [Reset All](#reset-all) — `0x20`
  - [Voice Config](#voice-config) — `0x30`
  - [Voice Query](#voice-query) — `0x31`
  - [Percussion Config](#percussion-config) — `0x32`
  - [Percussion Query](#percussion-query) — `0x33`
  - [Hardware Reset](#hardware-reset) — `0x7F`
- [RPN Control](#rpn-control)
  - [RPN Addressing](#rpn-addressing)
  - [Pitch Bend Sensitivity (RPN 0x0000)](#pitch-bend-sensitivity-rpn-0x0000)
- [NRPN Control](#nrpn-control)
  - [NRPN Addressing](#nrpn-addressing)
  - [Operator Parameters (MSB 0–3)](#operator-parameters-msb-03)
  - [Channel Parameters (MSB 4)](#channel-parameters-msb-4)
  - [Global Parameters (MSB 5)](#global-parameters-msb-5)
  - [MIDI-to-OPL3 Value Mapping](#midi-to-opl3-value-mapping)
- [Voice Allocation](#voice-allocation)
  - [Default Mapping](#default-mapping)
  - [Polyphony and Unison](#polyphony-and-unison)
  - [Voice Stealing](#voice-stealing)
  - [CC and NRPN Broadcasting](#cc-and-nrpn-broadcasting)
- [Standard MIDI Messages](#standard-midi-messages)
  - [Note On / Off](#note-on--off)
  - [Control Change](#control-change)
  - [Pitch Bend](#pitch-bend)
  - [Unsupported Messages](#unsupported-messages)
- [OPL3 Channel Layout](#opl3-channel-layout)
  - [2-Op Channels](#2-op-channels)
  - [4-Op Pairs](#4-op-pairs)
  - [Percussion Mode](#percussion-mode)

---

## Overview

Direct Mode provides real-time control of OPL3 registers over MIDI. Three mechanisms are available:

1. **SysEx** — Raw register access and bulk operations (patch dump/load, voice routing, percussion config).
2. **NRPN** — Parameter-level control of operators, channels, and global settings via CC99/98/6.
3. **Standard MIDI** — Note on/off, pitch bend, volume, pan, expression, sustain.

All SysEx messages use manufacturer ID `0x7D` (non-commercial/development use).

---

## SysEx Messages

### Common Format

All SysEx messages share this structure:

```
F0 7D <device-id> <command> [payload...] F7
```

| Byte | Description |
|------|-------------|
| `F0` | SysEx start |
| `7D` | Manufacturer ID (non-commercial) |
| `device-id` | `0x7F` = broadcast (all devices), or `0x00`–`0x7E` for specific device |
| `command` | See table below |
| `payload` | Command-specific data (all bytes 0x00–0x7F, MIDI-safe) |
| `F7` | SysEx end |

Device ID filtering: a message is accepted if the device-id in the message matches the receiver's device-id, or if either is `0x7F`.

### Command Summary

| Command | Hex | Direction | Description |
|---------|-----|-----------|-------------|
| RegWrite7 | `0x01` | In | Write one register (7-bit value) |
| BatchWrite7 | `0x02` | In | Write multiple registers (7-bit values) |
| RegWrite8 | `0x03` | In | Write one register (full 8-bit value, nibble-encoded) |
| BatchWrite8 | `0x04` | In | Write multiple registers (full 8-bit, nibble-encoded) |
| PatchDump | `0x10` | In | Request patch dump for a channel |
| PatchLoad | `0x11` | In/Out | Load patch data (also used as dump response) |
| ResetAll | `0x20` | In | Reset OPL3 to default direct-mode state |
| VoiceConfig | `0x30` | In/Out | Set voice allocation config (also used as query response) |
| VoiceQuery | `0x31` | In | Request voice allocation config |
| PercConfig | `0x32` | In/Out | Set percussion routing (also used as query response) |
| PercQuery | `0x33` | In | Request percussion routing |
| HWReset | `0x7F` | In | Hardware reset (writes to 0xFE/0xFF + reinit) |

---

### Register Write (7-bit)

Write a single OPL3 register. Value is limited to 0–127 (7-bit MIDI-safe).

```
F0 7D <dev> 01 <reg-hi> <reg-lo> <value> F7
```

| Byte | Range | Description |
|------|-------|-------------|
| `reg-hi` | `0x00`–`0x03` | Address bits 8–7 (high 2 bits, shifted) |
| `reg-lo` | `0x00`–`0x7F` | Address bits 6–0 |
| `value` | `0x00`–`0x7F` | Register value (7-bit) |

Address reconstruction: `addr = (reg-hi << 7) | reg-lo`

Valid OPL3 address range: `0x000`–`0x1FF`.

### Batch Write (7-bit)

Write multiple registers in a single message.

```
F0 7D <dev> 02 <count> [<reg-hi> <reg-lo> <value>]... F7
```

| Byte | Range | Description |
|------|-------|-------------|
| `count` | `0x01`–`0x7F` | Number of register writes |
| `reg-hi` | `0x00`–`0x03` | Per-write: address high |
| `reg-lo` | `0x00`–`0x7F` | Per-write: address low |
| `value` | `0x00`–`0x7F` | Per-write: value (7-bit) |

### Register Write (8-bit)

Write a single OPL3 register with full 8-bit value. The value is split into two nibbles to stay MIDI-safe.

```
F0 7D <dev> 03 <reg-hi> <reg-lo> <val-hi> <val-lo> F7
```

| Byte | Range | Description |
|------|-------|-------------|
| `reg-hi` | `0x00`–`0x03` | Address bits 8–7 |
| `reg-lo` | `0x00`–`0x7F` | Address bits 6–0 |
| `val-hi` | `0x00`–`0x0F` | Value bits 7–4 |
| `val-lo` | `0x00`–`0x0F` | Value bits 3–0 |

Value reconstruction: `value = (val-hi << 4) | val-lo`

### Batch Write (8-bit)

Write multiple registers with full 8-bit values.

```
F0 7D <dev> 04 <count> [<reg-hi> <reg-lo> <val-hi> <val-lo>]... F7
```

| Byte | Range | Description |
|------|-------|-------------|
| `count` | `0x01`–`0x7F` | Number of register writes |

Each write is 4 bytes: `reg-hi`, `reg-lo`, `val-hi`, `val-lo` (same encoding as Register Write 8-bit).

---

### Patch Dump (Request)

Request the current patch state for an OPL3 channel. The response is sent as a [Patch Load](#patch-load) message on the MIDI output port.

```
F0 7D <dev> 10 <channel> F7
```

| Byte | Range | Description |
|------|-------|-------------|
| `channel` | `0x00`–`0x11` | OPL3 channel index (0–17) |

The response mirrors the Patch Load format, so it can be saved and re-sent verbatim to restore the patch.

### Patch Load

Load a complete patch (operator settings + channel config) onto an OPL3 channel. Also used as the response format for Patch Dump.

Auto-detects 2-op vs 4-op based on payload length.

```
F0 7D <dev> 11 <channel> <operator-data...> <channel-data...> F7
```

| Byte | Range | Description |
|------|-------|-------------|
| `channel` | `0x00`–`0x11` | OPL3 channel index (0–17) |

**Operator data** — 2 or 4 operators, each encoded as 11 register bytes × 2 nibbles = 22 nibbles per operator:

| Register | OPL3 Addr | Description |
|----------|-----------|-------------|
| 0 | `0x20+off` | AM / Vibrato / EGT / KSR / Freq Mult |
| 1 | `0x40+off` | KSL / Total Level |
| 2 | `0x60+off` | Attack Rate / Decay Rate |
| 3 | `0x80+off` | Sustain Level / Release Rate |
| 4 | `0xE0+off` | Waveform Select |
| 5–10 | — | Reserved (zero) |

Each register byte is nibble-encoded as `<high-nibble> <low-nibble>`.

**Channel data** — follows the operator data:
- **2-op mode**: 2 nibbles (feedback/connection register `0xC0+ch`)
- **4-op mode**: 4 nibbles (primary `0xC0+ch` then paired `0xC0+partner`)

Only bits 3–0 (feedback + connection) are loaded from the channel data; pan bits (4–5) are preserved.

**Total payload sizes:**

| Mode | Operators | Op nibbles | Ch nibbles | Total nibbles |
|------|-----------|------------|------------|---------------|
| 2-op | 2 | 44 | 2 | 46 |
| 4-op | 4 | 88 | 4 | 92 |

---

### Reset All

Reset OPL3 to the default direct-mode state. Clears all registers and loads a default FM piano-like patch on all 18 channels:

- Both operators: EGT=1, Mult=1, sine waveform
- Modulator: TL=32 (partial modulation depth), AR=15, DR=4, SL=2, RR=4
- Carrier: TL=0 (set dynamically by volume), AR=15, DR=4, SL=2, RR=6
- Channel: Feedback=4, Connection=FM, both speakers enabled

```
F0 7D <dev> 20 F7
```

No payload. When received through VoiceAllocator, also resets all voice allocation state (releases sounding notes, clears drum state, restores default MIDI-to-OPL3 mapping).

### Voice Config

Configure voice allocation for a MIDI channel. Controls which OPL3 channels are assigned, unison count, detuning, and mode flags.

Also used as the response format for [Voice Query](#voice-query).

```
F0 7D <dev> 30 <midi-ch> <count> <opl3-ch-0> ... <opl3-ch-N> <unison> <detune> <flags> F7
```

| Byte | Range | Description |
|------|-------|-------------|
| `midi-ch` | `0x00`–`0x0F` | MIDI channel (0–15) |
| `count` | `0x00`–`0x12` | Number of assigned OPL3 channels (0–18) |
| `opl3-ch-*` | `0x00`–`0x11` | OPL3 channel indices |
| `unison` | `0x01`–`0x7F` | Unison voice count (1 = normal polyphony) |
| `detune` | `0x00`–`0x64` | Unison detune spread in cents (0–100) |
| `flags` | `0x00`–`0x03` | Bit 0: 4-op mode, Bit 1: pan split |

**Flags:**

| Bit | Name | Description |
|-----|------|-------------|
| 0 | `four_op` | Treat 4-op-capable channel pairs as single voice slots |
| 1 | `pan_split` | Spread unison voices across L/R stereo field |

Setting a voice config releases all sounding notes on the channel and deconflicts any OPL3 channels claimed by other MIDI channels.

### Voice Query

Request the current voice configuration for a MIDI channel. Response is sent as a [Voice Config](#voice-config) message.

```
F0 7D <dev> 31 <midi-ch> F7
```

| Byte | Range | Description |
|------|-------|-------------|
| `midi-ch` | `0x00`–`0x0F` | MIDI channel (0–15) |

### Percussion Config

Configure OPL3 percussion mode and drum-to-MIDI-channel routing.

Also used as the response format for [Percussion Query](#percussion-query).

```
F0 7D <dev> 32 <perc-mode> <bd-ch> <sd-ch> <tt-ch> <cy-ch> <hh-ch> F7
```

| Byte | Range | Description |
|------|-------|-------------|
| `perc-mode` | `0x00`–`0x7F` | `>= 0x40` = enable, `< 0x40` = disable |
| `bd-ch` | `0x00`–`0x0F` / `0x7F` | Bass Drum MIDI channel (`0x7F` = unassigned) |
| `sd-ch` | `0x00`–`0x0F` / `0x7F` | Snare Drum MIDI channel |
| `tt-ch` | `0x00`–`0x0F` / `0x7F` | Tom-Tom MIDI channel |
| `cy-ch` | `0x00`–`0x0F` / `0x7F` | Cymbal MIDI channel |
| `hh-ch` | `0x00`–`0x0F` / `0x7F` | Hi-Hat MIDI channel |

When percussion mode is enabled, OPL3 register `0xBD` bit 5 is set, and note on/off messages on assigned MIDI channels are routed to the corresponding drums instead of normal voice allocation.

### Percussion Query

Request the current percussion configuration. Response is sent as a [Percussion Config](#percussion-config) message.

```
F0 7D <dev> 33 F7
```

No payload.

### Hardware Reset

Perform a hardware reset by writing to registers `0xFE` and `0xFF`, then reinitializing to default state.

```
F0 7D <dev> 7F F7
```

No payload.

---

## RPN Control

### RPN Addressing

RPN (Registered Parameter Number) messages use the standard CC sequence:

| Step | CC | Value | Description |
|------|----|-------|-------------|
| 1 | CC 101 | MSB | RPN parameter MSB |
| 2 | CC 100 | LSB | RPN parameter LSB |
| 3 | CC 6 | value | Data Entry MSB |
| 4 | CC 38 | value | Data Entry LSB (optional) |

The MSB/LSB pair remains latched until changed. Setting either to `0x7F` deactivates the RPN.

**Mutual invalidation**: Setting NRPN address CCs (99/98) invalidates the active RPN, and setting RPN address CCs (101/100) invalidates the active NRPN. This prevents accidental cross-talk between the two parameter spaces.

### Pitch Bend Sensitivity (RPN 0x0000)

The only supported RPN. Controls the range of the pitch bend wheel.

| CC | Value | Description |
|----|-------|-------------|
| CC 101 | `0x00` | RPN MSB = 0 |
| CC 100 | `0x00` | RPN LSB = 0 |
| CC 6 | `0x00`–`0x7F` | Semitones (default: 2) |
| CC 38 | `0x00`–`0x63` | Cents (0–99, default: 0) |

Total range = semitones + cents/100. For example, CC 6 = 12 and CC 38 = 0 sets ±12 semitones (one octave). CC 6 = 2 and CC 38 = 50 sets ±2.5 semitones.

Per-channel setting. Reset to ±2 semitones on init.

---

## NRPN Control

### NRPN Addressing

NRPN messages use the standard CC sequence:

| Step | CC | Value | Description |
|------|----|-------|-------------|
| 1 | CC 99 | MSB | Parameter group (0–5) |
| 2 | CC 98 | LSB | Parameter index within group |
| 3 | CC 6 | value | Data Entry — triggers the write |

The MSB/LSB pair remains latched until changed. Sending CC 6 applies the value to the currently addressed parameter. Setting MSB or LSB to `0x7F` deactivates the NRPN (data entry is ignored until both are set to valid values).

**Note**: NRPN and RPN share the Data Entry CCs (6 and 38). Which parameter space is active depends on whether the last address CCs sent were 99/98 (NRPN) or 101/100 (RPN).

### Operator Parameters (MSB 0–3)

MSB selects the operator:

| MSB | Operator | Role |
|-----|----------|------|
| 0 | OP1 | Modulator (2-op) / Modulator 1 (4-op) |
| 1 | OP2 | Carrier (2-op) / Carrier 1 (4-op) |
| 2 | OP3 | Modulator 2 (4-op paired channel) |
| 3 | OP4 | Carrier 2 (4-op paired channel) |

OP3/OP4 require the channel to be part of a 4-op pair (channels 0–5 or 9–14).

| LSB | Parameter | OPL3 Register | Bits | MIDI → OPL3 |
|-----|-----------|---------------|------|-------------|
| 0 | Attack Rate | `0x60+off` | 7–4 | `val >> 3` |
| 1 | Decay Rate | `0x60+off` | 3–0 | `val >> 3` |
| 2 | Sustain Level | `0x80+off` | 7–4 | `val >> 3` |
| 3 | Release Rate | `0x80+off` | 3–0 | `val >> 3` |
| 4 | Waveform | `0xE0+off` | 2–0 | `val >> 4` |
| 5 | Freq Multiplier | `0x20+off` | 3–0 | `val >> 3` |
| 6 | Output Level | `0x40+off` | 5–0 | `val >> 1` |
| 7 | Key Scale Level | `0x40+off` | 7–6 | `(val >> 5) << 6` |
| 8 | Tremolo (AM) | `0x20+off` | 7 | `val >= 64` → on |
| 9 | Vibrato | `0x20+off` | 6 | `val >= 64` → on |
| 10 | Sustain Mode (EGT) | `0x20+off` | 5 | `val >= 64` → on |
| 11 | Key Scale Rate | `0x20+off` | 4 | `val >= 64` → on |

### Channel Parameters (MSB 4)

| LSB | Parameter | OPL3 Register | Bits | MIDI → OPL3 |
|-----|-----------|---------------|------|-------------|
| 0 | Feedback | `0xC0+ch` | 3–1 | `(val >> 4) << 1` |
| 1 | Connection (FM/AM) | `0xC0+ch` | 0 | `val >= 64` → AM |
| 2 | Pan Left | `0xC0+ch` | 4 | `val >= 64` → on |
| 3 | Pan Right | `0xC0+ch` | 5 | `val >= 64` → on |
| 4 | 4-Op Enable | `0x104` | pair bit | `val >= 64` → on |
| 5 | Secondary Connection | `0xC0+partner` | 0 | `val >= 64` → AM |

4-Op Enable (LSB 4) sets the corresponding bit in register `0x104` for the channel's 4-op pair. Only works on 4-op-capable channels (0–5, 9–14).

Secondary Connection (LSB 5) controls the connection type of the paired channel in 4-op mode.

### Global Parameters (MSB 5)

Global parameters are not channel-specific. They affect register `0xBD`.

| LSB | Parameter | OPL3 Register | Bits | MIDI → OPL3 |
|-----|-----------|---------------|------|-------------|
| 0 | Tremolo Depth | `0xBD` | 7 | `val >= 64` → deep |
| 1 | Vibrato Depth | `0xBD` | 6 | `val >= 64` → deep |
| 2 | Percussion Mode | `0xBD` | 5 | `val >= 64` → on |

### MIDI-to-OPL3 Value Mapping

MIDI values are 7-bit (0–127). OPL3 register fields vary in width. The conversion rules are:

| OPL3 Width | Conversion | MIDI Range → OPL3 Range |
|------------|------------|-------------------------|
| 4-bit (0–15) | `val >> 3` | 0–127 → 0–15 |
| 3-bit (0–7) | `val >> 4` | 0–127 → 0–7 |
| 6-bit (0–63) | `val >> 1` | 0–127 → 0–63 |
| 2-bit (0–3) | `(val >> 5) << 6` | 0–127 → 0/64/128/192 (in register position) |
| 1-bit | `val >= 64` | 0–63 = off, 64–127 = on |

---

## Voice Allocation

The VoiceAllocator sits between MIDI input and DirectMode, providing polyphonic voice allocation, unison detuning, stereo pan split, and percussion routing. It intercepts note, CC, pitch bend, and voice-related SysEx messages, routing them through DirectMode's per-channel API. All other SysEx messages are forwarded to DirectMode unmodified.

### Default Mapping

On startup, MIDI channels 0–15 are mapped 1:1 to OPL3 channels 0–15, with unison count 1 and detune spread 10 cents. OPL3 channels 16–17 are unassigned by default but can be added to any MIDI channel pool via [Voice Config](#voice-config) SysEx.

### Polyphony and Unison

Each MIDI channel has a pool of OPL3 channels (configured via [Voice Config](#voice-config)). With `unison_count = 1`, each note-on consumes one OPL3 channel from the pool. With `unison_count = N`, each note-on consumes N channels, detuned symmetrically around the base pitch.

Unison detune spread is specified in cents. For N unison voices, voice index `i` is offset by:

```
cents_offset = (i - (N-1)/2) × detune_cents / (N-1)
```

When `pan_split` is enabled, unison voices are spread across the stereo field — leftmost voice panned hard left, rightmost hard right, with intermediate voices distributed evenly.

If the same MIDI note is triggered again while already sounding, the old voices for that note are released before allocating new ones.

### Voice Stealing

When no free slots remain in the pool, the allocator steals the oldest sounding note group (by monotonic timestamp). If the oldest group is a unison group, all voices in that group are released together. Stealing recurses until enough slots are freed for the new note's unison count.

### CC and NRPN Broadcasting

When using VoiceAllocator, standard CCs (volume, expression, pan, mod wheel, brightness, sustain) are broadcast to all OPL3 channels assigned to the MIDI channel.

NRPN parameter changes (CC 99/98/6) are forwarded to all assigned OPL3 channels via `direct_nrpn()`, so a single NRPN write on a MIDI channel applies to all voices in its pool. RPN changes (pitch bend sensitivity) are stored per-MIDI-channel and affect all subsequent pitch bend calculations.

---

## Standard MIDI Messages

### Note On / Off

In basic DirectMode (no VoiceAllocator), MIDI channel N maps 1:1 to OPL3 channel N (channels 0–15). Each channel is monophonic — a new note-on silences any previously sounding note on that channel.

With VoiceAllocator, multiple OPL3 channels can be pooled per MIDI channel for polyphony, with oldest-note stealing when the pool is exhausted. See [Voice Allocation](#voice-allocation).

OPL3 channels 16–17 are only accessible through VoiceAllocator (by assigning them to a MIDI channel pool via [Voice Config](#voice-config)) or via raw SysEx register writes.

- **Note On** (`0x9n`): Sets carrier output level from velocity × volume × expression, then triggers key-on.
- **Note Off** (`0x8n` or `0x9n` vel=0): Clears key-on bit. Respects sustain pedal (CC 64). Under VoiceAllocator, sustained notes are held until the pedal is released, at which point all sustained voices on the channel are freed.

### Control Change

| CC | Name | Effect |
|----|------|--------|
| 1 | Mod Wheel | Scales modulator output level (combined with brightness) |
| 7 | Volume | Scales carrier output level (combined with expression) |
| 10 | Pan | OPL3 stereo: ≤42 = left, ≥85 = right, 43–84 = both |
| 11 | Expression | Scales carrier output level (combined with volume) |
| 64 | Sustain | ≥64 = hold, <64 = release held notes |
| 74 | Brightness | Scales modulator output level (combined with mod wheel) |
| 98 | NRPN LSB | Sets NRPN parameter index (invalidates RPN) |
| 99 | NRPN MSB | Sets NRPN parameter group (invalidates RPN) |
| 100 | RPN LSB | Sets RPN parameter index (invalidates NRPN) |
| 101 | RPN MSB | Sets RPN parameter group (invalidates NRPN) |
| 6 | Data Entry MSB | Applies value to current NRPN or RPN address |
| 38 | Data Entry LSB | Applies fine value to current RPN address |
| 120 | All Sound Off | Key off + set release rate to 15 on both operators |
| 123 | All Notes Off | Key off on current note (natural release) |

**Carrier volume model**: Attenuation is computed as `-20 × log10((vol/127) × (expr/127)) / 0.75`, clamped to 0–63. Velocity adds `(127 - vel) >> 1` attenuation on top. Volume 0 or expression 0 produces full attenuation (silence).

**Modulator level model**: Mod wheel and brightness are combined multiplicatively: `-20 × log10((mod/127) × (bright/127)) / 0.75`, clamped to 0–63. This controls the modulation depth — higher values mean less attenuation on the modulator operator, producing a brighter/more harmonically rich sound.

### Pitch Bend

Default range: ±2 semitones from center (8192). Configurable via [RPN 0x0000](#pitch-bend-sensitivity-rpn-0x0000).

Bend offset in semitones: `(bend - 8192) × range / 8192`, where `range = semitones + cents/100`.

The target frequency is computed as `440 × 2^((note - 69 + offset) / 12)` Hz, then converted to OPL3 F-Number and Block:

```
freq = F-Number × 49716 / 2^(20 - Block)
```

The smallest Block whose F-Number fits within 0–1023 is chosen. Key-on state is preserved during the write to avoid re-triggering envelopes.

Under VoiceAllocator, pitch bend is applied to all sounding voices on the channel, with unison detune offsets stacked on top of the bend.

### Unsupported Messages

The following standard MIDI messages are received but ignored:

- Program Change (`0xCn`)
- Channel Aftertouch (`0xDn`)
- Polyphonic Aftertouch (`0xAn`)

---

## OPL3 Channel Layout

### 2-Op Channels

18 channels across two ports:

| Index | Port | OPL3 Ch | Modulator Offset | Carrier Offset |
|-------|------|---------|------------------|----------------|
| 0 | 0 | 0 | `0x00` | `0x03` |
| 1 | 0 | 1 | `0x01` | `0x04` |
| 2 | 0 | 2 | `0x02` | `0x05` |
| 3 | 0 | 3 | `0x08` | `0x0B` |
| 4 | 0 | 4 | `0x09` | `0x0C` |
| 5 | 0 | 5 | `0x0A` | `0x0D` |
| 6 | 0 | 6 | `0x10` | `0x13` |
| 7 | 0 | 7 | `0x11` | `0x14` |
| 8 | 0 | 8 | `0x12` | `0x15` |
| 9 | 1 | 0 | `0x00` | `0x03` |
| 10 | 1 | 1 | `0x01` | `0x04` |
| 11 | 1 | 2 | `0x02` | `0x05` |
| 12 | 1 | 3 | `0x08` | `0x0B` |
| 13 | 1 | 4 | `0x09` | `0x0C` |
| 14 | 1 | 5 | `0x0A` | `0x0D` |
| 15 | 1 | 6 | `0x10` | `0x13` |
| 16 | 1 | 7 | `0x11` | `0x14` |
| 17 | 1 | 8 | `0x12` | `0x15` |

Port 0 registers use addresses `0x000`–`0x0FF`. Port 1 uses `0x100`–`0x1FF`.

### 4-Op Pairs

Six 4-op pairs (3 per port). When enabled via register `0x104`, each pair acts as a single channel with 4 operators:

| Pair | Port | Primary Ch | Secondary Ch | Enable Bit |
|------|------|------------|--------------|------------|
| 0 | 0 | 0 | 3 | bit 0 |
| 1 | 0 | 1 | 4 | bit 1 |
| 2 | 0 | 2 | 5 | bit 2 |
| 3 | 1 | 0 | 3 | bit 3 |
| 4 | 1 | 1 | 4 | bit 4 |
| 5 | 1 | 2 | 5 | bit 5 |

In 4-op mode, the primary channel's `C0` register controls feedback and the first connection type. The secondary channel's `C0` bit 0 controls the second connection type. Together they select among four FM algorithms.

### Percussion Mode

When percussion mode is enabled (`0xBD` bit 5), OPL3 channels 6–8 on port 0 are repurposed as five percussion instruments:

| Drum | Freq Ch | Operator | BD Mask Bit |
|------|---------|----------|-------------|
| Bass Drum | 6 | Ch 6 carrier (`0x13`) | 4 (`0x10`) |
| Snare Drum | 7 | Ch 7 carrier (`0x14`) | 3 (`0x08`) |
| Tom-Tom | 8 | Ch 8 modulator (`0x12`) | 2 (`0x04`) |
| Cymbal | 8 | Ch 8 carrier (`0x15`) | 1 (`0x02`) |
| Hi-Hat | 7 | Ch 7 modulator (`0x11`) | 0 (`0x01`) |

Drums are triggered by setting bits in register `0xBD` (not via the normal key-on mechanism). Frequency registers on channels 6–8 control drum pitch. Velocity maps to the relevant operator's total level.
