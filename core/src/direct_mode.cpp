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

#include <retrowave/direct_mode.h>
#include <cmath>
#include <algorithm>

namespace retrowave {

using namespace opl3;

DirectMode::DirectMode(OPL3State &state, uint8_t device_id)
	: state_(state), device_id_(device_id)
{
}

void DirectMode::init()
{
	state_.reset();
	for (auto &ch : channels_)
		ch = ChannelState{};

	// Set default instrument: a basic FM piano-like patch on all channels.
	// Modulator: AM=0, Vib=0, EGT=1, KSR=0, Mult=1 → 0x21
	// Carrier:   AM=0, Vib=0, EGT=1, KSR=0, Mult=1 → 0x21
	// Modulator KSL=0, TL=32 → 0x20
	// Carrier   KSL=0, TL=0  → 0x00 (will be set by volume)
	// Modulator AR=15, DR=4 → 0xF4
	// Carrier   AR=15, DR=4 → 0xF4
	// Modulator SL=2, RR=4 → 0x24
	// Carrier   SL=2, RR=6 → 0x26
	// Waveform: 0 (sine) for both
	// Feedback=4, Connection=0 (FM), both speakers → 0x38 | 0x30 = 0x38
	for (int midi_ch = 0; midi_ch < 18; ++midi_ch) {
		const auto &map = kChannelToOPL3[midi_ch];
		uint16_t base = map.port_base;
		uint8_t ch = map.opl_ch;
		uint8_t mod_off = kOperatorOffset[ch][0];
		uint8_t car_off = kOperatorOffset[ch][1];

		state_.write(base | (kRegAMVibEGKSMult + mod_off), 0x21);
		state_.write(base | (kRegAMVibEGKSMult + car_off), 0x21);
		state_.write(base | (kRegKSLTL + mod_off), 0x20);
		state_.write(base | (kRegKSLTL + car_off), 0x00);
		state_.write(base | (kRegAR_DR + mod_off), 0xF4);
		state_.write(base | (kRegAR_DR + car_off), 0xF4);
		state_.write(base | (kRegSL_RR + mod_off), 0x24);
		state_.write(base | (kRegSL_RR + car_off), 0x26);
		state_.write(base | (kRegWaveform + mod_off), 0x00);
		state_.write(base | (kRegWaveform + car_off), 0x00);
		state_.write(base | (kRegFeedbackConn + ch), 0x38); // FB=4, Conn=0, L+R
	}
}

void DirectMode::process_midi(const uint8_t *data, size_t len)
{
	if (len == 0)
		return;

	// SysEx
	if (data[0] == 0xF0) {
		handle_sysex(data, len);
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

// --- Note handling ---

void DirectMode::handle_note_on(uint8_t ch, uint8_t note, uint8_t vel)
{
	if (ch >= 16) return;
	auto &cs = channels_[ch];

	// If a note is already sounding, turn it off first
	if (cs.current_note >= 0) {
		write_freq(ch, 0, 0, false);
	}

	cs.current_note = static_cast<int8_t>(note);
	cs.note_velocity = vel;
	cs.sustained_note = false;

	const auto &nf = note_freq(note);
	const auto &map = kChannelToOPL3[ch];

	// Set carrier output level based on velocity + volume + expression
	uint8_t car_off = kOperatorOffset[map.opl_ch][1];
	uint8_t base_atten = compute_attenuation(cs.volume, cs.expression);
	// Scale by velocity: vel 127 = no additional attenuation
	uint8_t vel_atten = static_cast<uint8_t>((127 - vel) >> 1); // 0-63
	uint8_t total_atten = std::min<int>(base_atten + vel_atten, 63);

	// Preserve KSL bits (7-6), set total level (5-0)
	state_.modify_bits(map.port_base | (kRegKSLTL + car_off), 0x3F, total_atten);

	write_freq(ch, nf.f_num, nf.block, true);
}

void DirectMode::handle_note_off(uint8_t ch, uint8_t note)
{
	if (ch >= 16) return;
	auto &cs = channels_[ch];

	if (cs.current_note != static_cast<int8_t>(note))
		return;

	if (cs.sustain) {
		cs.sustained_note = true;
		return;
	}

	const auto &map = kChannelToOPL3[ch];
	// Key off: clear bit 5 of B0+ch
	state_.modify_bits(map.port_base | (kRegKeyOnBlkFNum + map.opl_ch), 0x20, 0x00);
	cs.current_note = -1;
	cs.sustained_note = false;
}

void DirectMode::write_freq(uint8_t ch, uint16_t f_num, uint8_t block, bool key_on)
{
	const auto &map = kChannelToOPL3[ch];
	uint16_t base = map.port_base;
	uint8_t opl_ch = map.opl_ch;

	// A0+ch: F-Num low 8 bits
	state_.write(base | (kRegFNumLow + opl_ch),
	             static_cast<uint8_t>(f_num & 0xFF));
	// B0+ch: Key-On (bit 5), Block (bits 4-2), F-Num high 2 bits (bits 1-0)
	uint8_t b0 = static_cast<uint8_t>(((f_num >> 8) & 0x03) |
	                                   ((block & 0x07) << 2) |
	                                   (key_on ? 0x20 : 0x00));
	state_.write(base | (kRegKeyOnBlkFNum + opl_ch), b0);
}

// --- CC handling ---

void DirectMode::handle_cc(uint8_t ch, uint8_t cc, uint8_t val)
{
	if (ch >= 16) return;
	auto &cs = channels_[ch];

	switch (cc) {
	case 1:   cc_mod_wheel(ch, val); break;
	case 7:   cc_volume(ch, val); break;
	case 10:  cc_pan(ch, val); break;
	case 11:  cc_expression(ch, val); break;
	case 64:  cc_sustain(ch, val); break;
	case 74:  cc_brightness(ch, val); break;
	case 120: cc_all_sound_off(ch); break;
	case 123: cc_all_notes_off(ch); break;

	// NRPN state machine
	case 99: cs.nrpn_msb = val; break;     // NRPN MSB
	case 98: cs.nrpn_lsb = val; break;     // NRPN LSB
	case 6:  nrpn_data_entry(ch, val); break; // Data Entry MSB

	default:
		break;
	}
}

void DirectMode::cc_mod_wheel(uint8_t ch, uint8_t val)
{
	channels_[ch].mod_wheel = val;
	update_modulator_level(ch);
}

void DirectMode::cc_volume(uint8_t ch, uint8_t val)
{
	channels_[ch].volume = val;
	update_carrier_level(ch);
}

void DirectMode::cc_pan(uint8_t ch, uint8_t val)
{
	channels_[ch].pan = val;
	const auto &map = kChannelToOPL3[ch];

	// OPL3 pan: bits 4 (left) and 5 (right) of register C0+ch
	uint8_t pan_bits = 0;
	if (val <= 42)
		pan_bits = 0x10; // Left only
	else if (val >= 85)
		pan_bits = 0x20; // Right only
	else
		pan_bits = 0x30; // Both (center)

	state_.modify_bits(map.port_base | (kRegFeedbackConn + map.opl_ch), 0x30, pan_bits);
}

void DirectMode::cc_expression(uint8_t ch, uint8_t val)
{
	channels_[ch].expression = val;
	update_carrier_level(ch);
}

void DirectMode::cc_sustain(uint8_t ch, uint8_t val)
{
	auto &cs = channels_[ch];
	bool was_on = cs.sustain;
	cs.sustain = (val >= 64);

	// If sustain released and a note was being held, release it
	if (was_on && !cs.sustain && cs.sustained_note && cs.current_note >= 0) {
		const auto &map = kChannelToOPL3[ch];
		state_.modify_bits(map.port_base | (kRegKeyOnBlkFNum + map.opl_ch), 0x20, 0x00);
		cs.current_note = -1;
		cs.sustained_note = false;
	}
}

void DirectMode::cc_brightness(uint8_t ch, uint8_t val)
{
	channels_[ch].brightness = val;
	update_modulator_level(ch);
}

void DirectMode::update_modulator_level(uint8_t ch)
{
	if (ch >= 18) return;
	auto &cs = channels_[ch];
	const auto &map = kChannelToOPL3[ch];
	uint8_t mod_off = kOperatorOffset[map.opl_ch][0];

	// Combine mod wheel and brightness multiplicatively.
	// Higher mod_wheel = more modulation (less attenuation).
	// Higher brightness = brighter sound (less attenuation).
	double mod_factor = cs.mod_wheel / 127.0;
	double bright_factor = cs.brightness / 127.0;
	double combined = mod_factor * bright_factor;

	uint8_t atten;
	if (combined < 0.001)
		atten = 63;
	else {
		int a = static_cast<int>(-20.0 * std::log10(combined) / 0.75 + 0.5);
		if (a < 0) a = 0;
		if (a > 63) a = 63;
		atten = static_cast<uint8_t>(a);
	}

	state_.modify_bits(map.port_base | (kRegKSLTL + mod_off), 0x3F, atten);
}

void DirectMode::cc_all_sound_off(uint8_t ch)
{
	const auto &map = kChannelToOPL3[ch];
	// Immediate silence: key off and set fastest release
	state_.modify_bits(map.port_base | (kRegKeyOnBlkFNum + map.opl_ch), 0x20, 0x00);
	uint8_t car_off = kOperatorOffset[map.opl_ch][1];
	uint8_t mod_off = kOperatorOffset[map.opl_ch][0];
	state_.modify_bits(map.port_base | (kRegSL_RR + car_off), 0x0F, 0x0F);
	state_.modify_bits(map.port_base | (kRegSL_RR + mod_off), 0x0F, 0x0F);
	channels_[ch].current_note = -1;
	channels_[ch].sustained_note = false;
}

void DirectMode::cc_all_notes_off(uint8_t ch)
{
	auto &cs = channels_[ch];
	if (cs.current_note >= 0) {
		const auto &map = kChannelToOPL3[ch];
		state_.modify_bits(map.port_base | (kRegKeyOnBlkFNum + map.opl_ch), 0x20, 0x00);
		cs.current_note = -1;
		cs.sustained_note = false;
	}
}

void DirectMode::update_carrier_level(uint8_t ch)
{
	if (ch >= 18) return;
	auto &cs = channels_[ch];
	const auto &map = kChannelToOPL3[ch];
	uint8_t car_off = kOperatorOffset[map.opl_ch][1];

	uint8_t base_atten = compute_attenuation(cs.volume, cs.expression);
	// Factor in velocity if a note is playing
	uint8_t vel_atten = 0;
	if (cs.current_note >= 0)
		vel_atten = static_cast<uint8_t>((127 - cs.note_velocity) >> 1);
	uint8_t total = std::min<int>(base_atten + vel_atten, 63);

	state_.modify_bits(map.port_base | (kRegKSLTL + car_off), 0x3F, total);
}

uint8_t DirectMode::compute_attenuation(uint8_t volume, uint8_t expression)
{
	if (volume == 0 || expression == 0)
		return 63; // Full attenuation (silence)

	// Multiplicative volume model: combined = (vol/127) * (expr/127)
	// OPL3 attenuation = -20*log10(combined) / 0.75, clamped 0-63
	double combined = (volume / 127.0) * (expression / 127.0);
	double atten_db = -20.0 * std::log10(combined);
	int atten = static_cast<int>(atten_db / 0.75 + 0.5);
	if (atten < 0) atten = 0;
	if (atten > 63) atten = 63;
	return static_cast<uint8_t>(atten);
}

// --- Pitch Bend ---

void DirectMode::handle_pitch_bend(uint8_t ch, uint16_t bend)
{
	if (ch >= 16) return;
	auto &cs = channels_[ch];
	if (cs.current_note < 0)
		return;

	// Bend range: +/- 2 semitones. Center = 8192.
	// Compute pitch offset in semitones, then re-lookup frequency.
	double semitones = (static_cast<int>(bend) - 8192) * 2.0 / 8192.0;
	double freq = 440.0 * std::pow(2.0, (cs.current_note - 69.0 + semitones) / 12.0);

	// Convert to F-Num + Block
	static constexpr double kOPL3FreqBase = 49716.0;
	uint16_t f_num = 0;
	uint8_t block = 0;

	for (int b = 0; b < 8; ++b) {
		double divisor = kOPL3FreqBase / static_cast<double>(1 << (20 - b));
		double f = freq / divisor;
		int fn = static_cast<int>(f + 0.5);
		if (fn <= 1023) {
			if (fn < 0) fn = 0;
			f_num = static_cast<uint16_t>(fn);
			block = static_cast<uint8_t>(b);
			break;
		}
		if (b == 7) {
			f_num = 1023;
			block = 7;
		}
	}

	// Preserve current key-on state rather than forcing key-on=true,
	// to avoid re-triggering the OPL3 envelope on sustained notes.
	const auto &map = kChannelToOPL3[ch];
	uint8_t cur_b0 = state_.read(map.port_base | (kRegKeyOnBlkFNum + map.opl_ch));
	bool current_key_on = (cur_b0 & 0x20) != 0;
	write_freq(ch, f_num, block, current_key_on);
}

// --- NRPN ---

void DirectMode::nrpn_data_entry(uint8_t ch, uint8_t val)
{
	auto &cs = channels_[ch];
	if (cs.nrpn_msb == 0x7F || cs.nrpn_lsb == 0x7F)
		return; // NRPN not set

	nrpn_apply(ch, cs.nrpn_msb, cs.nrpn_lsb, val);
}

void DirectMode::direct_nrpn(uint8_t ch, uint8_t msb, uint8_t lsb, uint8_t val)
{
	if (ch >= 18) return;
	nrpn_apply(ch, msb, lsb, val);
}

void DirectMode::nrpn_apply(uint8_t ch, uint8_t msb, uint8_t lsb, uint8_t val)
{
	if (msb <= 3) {
		nrpn_operator(ch, msb, lsb, val);
	} else if (msb == 4) {
		nrpn_channel(ch, lsb, val);
	} else if (msb == 5) {
		nrpn_global(lsb, val);
	}
}

void DirectMode::nrpn_operator(uint8_t ch, uint8_t op_idx, uint8_t param, uint8_t val)
{
	if (ch >= 18) return;
	const auto &map = kChannelToOPL3[ch];
	uint16_t base = map.port_base;
	uint8_t opl_ch = map.opl_ch;

	// op_idx 0-1: operators on this channel
	// op_idx 2-3: operators on paired channel (4-op mode only)
	uint8_t op_off;
	if (op_idx <= 1) {
		op_off = kOperatorOffset[opl_ch][op_idx];
	} else {
		// 4-op: find the paired channel. Channels 0-2 pair with 3-5.
		uint8_t pair_ch = 0xFF;
		for (auto &p : kFourOpPairs) {
			if (p[0] == opl_ch) { pair_ch = p[1]; break; }
		}
		if (pair_ch == 0xFF) return; // Not a 4-op capable channel
		op_off = kOperatorOffset[pair_ch][op_idx - 2];
	}

	val &= 0x7F; // Clamp to MIDI range

	switch (param) {
	case 0: // Attack Rate (bits 7-4 of 0x60+off)
		state_.modify_bits(base | (kRegAR_DR + op_off), 0xF0,
		                   static_cast<uint8_t>((val >> 3) << 4));
		break;
	case 1: // Decay Rate (bits 3-0 of 0x60+off)
		state_.modify_bits(base | (kRegAR_DR + op_off), 0x0F,
		                   static_cast<uint8_t>(val >> 3));
		break;
	case 2: // Sustain Level (bits 7-4 of 0x80+off)
		state_.modify_bits(base | (kRegSL_RR + op_off), 0xF0,
		                   static_cast<uint8_t>((val >> 3) << 4));
		break;
	case 3: // Release Rate (bits 3-0 of 0x80+off)
		state_.modify_bits(base | (kRegSL_RR + op_off), 0x0F,
		                   static_cast<uint8_t>(val >> 3));
		break;
	case 4: // Waveform (bits 2-0 of 0xE0+off)
		state_.modify_bits(base | (kRegWaveform + op_off), 0x07,
		                   static_cast<uint8_t>(val >> 4));
		break;
	case 5: // Frequency Multiplier (bits 3-0 of 0x20+off)
		state_.modify_bits(base | (kRegAMVibEGKSMult + op_off), 0x0F,
		                   static_cast<uint8_t>(val >> 3));
		break;
	case 6: // Output Level (bits 5-0 of 0x40+off)
		state_.modify_bits(base | (kRegKSLTL + op_off), 0x3F,
		                   static_cast<uint8_t>(val >> 1));
		break;
	case 7: // Key Scale Level (bits 7-6 of 0x40+off)
		state_.modify_bits(base | (kRegKSLTL + op_off), 0xC0,
		                   static_cast<uint8_t>((val >> 5) << 6));
		break;
	case 8: // Tremolo AM (bit 7 of 0x20+off)
		state_.modify_bits(base | (kRegAMVibEGKSMult + op_off), 0x80,
		                   static_cast<uint8_t>(val >= 64 ? 0x80 : 0x00));
		break;
	case 9: // Vibrato (bit 6 of 0x20+off)
		state_.modify_bits(base | (kRegAMVibEGKSMult + op_off), 0x40,
		                   static_cast<uint8_t>(val >= 64 ? 0x40 : 0x00));
		break;
	case 10: // Sustain Mode EGT (bit 5 of 0x20+off)
		state_.modify_bits(base | (kRegAMVibEGKSMult + op_off), 0x20,
		                   static_cast<uint8_t>(val >= 64 ? 0x20 : 0x00));
		break;
	case 11: // KSR (bit 4 of 0x20+off)
		state_.modify_bits(base | (kRegAMVibEGKSMult + op_off), 0x10,
		                   static_cast<uint8_t>(val >= 64 ? 0x10 : 0x00));
		break;
	default:
		break;
	}
}

void DirectMode::nrpn_channel(uint8_t ch, uint8_t param, uint8_t val)
{
	if (ch >= 18) return;
	const auto &map = kChannelToOPL3[ch];
	uint16_t base = map.port_base;
	uint8_t opl_ch = map.opl_ch;

	switch (param) {
	case 0: // Feedback (bits 3-1 of 0xC0+ch)
		state_.modify_bits(base | (kRegFeedbackConn + opl_ch), 0x0E,
		                   static_cast<uint8_t>((val >> 4) << 1));
		break;
	case 1: // Connection FM/AM (bit 0 of 0xC0+ch)
		state_.modify_bits(base | (kRegFeedbackConn + opl_ch), 0x01,
		                   static_cast<uint8_t>(val >= 64 ? 0x01 : 0x00));
		break;
	case 2: // Pan Left (bit 4 of 0xC0+ch)
		state_.modify_bits(base | (kRegFeedbackConn + opl_ch), 0x10,
		                   static_cast<uint8_t>(val >= 64 ? 0x10 : 0x00));
		break;
	case 3: // Pan Right (bit 5 of 0xC0+ch)
		state_.modify_bits(base | (kRegFeedbackConn + opl_ch), 0x20,
		                   static_cast<uint8_t>(val >= 64 ? 0x20 : 0x00));
		break;
	case 4: { // 4-op Enable (bit in 0x104)
		// Find which 4-op pair this channel belongs to
		int pair_idx = -1;
		int port_offset = (base == 0x100) ? 3 : 0;
		for (int i = 0; i < 3; ++i) {
			if (kFourOpPairs[i][0] == opl_ch) {
				pair_idx = i + port_offset;
				break;
			}
		}
		if (pair_idx >= 0) {
			state_.modify_bits(kReg4OpEnable, kFourOpEnableBit[pair_idx],
			                   val >= 64 ? kFourOpEnableBit[pair_idx] : 0);
		}
		break;
	}
	case 5: { // 4-op Secondary Connection (conn bit on paired channel's C0)
		// In 4-op mode, the second connection bit controls the routing
		// between the second pair of operators. It lives on the paired
		// channel's C0 register.
		uint8_t pair_opl_ch = 0xFF;
		for (auto &p : kFourOpPairs) {
			if (p[0] == opl_ch) { pair_opl_ch = p[1]; break; }
		}
		if (pair_opl_ch != 0xFF) {
			state_.modify_bits(base | (kRegFeedbackConn + pair_opl_ch), 0x01,
			                   static_cast<uint8_t>(val >= 64 ? 0x01 : 0x00));
		}
		break;
	}
	default:
		break;
	}
}

void DirectMode::nrpn_global(uint8_t param, uint8_t val)
{
	switch (param) {
	case 0: // Tremolo Depth (bit 7 of 0xBD)
		state_.modify_bits(kRegBD, 0x80,
		                   static_cast<uint8_t>(val >= 64 ? 0x80 : 0x00));
		break;
	case 1: // Vibrato Depth (bit 6 of 0xBD)
		state_.modify_bits(kRegBD, 0x40,
		                   static_cast<uint8_t>(val >= 64 ? 0x40 : 0x00));
		break;
	case 2: // Percussion Mode (bit 5 of 0xBD)
		state_.modify_bits(kRegBD, 0x20,
		                   static_cast<uint8_t>(val >= 64 ? 0x20 : 0x00));
		break;
	default:
		break;
	}
}

// --- SysEx ---

void DirectMode::handle_sysex(const uint8_t *data, size_t len)
{
	// Minimum: F0 7D [device-id] [command] F7 = 5 bytes
	if (len < 5)
		return;
	if (data[0] != 0xF0 || data[len - 1] != 0xF7)
		return;
	if (data[1] != kSysExManufID)
		return;
	// Check device ID
	if (data[2] != device_id_ && data[2] != 0x7F && device_id_ != 0x7F)
		return;

	uint8_t cmd = data[3];
	const uint8_t *payload = data + 4;
	size_t payload_len = len - 5; // Exclude F0, manuf, devid, cmd, F7

	switch (cmd) {
	case kSysExRegWrite7:
		sysex_reg_write_7(payload, payload_len);
		break;
	case kSysExBatchWrite7:
		sysex_batch_write_7(payload, payload_len);
		break;
	case kSysExRegWrite8:
		sysex_reg_write_8(payload, payload_len);
		break;
	case kSysExBatchWrite8:
		sysex_batch_write_8(payload, payload_len);
		break;
	case kSysExPatchDump:
		sysex_patch_dump(payload, payload_len);
		break;
	case kSysExPatchLoad:
		sysex_patch_load(payload, payload_len);
		break;
	case kSysExResetAll:
		sysex_reset_all();
		break;
	case kSysExHWReset:
		sysex_hw_reset();
		break;
	default:
		break;
	}
}

void DirectMode::sysex_reg_write_7(const uint8_t *data, size_t len)
{
	// reg-hi, reg-lo, value (all 7-bit safe, value 0-127)
	if (len < 3) return;
	uint16_t addr = (static_cast<uint16_t>(data[0]) << 7) | data[1];
	if (addr > 0x1FF) return; // OPL3 address space is 0x000-0x1FF
	state_.write(addr, data[2]);
}

void DirectMode::sysex_reg_write_8(const uint8_t *data, size_t len)
{
	// reg-hi, reg-lo, val-hi-nib, val-lo-nib
	if (len < 4) return;
	uint16_t addr = (static_cast<uint16_t>(data[0]) << 7) | data[1];
	if (addr > 0x1FF) return; // OPL3 address space is 0x000-0x1FF
	uint8_t val = static_cast<uint8_t>((data[2] << 4) | (data[3] & 0x0F));
	state_.write(addr, val);
}

void DirectMode::sysex_batch_write_7(const uint8_t *data, size_t len)
{
	// count, [reg-hi, reg-lo, value]...
	if (len < 1) return;
	uint8_t count = data[0];
	const uint8_t *p = data + 1;
	size_t remaining = len - 1;

	for (uint8_t i = 0; i < count && remaining >= 3; ++i) {
		uint16_t addr = (static_cast<uint16_t>(p[0]) << 7) | p[1];
		if (addr <= 0x1FF)
			state_.write(addr, p[2]);
		p += 3;
		remaining -= 3;
	}
}

void DirectMode::sysex_batch_write_8(const uint8_t *data, size_t len)
{
	// count, [reg-hi, reg-lo, val-hi, val-lo]...
	if (len < 1) return;
	uint8_t count = data[0];
	const uint8_t *p = data + 1;
	size_t remaining = len - 1;

	for (uint8_t i = 0; i < count && remaining >= 4; ++i) {
		uint16_t addr = (static_cast<uint16_t>(p[0]) << 7) | p[1];
		uint8_t val = static_cast<uint8_t>((p[2] << 4) | (p[3] & 0x0F));
		if (addr <= 0x1FF)
			state_.write(addr, val);
		p += 4;
		remaining -= 4;
	}
}

void DirectMode::sysex_patch_dump(const uint8_t *data, size_t len)
{
	// Request payload: midi-ch
	// Response: F0 7D [device-id] 0x10 [midi-ch] [nibble-encoded operator data] F7
	// Mirrors the patch load format so the response can be sent back as a patch load.
	if (len < 1 || !midi_output_) return;
	uint8_t midi_ch = data[0];
	if (midi_ch >= 18) return;

	const auto &map = kChannelToOPL3[midi_ch];
	uint16_t base = map.port_base;
	uint8_t opl_ch = map.opl_ch;

	// Check if channel is in 4-op mode
	int partner = four_op_partner(midi_ch);
	bool is_four_op = false;
	if (partner >= 0) {
		// Check if 4-op is enabled for this pair
		int primary = std::min(midi_ch, static_cast<uint8_t>(partner));
		int port_offset = (base == 0x100) ? 3 : 0;
		int pair_idx = -1;
		for (int i = 0; i < 3; ++i) {
			if (kFourOpPairs[i][0] == kChannelToOPL3[primary].opl_ch) {
				pair_idx = i + port_offset;
				break;
			}
		}
		if (pair_idx >= 0)
			is_four_op = (state_.read(kReg4OpEnable) & kFourOpEnableBit[pair_idx]) != 0;
	}

	int num_ops = is_four_op ? 4 : 2;

	std::vector<uint8_t> msg;
	msg.reserve(52 + (is_four_op ? 48 : 0));

	// Header — response uses PatchLoad command so it's directly re-sendable
	msg.push_back(0xF0);
	msg.push_back(kSysExManufID);
	msg.push_back(device_id_);
	msg.push_back(kSysExPatchLoad);
	msg.push_back(midi_ch);

	// Operators: 2 on primary channel, optionally 2 more on paired channel
	for (int op = 0; op < num_ops; ++op) {
		uint8_t op_off;
		uint16_t reg_base;
		if (op < 2) {
			op_off = kOperatorOffset[opl_ch][op];
			reg_base = base;
		} else {
			const auto &pmap = kChannelToOPL3[partner];
			op_off = kOperatorOffset[pmap.opl_ch][op - 2];
			reg_base = pmap.port_base;
		}

		uint8_t regs[11] = {};
		regs[0] = state_.read(reg_base | (kRegAMVibEGKSMult + op_off));
		regs[1] = state_.read(reg_base | (kRegKSLTL + op_off));
		regs[2] = state_.read(reg_base | (kRegAR_DR + op_off));
		regs[3] = state_.read(reg_base | (kRegSL_RR + op_off));
		regs[4] = state_.read(reg_base | (kRegWaveform + op_off));
		// regs[5..10] reserved, stay 0

		for (int r = 0; r < 11; ++r) {
			msg.push_back((regs[r] >> 4) & 0x0F);
			msg.push_back(regs[r] & 0x0F);
		}
	}

	// Primary channel register (feedback + connection)
	uint8_t fb_conn = state_.read(base | (kRegFeedbackConn + opl_ch));
	msg.push_back((fb_conn >> 4) & 0x0F);
	msg.push_back(fb_conn & 0x0F);

	// Paired channel register (if 4-op)
	if (is_four_op) {
		const auto &pmap = kChannelToOPL3[partner];
		uint8_t fb_conn2 = state_.read(pmap.port_base | (kRegFeedbackConn + pmap.opl_ch));
		msg.push_back((fb_conn2 >> 4) & 0x0F);
		msg.push_back(fb_conn2 & 0x0F);
	}

	msg.push_back(0xF7);

	midi_output_(msg);
}

void DirectMode::sysex_patch_load(const uint8_t *data, size_t len)
{
	// midi-ch, then nibble-encoded operator data.
	// Each operator: 11 parameter bytes → 22 nibbles.
	// 2-op: 2 operators + channel byte = 44 + 2 = 46 nibbles
	// 4-op: 4 operators + 2 channel bytes = 88 + 4 = 92 nibbles
	// Auto-detect: if enough data for 4 ops, load all 4.
	if (len < 1) return;
	uint8_t midi_ch = data[0];
	if (midi_ch >= 18) return;

	const auto &map = kChannelToOPL3[midi_ch];
	uint16_t base = map.port_base;
	uint8_t opl_ch = map.opl_ch;

	int partner = four_op_partner(midi_ch);
	const uint8_t *p = data + 1;
	size_t remaining = len - 1;

	// Determine max operators: 4 if enough data and channel is pairable
	int max_ops = (remaining >= 22 * 4 + 4 && partner >= 0) ? 4 : 2;

	for (int op = 0; op < max_ops && remaining >= 22; ++op) {
		uint8_t op_off;
		uint16_t reg_base;
		if (op < 2) {
			op_off = kOperatorOffset[opl_ch][op];
			reg_base = base;
		} else {
			const auto &pmap = kChannelToOPL3[partner];
			op_off = kOperatorOffset[pmap.opl_ch][op - 2];
			reg_base = pmap.port_base;
		}

		uint8_t regs[11];
		for (int r = 0; r < 11; ++r) {
			regs[r] = static_cast<uint8_t>((p[r * 2] << 4) | (p[r * 2 + 1] & 0x0F));
		}
		p += 22;
		remaining -= 22;

		state_.write(reg_base | (kRegAMVibEGKSMult + op_off), regs[0]);
		state_.write(reg_base | (kRegKSLTL + op_off), regs[1]);
		state_.write(reg_base | (kRegAR_DR + op_off), regs[2]);
		state_.write(reg_base | (kRegSL_RR + op_off), regs[3]);
		state_.write(reg_base | (kRegWaveform + op_off), regs[4]);
	}

	// Primary channel register (feedback + connection)
	if (remaining >= 2) {
		uint8_t fb_conn = static_cast<uint8_t>((p[0] << 4) | (p[1] & 0x0F));
		state_.modify_bits(base | (kRegFeedbackConn + opl_ch), 0x0F, fb_conn & 0x0F);
		p += 2;
		remaining -= 2;
	}

	// Paired channel register (if 4-op data present)
	if (max_ops == 4 && remaining >= 2 && partner >= 0) {
		const auto &pmap = kChannelToOPL3[partner];
		uint8_t fb_conn2 = static_cast<uint8_t>((p[0] << 4) | (p[1] & 0x0F));
		state_.modify_bits(pmap.port_base | (kRegFeedbackConn + pmap.opl_ch), 0x0F, fb_conn2 & 0x0F);
	}
}

void DirectMode::sysex_reset_all()
{
	init();
}

void DirectMode::sysex_hw_reset()
{
	// Send hardware reset sequence: writes to 0xFE and 0xFF
	state_.write(0x0FE, 0x00);
	state_.write(0x0FF, 0x00);
	init();
}

// --- Per-OPL3-channel methods (used by VoiceAllocator) ---

void DirectMode::play_note_on_channel(uint8_t opl3_ch, uint8_t note, uint8_t vel)
{
	if (opl3_ch >= 18) return;
	auto &cs = channels_[opl3_ch];

	// If a note is already sounding, turn it off first
	if (cs.current_note >= 0) {
		write_freq(opl3_ch, 0, 0, false);
	}

	cs.current_note = static_cast<int8_t>(note);
	cs.note_velocity = vel;
	cs.sustained_note = false;

	const auto &nf = note_freq(note);
	const auto &map = kChannelToOPL3[opl3_ch];

	// Set carrier output level based on velocity + volume + expression
	uint8_t car_off = kOperatorOffset[map.opl_ch][1];
	uint8_t base_atten = compute_attenuation(cs.volume, cs.expression);
	uint8_t vel_atten = static_cast<uint8_t>((127 - vel) >> 1);
	uint8_t total_atten = std::min<int>(base_atten + vel_atten, 63);

	state_.modify_bits(map.port_base | (kRegKSLTL + car_off), 0x3F, total_atten);

	write_freq(opl3_ch, nf.f_num, nf.block, true);
}

void DirectMode::release_note_on_channel(uint8_t opl3_ch)
{
	if (opl3_ch >= 18) return;
	auto &cs = channels_[opl3_ch];

	if (cs.current_note < 0)
		return;

	const auto &map = kChannelToOPL3[opl3_ch];
	state_.modify_bits(map.port_base | (kRegKeyOnBlkFNum + map.opl_ch), 0x20, 0x00);
	cs.current_note = -1;
	cs.sustained_note = false;
}

void DirectMode::bend_channel(uint8_t opl3_ch, uint16_t f_num, uint8_t block)
{
	if (opl3_ch >= 18) return;

	// Preserve current key-on state
	const auto &map = kChannelToOPL3[opl3_ch];
	uint8_t cur_b0 = state_.read(map.port_base | (kRegKeyOnBlkFNum + map.opl_ch));
	bool current_key_on = (cur_b0 & 0x20) != 0;
	write_freq(opl3_ch, f_num, block, current_key_on);
}

void DirectMode::apply_cc_to_channel(uint8_t opl3_ch, uint8_t cc, uint8_t val)
{
	if (opl3_ch >= 18) return;

	switch (cc) {
	case 1:   cc_mod_wheel(opl3_ch, val); break;
	case 7:   cc_volume(opl3_ch, val); break;
	case 10:  cc_pan(opl3_ch, val); break;
	case 11:  cc_expression(opl3_ch, val); break;
	case 64:  cc_sustain(opl3_ch, val); break;
	case 74:  cc_brightness(opl3_ch, val); break;
	case 120: cc_all_sound_off(opl3_ch); break;
	case 123: cc_all_notes_off(opl3_ch); break;
	default: break;
	}
}

// --- Percussion ---

// Drum → OPL3 channel (port 0), BD key-on bit mask
// BD uses ch6 (both ops), SD uses ch7 carrier, HH uses ch7 mod,
// TT uses ch8 mod, CY uses ch8 carrier.
// Frequency: BD→ch6, SD/HH→ch7, TT/CY→ch8.
static constexpr uint8_t kDrumFreqChannel[5] = {6, 7, 8, 8, 7}; // BD,SD,TT,CY,HH
static constexpr uint8_t kDrumBDMask[5] = {0x10, 0x08, 0x04, 0x02, 0x01};

void DirectMode::perc_note_on(Drum drum, uint8_t note, uint8_t vel)
{
	if (drum >= kNumDrums) return;

	// Set frequency on the drum's OPL3 channel (port 0)
	uint8_t freq_ch = kDrumFreqChannel[drum];
	const auto &nf = opl3::note_freq(note);

	// Write freq regs directly to port 0 (don't use write_freq which
	// indexes through kChannelToOPL3 — we want port 0 explicitly).
	state_.write(kRegFNumLow + freq_ch, static_cast<uint8_t>(nf.f_num & 0xFF));
	// B0: block + fnum high, but do NOT set key-on bit (drums use BD reg)
	uint8_t b0 = static_cast<uint8_t>(((nf.f_num >> 8) & 0x03) | ((nf.block & 0x07) << 2));
	state_.write(kRegKeyOnBlkFNum + freq_ch, b0);

	// Set carrier level from velocity
	// Each drum uses a specific operator for output. Map velocity to
	// attenuation on the relevant operator's KSL/TL register.
	static constexpr uint8_t kDrumOperator[5] = {
		// BD: carrier of ch6 (op slot 1)
		kOperatorOffset[6][1],
		// SD: carrier of ch7 (op slot 1)
		kOperatorOffset[7][1],
		// TT: modulator of ch8 (op slot 0)
		kOperatorOffset[8][0],
		// CY: carrier of ch8 (op slot 1)
		kOperatorOffset[8][1],
		// HH: modulator of ch7 (op slot 0)
		kOperatorOffset[7][0],
	};
	uint8_t drum_op = kDrumOperator[drum];
	uint8_t vel_atten = static_cast<uint8_t>((127 - vel) >> 1); // 0-63
	state_.modify_bits(kRegKSLTL + drum_op, 0x3F, vel_atten);

	// Trigger key-on via BD register
	state_.modify_bits(kRegBD, kDrumBDMask[drum], kDrumBDMask[drum]);
}

void DirectMode::perc_note_off(Drum drum)
{
	if (drum >= kNumDrums) return;
	state_.modify_bits(kRegBD, kDrumBDMask[drum], 0x00);
}

} // namespace retrowave
