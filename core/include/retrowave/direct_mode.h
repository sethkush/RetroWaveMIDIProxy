/*
    This file is part of RetroWaveMIDIProxy

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU Affero General Public License as
    published by the Free Software Foundation, either version 3 of the
    License, or (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU Affero General Public License for more details.

    You should have received a copy of the GNU Affero General Public License
    along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/

#pragma once

#include <cstdint>
#include <cstddef>
#include <functional>
#include <vector>

#include <retrowave/opl3_state.h>
#include <retrowave/opl3_registers.h>

namespace retrowave {

// SysEx manufacturer ID (non-commercial, for development/personal use)
static constexpr uint8_t kSysExManufID = 0x7D;

// SysEx commands
static constexpr uint8_t kSysExRegWrite7   = 0x01;
static constexpr uint8_t kSysExBatchWrite7 = 0x02;
static constexpr uint8_t kSysExRegWrite8   = 0x03;
static constexpr uint8_t kSysExBatchWrite8 = 0x04;
static constexpr uint8_t kSysExPatchDump   = 0x10;
static constexpr uint8_t kSysExPatchLoad   = 0x11;
static constexpr uint8_t kSysExResetAll    = 0x20;
static constexpr uint8_t kSysExVoiceConfig  = 0x30;
static constexpr uint8_t kSysExVoiceQuery  = 0x31;
static constexpr uint8_t kSysExHWReset     = 0x7F;

// Direct OPL3 control mode. Translates MIDI note on/off, CCs, NRPNs,
// and SysEx messages into OPL3 register writes.
class DirectMode {
public:
	// Per-channel state (public for VoiceAllocator access)
	struct ChannelState {
		uint8_t volume = 100;       // CC7
		uint8_t expression = 127;   // CC11
		uint8_t pan = 64;           // CC10 (0=left, 64=center, 127=right)
		uint8_t mod_wheel = 0;      // CC1
		uint8_t brightness = 64;    // CC74
		bool sustain = false;       // CC64
		uint8_t nrpn_msb = 0x7F;   // CC99 (0x7F = null/inactive)
		uint8_t nrpn_lsb = 0x7F;   // CC98
		uint8_t rpn_msb = 0x7F;    // CC101 (0x7F = null/inactive)
		uint8_t rpn_lsb = 0x7F;    // CC100
		uint8_t bend_range_semitones = 2;
		uint8_t bend_range_cents = 0;
		int8_t current_note = -1;   // Currently sounding note (-1 = none)
		bool sustained_note = false; // Note held by sustain pedal
		uint8_t note_velocity = 0;
	};

	// device_id: SysEx device ID for filtering (0x7F = all)
	explicit DirectMode(OPL3State &state, uint8_t device_id = 0x7F);

	// Callback type for sending MIDI output (SysEx responses, patch dumps).
	using MidiOutputFn = std::function<void(const std::vector<uint8_t> &)>;

	// Set callback for MIDI output. Must be set before patch dump will work.
	void set_midi_output(MidiOutputFn fn) { midi_output_ = std::move(fn); }

	// Process a raw MIDI message (called from MIDI callback under lock).
	void process_midi(const uint8_t *data, size_t len);

	// Send an NRPN directly to channel ch (0-17). Bypasses MIDI status byte
	// parsing, so it can address all 18 OPL3 channels including port 1 ch 7-8.
	void direct_nrpn(uint8_t ch, uint8_t msb, uint8_t lsb, uint8_t val);

	// Initialize OPL3 to a clean state for direct mode.
	void init();

	// --- Per-OPL3-channel methods (used by VoiceAllocator) ---

	// Play a note on a specific OPL3 channel index (0-17).
	void play_note_on_channel(uint8_t opl3_ch, uint8_t note, uint8_t vel);

	// Release the sounding note on a specific OPL3 channel index (0-17).
	void release_note_on_channel(uint8_t opl3_ch);

	// Apply pitch bend to a specific OPL3 channel, given a detuned frequency.
	void bend_channel(uint8_t opl3_ch, uint16_t f_num, uint8_t block);

	// Apply a CC value to a specific OPL3 channel.
	void apply_cc_to_channel(uint8_t opl3_ch, uint8_t cc, uint8_t val);

	// --- Percussion methods (used by VoiceAllocator) ---

	enum Drum : uint8_t { kBD = 0, kSD = 1, kTT = 2, kCY = 3, kHH = 4, kNumDrums = 5 };

	// Trigger a percussion drum with pitch from a MIDI note.
	void perc_note_on(Drum drum, uint8_t note, uint8_t vel);

	// Release a percussion drum.
	void perc_note_off(Drum drum);

	// Access per-channel state (read-only, for VoiceAllocator queries).
	const ChannelState &channel_state(uint8_t ch) const { return channels_[ch]; }

	// Access OPL3 state (for reading shadow registers).
	OPL3State &state() { return state_; }

private:
	// MIDI message handlers
	void handle_note_on(uint8_t ch, uint8_t note, uint8_t vel);
	void handle_note_off(uint8_t ch, uint8_t note);
	void handle_cc(uint8_t ch, uint8_t cc, uint8_t val);
	void handle_pitch_bend(uint8_t ch, uint16_t bend);
	void handle_sysex(const uint8_t *data, size_t len);

	// CC handlers
	void cc_mod_wheel(uint8_t ch, uint8_t val);
	void cc_volume(uint8_t ch, uint8_t val);
	void cc_pan(uint8_t ch, uint8_t val);
	void cc_expression(uint8_t ch, uint8_t val);
	void cc_sustain(uint8_t ch, uint8_t val);
	void cc_brightness(uint8_t ch, uint8_t val);
	void cc_all_sound_off(uint8_t ch);
	void cc_all_notes_off(uint8_t ch);

	// NRPN/RPN state machine
	void nrpn_data_entry(uint8_t ch, uint8_t val);
	void data_entry_msb(uint8_t ch, uint8_t val);
	void data_entry_lsb(uint8_t ch, uint8_t val);
	void rpn_data_entry(uint8_t ch, uint8_t msb, uint8_t lsb, uint8_t val);
	void rpn_data_entry_lsb(uint8_t ch, uint8_t msb, uint8_t lsb, uint8_t val);
	void nrpn_apply(uint8_t ch, uint8_t msb, uint8_t lsb, uint8_t val);
	void nrpn_operator(uint8_t ch, uint8_t op_idx, uint8_t param, uint8_t val);
	void nrpn_channel(uint8_t ch, uint8_t param, uint8_t val);
	void nrpn_global(uint8_t param, uint8_t val);

	// SysEx handlers
	void sysex_reg_write_7(const uint8_t *data, size_t len);
	void sysex_reg_write_8(const uint8_t *data, size_t len);
	void sysex_batch_write_7(const uint8_t *data, size_t len);
	void sysex_batch_write_8(const uint8_t *data, size_t len);
	void sysex_patch_dump(const uint8_t *data, size_t len);
	void sysex_patch_load(const uint8_t *data, size_t len);
	void sysex_reset_all();
	void sysex_hw_reset();

	// Update output level for a channel's carrier, combining volume + expression.
	void update_carrier_level(uint8_t ch);

	// Update output level for a channel's modulator, combining mod wheel + brightness.
	void update_modulator_level(uint8_t ch);

	// Compute OPL3 attenuation (0-63) from MIDI volume (0-127) and expression (0-127).
	static uint8_t compute_attenuation(uint8_t volume, uint8_t expression);

	// Write frequency registers for a channel.
	void write_freq(uint8_t ch, uint16_t f_num, uint8_t block, bool key_on);

	OPL3State &state_;
	uint8_t device_id_;
	MidiOutputFn midi_output_;

	ChannelState channels_[18];
};

} // namespace retrowave
