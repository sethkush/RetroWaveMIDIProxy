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
#include <vector>
#include <array>
#include <functional>

#include <retrowave/direct_mode.h>
#include <retrowave/opl3_state.h>

namespace retrowave {

struct VoiceConfig {
	std::vector<uint8_t> opl3_channels; // assigned OPL3 ch indices (0-17)
	uint8_t unison_count = 1;           // 1=poly, N=unison, combined otherwise
	uint8_t detune_cents = 10;          // spread for unison voices (0-100)
	bool four_op = false;               // treat 4-op pairs as single voice slot
	bool pan_split = false;             // unison stereo spread (L/R split)
};

// Polyphonic voice allocator. Routes MIDI messages through DirectMode
// per-channel methods with support for multi-voice polyphony, unison
// detuning, and note stealing.
class VoiceAllocator {
public:
	// device_id: SysEx device ID for filtering (0x7F = all)
	explicit VoiceAllocator(DirectMode &dm, OPL3State &state, uint8_t device_id = 0x7F);

	// Callback type for sending MIDI output (SysEx responses).
	using MidiOutputFn = std::function<void(const std::vector<uint8_t> &)>;

	// Set callback for MIDI output (needed for voice query responses).
	void set_midi_output(MidiOutputFn fn) { midi_output_ = std::move(fn); }

	// Process a raw MIDI message. Intercepts note/CC/bend/SysEx and
	// routes through the voice allocation engine.
	void process_midi(const uint8_t *data, size_t len);

	// Set voice configuration for a MIDI channel (0-15).
	void set_voice_config(uint8_t midi_ch, const VoiceConfig &config);

	// Get current voice configuration for a MIDI channel.
	const VoiceConfig &voice_config(uint8_t midi_ch) const;

	// Initialize default 1:1 mapping: MIDI 0-15 → OPL3 0-15, unison=1.
	void init_default_mapping();

	// Release all sounding notes and reset allocation state.
	void reset();

	// Get the number of poly voices available for a MIDI channel.
	int poly_voice_count(uint8_t midi_ch) const;

	// --- Percussion routing ---

	// Enable/disable percussion mode (sets 0xBD bit 5).
	void set_percussion_mode(bool enabled);
	bool percussion_mode() const { return perc_mode_; }

	// Assign a drum to a MIDI channel (-1 = unassigned).
	void set_drum_midi_channel(DirectMode::Drum drum, int midi_ch);
	int drum_midi_channel(DirectMode::Drum drum) const;

	// SysEx commands for percussion routing
	static constexpr uint8_t kSysExPercConfig = 0x32;
	static constexpr uint8_t kSysExPercQuery = 0x33;

private:
	// Internal voice slot — tracks one OPL3 channel playing a note.
	struct Voice {
		int8_t note = -1;        // MIDI note (-1 = free)
		uint8_t velocity = 0;
		uint64_t timestamp = 0;  // monotonic, for LRU stealing
		uint16_t detuned_fnum = 0;
		uint8_t detuned_block = 0;
		bool sustained = false;  // held by sustain pedal
	};

	// Per-MIDI-channel allocation state.
	struct MidiChannelState {
		VoiceConfig config;
		std::vector<Voice> voices; // one per OPL3 channel in config.opl3_channels

		// Shadow MIDI state for broadcasting CCs
		uint8_t volume = 100;
		uint8_t expression = 127;
		uint8_t pan = 64;
		uint8_t mod_wheel = 0;
		uint8_t brightness = 64;
		bool sustain = false;
		uint16_t pitch_bend = 8192; // center
		uint8_t bend_range_semitones = 2;
		uint8_t bend_range_cents = 0;
		uint8_t nrpn_msb = 0x7F;
		uint8_t nrpn_lsb = 0x7F;
		uint8_t rpn_msb = 0x7F;
		uint8_t rpn_lsb = 0x7F;
	};

	void handle_note_on(uint8_t midi_ch, uint8_t note, uint8_t vel);
	void handle_note_off(uint8_t midi_ch, uint8_t note);
	void handle_cc(uint8_t midi_ch, uint8_t cc, uint8_t val);
	void handle_pitch_bend(uint8_t midi_ch, uint16_t bend);
	void handle_sysex(const uint8_t *data, size_t len);

	// Voice allocation helpers
	struct AllocResult {
		std::vector<int> slot_indices; // indices into voices[]
		bool stolen = false;
	};
	AllocResult allocate_slots(MidiChannelState &mcs, int count);
	void steal_oldest_group(MidiChannelState &mcs, int group_size);
	void release_voice_slots(uint8_t midi_ch, const std::vector<int> &slots);

	// Compute detuned frequency for a unison voice.
	static void compute_detuned_freq(uint8_t note, int voice_idx, int unison_count,
	                                  uint8_t detune_cents, uint16_t &f_num, uint8_t &block);

	// Recompute pitch bend for all sounding voices on a MIDI channel.
	void recompute_bend(uint8_t midi_ch);

	// SysEx voice config handlers
	void sysex_voice_config(const uint8_t *data, size_t len);
	void sysex_voice_query(const uint8_t *data, size_t len);
	void sysex_perc_config(const uint8_t *data, size_t len);
	void sysex_perc_query();

	// Check if a note-on/off should be routed to a percussion drum.
	// Returns true if handled by percussion.
	bool try_perc_note_on(uint8_t midi_ch, uint8_t note, uint8_t vel);
	bool try_perc_note_off(uint8_t midi_ch, uint8_t note);

	DirectMode &dm_;
	OPL3State &state_;
	uint8_t device_id_;
	MidiOutputFn midi_output_;
	uint64_t timestamp_counter_ = 0;
	std::array<MidiChannelState, 16> midi_channels_;

	// Percussion state
	bool perc_mode_ = false;
	std::array<int, DirectMode::kNumDrums> drum_midi_ch_ = {{-1, -1, -1, -1, -1}};
	std::array<int8_t, DirectMode::kNumDrums> drum_sounding_note_ = {{-1, -1, -1, -1, -1}};
};

} // namespace retrowave
