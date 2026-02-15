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

#include <retrowave/voice_allocator.h>
#include <retrowave/opl3_registers.h>
#include <cmath>
#include <algorithm>
#include <limits>

namespace retrowave {

using namespace opl3;

VoiceAllocator::VoiceAllocator(DirectMode &dm, OPL3State &state, uint8_t device_id)
	: dm_(dm), state_(state), device_id_(device_id)
{
	init_default_mapping();
}

void VoiceAllocator::init_default_mapping()
{
	for (int i = 0; i < 16; ++i) {
		auto &mcs = midi_channels_[i];
		mcs.config.opl3_channels.clear();
		if (i < 18)
			mcs.config.opl3_channels.push_back(static_cast<uint8_t>(i));
		mcs.config.unison_count = 1;
		mcs.config.detune_cents = 10;
		mcs.config.four_op = false;

		mcs.voices.resize(mcs.config.opl3_channels.size());
		for (auto &v : mcs.voices)
			v = Voice{};

		mcs.volume = 100;
		mcs.expression = 127;
		mcs.pan = 64;
		mcs.mod_wheel = 0;
		mcs.brightness = 64;
		mcs.sustain = false;
		mcs.pitch_bend = 8192;
	}
}

void VoiceAllocator::reset()
{
	// Release all sounding notes
	for (int midi_ch = 0; midi_ch < 16; ++midi_ch) {
		auto &mcs = midi_channels_[midi_ch];
		for (size_t i = 0; i < mcs.voices.size(); ++i) {
			if (mcs.voices[i].note >= 0) {
				dm_.release_note_on_channel(mcs.config.opl3_channels[i]);
				mcs.voices[i] = Voice{};
			}
		}
	}
	// Release any sounding drums
	for (int d = 0; d < DirectMode::kNumDrums; ++d) {
		if (drum_sounding_note_[d] >= 0) {
			dm_.perc_note_off(static_cast<DirectMode::Drum>(d));
			drum_sounding_note_[d] = -1;
		}
	}
	timestamp_counter_ = 0;
}

void VoiceAllocator::set_percussion_mode(bool enabled)
{
	if (perc_mode_ == enabled) return;
	perc_mode_ = enabled;

	// Toggle the OPL3 percussion mode register via NRPN
	dm_.direct_nrpn(0, 5, 2, enabled ? 127 : 0);

	if (!enabled) {
		// Release all sounding drums
		for (int d = 0; d < DirectMode::kNumDrums; ++d) {
			if (drum_sounding_note_[d] >= 0) {
				dm_.perc_note_off(static_cast<DirectMode::Drum>(d));
				drum_sounding_note_[d] = -1;
			}
		}
	}
}

void VoiceAllocator::set_drum_midi_channel(DirectMode::Drum drum, int midi_ch)
{
	if (drum >= DirectMode::kNumDrums) return;
	// Release if currently sounding
	if (drum_sounding_note_[drum] >= 0) {
		dm_.perc_note_off(drum);
		drum_sounding_note_[drum] = -1;
	}
	drum_midi_ch_[drum] = midi_ch;
}

int VoiceAllocator::drum_midi_channel(DirectMode::Drum drum) const
{
	if (drum >= DirectMode::kNumDrums) return -1;
	return drum_midi_ch_[drum];
}

void VoiceAllocator::set_voice_config(uint8_t midi_ch, const VoiceConfig &config)
{
	if (midi_ch >= 16) return;
	auto &mcs = midi_channels_[midi_ch];

	// Release all sounding notes on the old config
	for (size_t i = 0; i < mcs.voices.size(); ++i) {
		if (mcs.voices[i].note >= 0)
			dm_.release_note_on_channel(mcs.config.opl3_channels[i]);
	}

	// Deconflict: remove claimed channels from any other MIDI channel
	for (uint8_t opl3_ch : config.opl3_channels) {
		for (int other = 0; other < 16; ++other) {
			if (other == midi_ch) continue;
			auto &other_mcs = midi_channels_[other];
			auto it = std::find(other_mcs.config.opl3_channels.begin(),
			                    other_mcs.config.opl3_channels.end(), opl3_ch);
			if (it != other_mcs.config.opl3_channels.end()) {
				size_t idx = static_cast<size_t>(it - other_mcs.config.opl3_channels.begin());
				// Release any note on this slot
				if (idx < other_mcs.voices.size() && other_mcs.voices[idx].note >= 0)
					dm_.release_note_on_channel(opl3_ch);
				other_mcs.config.opl3_channels.erase(it);
				if (idx < other_mcs.voices.size())
					other_mcs.voices.erase(other_mcs.voices.begin() + static_cast<long>(idx));
			}
		}
	}

	mcs.config = config;
	mcs.voices.resize(config.opl3_channels.size());
	for (auto &v : mcs.voices)
		v = Voice{};

	// Apply current MIDI state to all newly assigned OPL3 channels
	for (uint8_t opl3_ch : config.opl3_channels) {
		dm_.apply_cc_to_channel(opl3_ch, 7, mcs.volume);
		dm_.apply_cc_to_channel(opl3_ch, 11, mcs.expression);
		dm_.apply_cc_to_channel(opl3_ch, 10, mcs.pan);
		dm_.apply_cc_to_channel(opl3_ch, 1, mcs.mod_wheel);
		dm_.apply_cc_to_channel(opl3_ch, 74, mcs.brightness);
	}
}

const VoiceConfig &VoiceAllocator::voice_config(uint8_t midi_ch) const
{
	return midi_channels_[midi_ch].config;
}

int VoiceAllocator::poly_voice_count(uint8_t midi_ch) const
{
	if (midi_ch >= 16) return 0;
	const auto &mcs = midi_channels_[midi_ch];
	int unison = std::max<int>(mcs.config.unison_count, 1);

	if (mcs.config.four_op) {
		// Count 4-op pairs as 1 voice slot, standalone channels as 1 slot
		int slots = 0;
		std::vector<bool> counted(mcs.config.opl3_channels.size(), false);
		for (size_t i = 0; i < mcs.config.opl3_channels.size(); ++i) {
			if (counted[i]) continue;
			uint8_t ch = mcs.config.opl3_channels[i];
			int partner = four_op_partner(ch);
			bool found_partner = false;
			if (partner >= 0) {
				for (size_t j = i + 1; j < mcs.config.opl3_channels.size(); ++j) {
					if (!counted[j] && mcs.config.opl3_channels[j] == static_cast<uint8_t>(partner)) {
						counted[j] = true;
						found_partner = true;
						break;
					}
				}
			}
			counted[i] = true;
			slots++;
			(void)found_partner; // pair counts as 1 regardless
		}
		return slots / unison;
	}

	int pool = static_cast<int>(mcs.config.opl3_channels.size());
	return pool / unison;
}

void VoiceAllocator::process_midi(const uint8_t *data, size_t len)
{
	if (len == 0) return;

	// SysEx — check for voice config commands first
	if (data[0] == 0xF0) {
		if (len >= 5 && data[1] == kSysExManufID) {
			uint8_t cmd = data[3];
			if (cmd == kSysExVoiceConfig || cmd == kSysExVoiceQuery ||
			    cmd == kSysExPercConfig || cmd == kSysExPercQuery) {
				handle_sysex(data, len);
				return;
			}
			// Intercept ResetAll to also reset voice allocator state
			if (cmd == kSysExResetAll) {
				reset();
				dm_.process_midi(data, len);
				return;
			}
		}
		// Forward other SysEx to DirectMode
		dm_.process_midi(data, len);
		return;
	}

	uint8_t status = data[0] & 0xF0;
	uint8_t ch = data[0] & 0x0F;

	switch (status) {
	case 0x90: // Note On
		if (len >= 3) {
			if (data[2] == 0)
				handle_note_off(ch, data[1]);
			else
				handle_note_on(ch, data[1], data[2]);
		}
		break;
	case 0x80: // Note Off
		if (len >= 3)
			handle_note_off(ch, data[1]);
		break;
	case 0xB0: // Control Change
		if (len >= 3)
			handle_cc(ch, data[1], data[2]);
		break;
	case 0xE0: // Pitch Bend
		if (len >= 3)
			handle_pitch_bend(ch, static_cast<uint16_t>(data[1]) |
			                      (static_cast<uint16_t>(data[2]) << 7));
		break;
	default:
		break;
	}
}

// --- Note On ---

void VoiceAllocator::handle_note_on(uint8_t midi_ch, uint8_t note, uint8_t vel)
{
	if (midi_ch >= 16) return;

	// Check percussion routing first
	if (try_perc_note_on(midi_ch, note, vel)) return;

	auto &mcs = midi_channels_[midi_ch];
	if (mcs.config.opl3_channels.empty()) return;

	int unison = std::max<int>(mcs.config.unison_count, 1);

	// If this note is already playing, release the old voices first
	for (size_t i = 0; i < mcs.voices.size(); ++i) {
		if (mcs.voices[i].note == static_cast<int8_t>(note)) {
			dm_.release_note_on_channel(mcs.config.opl3_channels[i]);
			mcs.voices[i] = Voice{};
		}
	}

	// Allocate voice slots
	auto result = allocate_slots(mcs, unison);
	if (result.slot_indices.empty()) return;

	uint64_t ts = ++timestamp_counter_;

	for (int idx = 0; idx < static_cast<int>(result.slot_indices.size()); ++idx) {
		int slot = result.slot_indices[idx];
		uint8_t opl3_ch = mcs.config.opl3_channels[slot];

		uint16_t f_num;
		uint8_t block;
		if (unison > 1) {
			compute_detuned_freq(note, idx, unison, mcs.config.detune_cents, f_num, block);
		} else {
			const auto &nf = note_freq(note);
			f_num = nf.f_num;
			block = nf.block;
		}

		// Apply pitch bend if active
		if (mcs.pitch_bend != 8192) {
			double range = mcs.bend_range_semitones + mcs.bend_range_cents / 100.0;
			double semitones = (static_cast<int>(mcs.pitch_bend) - 8192) * range / 8192.0;
			double freq = 440.0 * std::pow(2.0, (note - 69.0 + semitones) / 12.0);

			// Apply unison detune on top of bend
			if (unison > 1) {
				double cents_offset = 0;
				if (unison > 1)
					cents_offset = (idx - (unison - 1) / 2.0) * mcs.config.detune_cents / (unison - 1);
				freq *= std::pow(2.0, cents_offset / 1200.0);
			}

			static constexpr double kOPL3FreqBase = 49716.0;
			f_num = 0;
			block = 0;
			for (int b = 0; b < 8; ++b) {
				double divisor = kOPL3FreqBase / static_cast<double>(1 << (20 - b));
				int fn = static_cast<int>(freq / divisor + 0.5);
				if (fn <= 1023) {
					if (fn < 0) fn = 0;
					f_num = static_cast<uint16_t>(fn);
					block = static_cast<uint8_t>(b);
					break;
				}
				if (b == 7) { f_num = 1023; block = 7; }
			}
		}

		auto &voice = mcs.voices[slot];
		voice.note = static_cast<int8_t>(note);
		voice.velocity = vel;
		voice.timestamp = ts;
		voice.detuned_fnum = f_num;
		voice.detuned_block = block;
		voice.sustained = false;

		dm_.play_note_on_channel(opl3_ch, note, vel);

		// If detuned or bent, overwrite the frequency
		if (unison > 1 || mcs.pitch_bend != 8192) {
			dm_.bend_channel(opl3_ch, f_num, block);
		}

		// Apply stereo pan split for unison voices
		if (unison > 1 && mcs.config.pan_split) {
			// Even: split evenly L/R. Odd: extra voice goes center.
			uint8_t pan_val;
			if (unison == 1) {
				pan_val = mcs.pan; // no split
			} else if (unison % 2 == 0) {
				// Even: map voices evenly from left to right
				// idx 0 → full left, idx N-1 → full right
				pan_val = static_cast<uint8_t>(idx * 127 / (unison - 1));
			} else {
				// Odd: center voice is the middle index
				int center_idx = unison / 2;
				if (idx == center_idx) {
					pan_val = 64; // center (both speakers)
				} else if (idx < center_idx) {
					// Left side: 0 to just before center
					pan_val = static_cast<uint8_t>(idx * 64 / center_idx);
				} else {
					// Right side: just after center to 127
					pan_val = static_cast<uint8_t>(64 + (idx - center_idx) * 63 / (unison - 1 - center_idx));
				}
			}
			dm_.apply_cc_to_channel(opl3_ch, 10, pan_val);
		}
	}
}

// --- Note Off ---

void VoiceAllocator::handle_note_off(uint8_t midi_ch, uint8_t note)
{
	if (midi_ch >= 16) return;

	// Check percussion routing first
	if (try_perc_note_off(midi_ch, note)) return;

	auto &mcs = midi_channels_[midi_ch];

	for (size_t i = 0; i < mcs.voices.size(); ++i) {
		if (mcs.voices[i].note == static_cast<int8_t>(note)) {
			if (mcs.sustain) {
				mcs.voices[i].sustained = true;
			} else {
				dm_.release_note_on_channel(mcs.config.opl3_channels[i]);
				mcs.voices[i] = Voice{};
			}
		}
	}
}

// --- CC handling ---

void VoiceAllocator::handle_cc(uint8_t midi_ch, uint8_t cc, uint8_t val)
{
	if (midi_ch >= 16) return;
	auto &mcs = midi_channels_[midi_ch];

	// Update shadow state
	switch (cc) {
	case 1:   mcs.mod_wheel = val; break;
	case 7:   mcs.volume = val; break;
	case 10:  mcs.pan = val; break;
	case 11:  mcs.expression = val; break;
	case 74:  mcs.brightness = val; break;
	case 64: {
		bool was_on = mcs.sustain;
		mcs.sustain = (val >= 64);
		if (was_on && !mcs.sustain) {
			// Release sustained notes
			for (size_t i = 0; i < mcs.voices.size(); ++i) {
				if (mcs.voices[i].sustained) {
					dm_.release_note_on_channel(mcs.config.opl3_channels[i]);
					mcs.voices[i] = Voice{};
				}
			}
		}
		break;
	}

	// NRPN addressing (invalidates RPN)
	case 99: mcs.nrpn_msb = val; mcs.rpn_msb = mcs.rpn_lsb = 0x7F; break;
	case 98: mcs.nrpn_lsb = val; mcs.rpn_msb = mcs.rpn_lsb = 0x7F; break;

	// RPN addressing (invalidates NRPN)
	case 101: mcs.rpn_msb = val; mcs.nrpn_msb = mcs.nrpn_lsb = 0x7F; break;
	case 100: mcs.rpn_lsb = val; mcs.nrpn_msb = mcs.nrpn_lsb = 0x7F; break;

	// Data Entry MSB
	case 6:
		if (mcs.nrpn_msb != 0x7F && mcs.nrpn_lsb != 0x7F) {
			// Forward NRPN to all assigned OPL3 channels
			for (uint8_t opl3_ch : mcs.config.opl3_channels)
				dm_.direct_nrpn(opl3_ch, mcs.nrpn_msb, mcs.nrpn_lsb, val);
		} else if (mcs.rpn_msb == 0 && mcs.rpn_lsb == 0) {
			// RPN 0x0000: Pitch Bend Sensitivity — semitones
			mcs.bend_range_semitones = val;
		}
		return; // Don't broadcast parameter addressing CCs
	// Data Entry LSB
	case 38:
		if (mcs.rpn_msb == 0 && mcs.rpn_lsb == 0) {
			// RPN 0x0000: Pitch Bend Sensitivity — cents
			mcs.bend_range_cents = val;
		}
		return;

	default:
		break;
	}

	// Broadcast CC to all assigned OPL3 channels
	// (skip for NRPN/RPN addressing CCs — consumed above)
	if (cc == 99 || cc == 98 || cc == 101 || cc == 100)
		return;
	for (uint8_t opl3_ch : mcs.config.opl3_channels) {
		dm_.apply_cc_to_channel(opl3_ch, cc, val);
	}
}

// --- Pitch Bend ---

void VoiceAllocator::handle_pitch_bend(uint8_t midi_ch, uint16_t bend)
{
	if (midi_ch >= 16) return;
	auto &mcs = midi_channels_[midi_ch];
	mcs.pitch_bend = bend;

	recompute_bend(midi_ch);
}

void VoiceAllocator::recompute_bend(uint8_t midi_ch)
{
	auto &mcs = midi_channels_[midi_ch];
	int unison = std::max<int>(mcs.config.unison_count, 1);

	// Recompute frequency for all sounding voices
	for (size_t i = 0; i < mcs.voices.size(); ++i) {
		auto &v = mcs.voices[i];
		if (v.note < 0) continue;

		double range = mcs.bend_range_semitones + mcs.bend_range_cents / 100.0;
		double semitones = (static_cast<int>(mcs.pitch_bend) - 8192) * range / 8192.0;
		double freq = 440.0 * std::pow(2.0, (v.note - 69.0 + semitones) / 12.0);

		// Apply unison detune
		if (unison > 1) {
			// Find which unison index this voice is within its note group
			int unison_idx = 0;
			int count = 0;
			for (size_t j = 0; j < mcs.voices.size(); ++j) {
				if (mcs.voices[j].note == v.note && mcs.voices[j].timestamp == v.timestamp) {
					if (j == i) { unison_idx = count; break; }
					count++;
				}
			}
			double cents_offset = 0;
			if (unison > 1)
				cents_offset = (unison_idx - (unison - 1) / 2.0) * mcs.config.detune_cents / (unison - 1);
			freq *= std::pow(2.0, cents_offset / 1200.0);
		}

		static constexpr double kOPL3FreqBase = 49716.0;
		uint16_t f_num = 0;
		uint8_t block = 0;
		for (int b = 0; b < 8; ++b) {
			double divisor = kOPL3FreqBase / static_cast<double>(1 << (20 - b));
			int fn = static_cast<int>(freq / divisor + 0.5);
			if (fn <= 1023) {
				if (fn < 0) fn = 0;
				f_num = static_cast<uint16_t>(fn);
				block = static_cast<uint8_t>(b);
				break;
			}
			if (b == 7) { f_num = 1023; block = 7; }
		}

		v.detuned_fnum = f_num;
		v.detuned_block = block;
		dm_.bend_channel(mcs.config.opl3_channels[i], f_num, block);
	}
}

// --- SysEx ---

void VoiceAllocator::handle_sysex(const uint8_t *data, size_t len)
{
	if (len < 5) return;
	if (data[0] != 0xF0 || data[len - 1] != 0xF7) return;
	if (data[1] != kSysExManufID) return;
	// Check device ID
	if (data[2] != device_id_ && data[2] != 0x7F && device_id_ != 0x7F)
		return;

	uint8_t cmd = data[3];
	const uint8_t *payload = data + 4;
	size_t payload_len = len - 5;

	switch (cmd) {
	case kSysExVoiceConfig:
		sysex_voice_config(payload, payload_len);
		break;
	case kSysExVoiceQuery:
		sysex_voice_query(payload, payload_len);
		break;
	case kSysExPercConfig:
		sysex_perc_config(payload, payload_len);
		break;
	case kSysExPercQuery:
		sysex_perc_query();
		break;
	default:
		break;
	}
}

void VoiceAllocator::sysex_voice_config(const uint8_t *data, size_t len)
{
	// Format: [midi-ch] [count] [opl3-ch-0..N] [unison] [detune] [flags]
	// flags bit 0: four_op, bit 1: pan_split
	if (len < 4) return;
	uint8_t midi_ch = data[0];
	if (midi_ch >= 16) return;

	uint8_t count = data[1];
	if (len < static_cast<size_t>(2 + count + 2)) return;

	VoiceConfig config;
	for (uint8_t i = 0; i < count; ++i) {
		uint8_t ch = data[2 + i];
		if (ch < 18)
			config.opl3_channels.push_back(ch);
	}
	config.unison_count = data[2 + count];
	config.detune_cents = data[3 + count];
	if (len >= static_cast<size_t>(4 + count)) {
		uint8_t flags = data[4 + count];
		config.four_op = (flags & 0x01) != 0;
		config.pan_split = (flags & 0x02) != 0;
	}

	set_voice_config(midi_ch, config);
}

void VoiceAllocator::sysex_voice_query(const uint8_t *data, size_t len)
{
	if (len < 1 || !midi_output_) return;
	uint8_t midi_ch = data[0];
	if (midi_ch >= 16) return;

	const auto &config = midi_channels_[midi_ch].config;

	// Build response using same format as voice config
	std::vector<uint8_t> msg;
	msg.push_back(0xF0);
	msg.push_back(kSysExManufID);
	msg.push_back(device_id_);
	msg.push_back(kSysExVoiceConfig);
	msg.push_back(midi_ch);
	msg.push_back(static_cast<uint8_t>(config.opl3_channels.size()));
	for (uint8_t ch : config.opl3_channels)
		msg.push_back(ch);
	msg.push_back(config.unison_count);
	msg.push_back(config.detune_cents);
	uint8_t flags = (config.four_op ? 0x01 : 0x00) | (config.pan_split ? 0x02 : 0x00);
	msg.push_back(flags);
	msg.push_back(0xF7);

	midi_output_(msg);
}

// --- Voice allocation helpers ---

VoiceAllocator::AllocResult VoiceAllocator::allocate_slots(MidiChannelState &mcs, int count)
{
	AllocResult result;
	result.stolen = false;

	// Find free slots
	for (size_t i = 0; i < mcs.voices.size() && static_cast<int>(result.slot_indices.size()) < count; ++i) {
		if (mcs.voices[i].note < 0)
			result.slot_indices.push_back(static_cast<int>(i));
	}

	// If we have enough free slots, we're done
	if (static_cast<int>(result.slot_indices.size()) >= count)
		return result;

	// Not enough free slots — steal oldest note group
	result.slot_indices.clear();
	steal_oldest_group(mcs, count);

	// Try again after stealing
	for (size_t i = 0; i < mcs.voices.size() && static_cast<int>(result.slot_indices.size()) < count; ++i) {
		if (mcs.voices[i].note < 0)
			result.slot_indices.push_back(static_cast<int>(i));
	}

	if (!result.slot_indices.empty())
		result.stolen = true;

	return result;
}

void VoiceAllocator::steal_oldest_group(MidiChannelState &mcs, int group_size)
{
	// Find the voice group with the lowest (oldest) timestamp
	uint64_t oldest_ts = std::numeric_limits<uint64_t>::max();
	int8_t oldest_note = -1;
	uint64_t oldest_note_ts = 0;

	for (auto &v : mcs.voices) {
		if (v.note >= 0 && v.timestamp < oldest_ts) {
			oldest_ts = v.timestamp;
			oldest_note = v.note;
			oldest_note_ts = v.timestamp;
		}
	}

	if (oldest_note < 0) return;

	// Release all voices in this note group (same note + timestamp = unison group)
	int freed = 0;
	for (size_t i = 0; i < mcs.voices.size(); ++i) {
		if (mcs.voices[i].note == oldest_note && mcs.voices[i].timestamp == oldest_note_ts) {
			dm_.release_note_on_channel(mcs.config.opl3_channels[i]);
			mcs.voices[i] = Voice{};
			freed++;
		}
	}

	// If we didn't free enough, steal another group
	if (freed < group_size) {
		steal_oldest_group(mcs, group_size - freed);
	}
}

// --- Unison detuning ---

void VoiceAllocator::compute_detuned_freq(uint8_t note, int voice_idx, int unison_count,
                                            uint8_t detune_cents, uint16_t &f_num, uint8_t &block)
{
	double cents_offset = 0;
	if (unison_count > 1) {
		cents_offset = (voice_idx - (unison_count - 1) / 2.0) * detune_cents / (unison_count - 1);
	}

	double freq = 440.0 * std::pow(2.0, (note - 69.0 + cents_offset / 100.0) / 12.0);

	static constexpr double kOPL3FreqBase = 49716.0;
	f_num = 0;
	block = 0;

	for (int b = 0; b < 8; ++b) {
		double divisor = kOPL3FreqBase / static_cast<double>(1 << (20 - b));
		int fn = static_cast<int>(freq / divisor + 0.5);
		if (fn <= 1023) {
			if (fn < 0) fn = 0;
			f_num = static_cast<uint16_t>(fn);
			block = static_cast<uint8_t>(b);
			return;
		}
	}

	f_num = 1023;
	block = 7;
}

// --- Percussion routing ---

bool VoiceAllocator::try_perc_note_on(uint8_t midi_ch, uint8_t note, uint8_t vel)
{
	if (!perc_mode_) return false;

	bool handled = false;
	for (int d = 0; d < DirectMode::kNumDrums; ++d) {
		if (drum_midi_ch_[d] == static_cast<int>(midi_ch)) {
			auto drum = static_cast<DirectMode::Drum>(d);
			// Release if already sounding
			if (drum_sounding_note_[d] >= 0)
				dm_.perc_note_off(drum);
			dm_.perc_note_on(drum, note, vel);
			drum_sounding_note_[d] = static_cast<int8_t>(note);
			handled = true;
		}
	}
	return handled;
}

bool VoiceAllocator::try_perc_note_off(uint8_t midi_ch, uint8_t note)
{
	if (!perc_mode_) return false;

	bool handled = false;
	for (int d = 0; d < DirectMode::kNumDrums; ++d) {
		if (drum_midi_ch_[d] == static_cast<int>(midi_ch) &&
		    drum_sounding_note_[d] == static_cast<int8_t>(note)) {
			dm_.perc_note_off(static_cast<DirectMode::Drum>(d));
			drum_sounding_note_[d] = -1;
			handled = true;
		}
	}
	return handled;
}

void VoiceAllocator::sysex_perc_config(const uint8_t *data, size_t len)
{
	// Format: [perc-mode] [bd-midi-ch] [sd-midi-ch] [tt-midi-ch] [cy-midi-ch] [hh-midi-ch]
	// midi-ch values: 0-15 = assigned, 0x7F = unassigned
	if (len < 6) return;

	set_percussion_mode(data[0] >= 64);

	for (int d = 0; d < DirectMode::kNumDrums; ++d) {
		int ch = (data[1 + d] < 16) ? data[1 + d] : -1;
		set_drum_midi_channel(static_cast<DirectMode::Drum>(d), ch);
	}
}

void VoiceAllocator::sysex_perc_query()
{
	if (!midi_output_) return;

	// Response uses PercConfig format so it's re-sendable
	std::vector<uint8_t> msg;
	msg.push_back(0xF0);
	msg.push_back(kSysExManufID);
	msg.push_back(device_id_);
	msg.push_back(kSysExPercConfig);
	msg.push_back(perc_mode_ ? 0x7F : 0x00);
	for (int d = 0; d < DirectMode::kNumDrums; ++d) {
		int ch = drum_midi_ch_[d];
		msg.push_back(static_cast<uint8_t>(ch >= 0 ? ch : 0x7F));
	}
	msg.push_back(0xF7);

	midi_output_(msg);
}

} // namespace retrowave
