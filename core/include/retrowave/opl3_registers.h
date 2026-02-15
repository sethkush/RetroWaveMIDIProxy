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

namespace retrowave {
namespace opl3 {

// OPL3 has 18 2-op channels (or 6 4-op + 6 2-op).
// Port 0: channels 0-8, port 1: channels 9-17.
static constexpr int kNumChannels = 18;
static constexpr int kNumOperators = 36;

// Base register addresses
static constexpr uint8_t kRegTest        = 0x01;
static constexpr uint8_t kRegTimer1      = 0x02;
static constexpr uint8_t kRegTimer2      = 0x03;
static constexpr uint8_t kRegTimerCtrl   = 0x04;
static constexpr uint8_t kRegOPL3Enable  = 0x05; // Port 1 only (0x105)
static constexpr uint8_t kRegCSW         = 0x08;

// Per-operator registers (add operator offset)
static constexpr uint8_t kRegAMVibEGKSMult = 0x20; // AM, Vib, EGT, KSR, Mult
static constexpr uint8_t kRegKSLTL        = 0x40;  // Key Scale Level, Total Level
static constexpr uint8_t kRegAR_DR        = 0x60;  // Attack Rate, Decay Rate
static constexpr uint8_t kRegSL_RR        = 0x80;  // Sustain Level, Release Rate
static constexpr uint8_t kRegWaveform     = 0xE0;  // Waveform select

// Per-channel registers (add channel offset 0-8)
static constexpr uint8_t kRegFNumLow      = 0xA0;
static constexpr uint8_t kRegKeyOnBlkFNum = 0xB0; // Key-On, Block, F-Num high
static constexpr uint8_t kRegFeedbackConn = 0xC0; // Feedback, Connection, Pan

// Global registers
static constexpr uint8_t kRegBD           = 0xBD; // Tremolo/Vibrato depth, Percussion
static constexpr uint16_t kReg4OpEnable   = 0x104; // 4-op channel enable (port 1)

// Operator offset table: maps (channel 0-8, operator 0/1) to register offset.
// Operator 0 = modulator, operator 1 = carrier.
// For 4-op, operators 2 and 3 are on the paired channel.
static constexpr uint8_t kOperatorOffset[9][2] = {
	{0x00, 0x03}, // ch 0: op 0,3
	{0x01, 0x04}, // ch 1: op 1,4
	{0x02, 0x05}, // ch 2: op 2,5
	{0x08, 0x0B}, // ch 3: op 8,B
	{0x09, 0x0C}, // ch 4: op 9,C
	{0x0A, 0x0D}, // ch 5: op A,D
	{0x10, 0x13}, // ch 6: op 10,13
	{0x11, 0x14}, // ch 7: op 11,14
	{0x12, 0x15}, // ch 8: op 12,15
};

// 4-op channel pairs (indices into port-local 0-8):
// ch0+ch3, ch1+ch4, ch2+ch5 — on each port
static constexpr uint8_t kFourOpPairs[3][2] = {
	{0, 3}, {1, 4}, {2, 5},
};

// Bits in 0x104 for enabling 4-op on each pair:
// bit 0 = port0 ch0+3, bit 1 = port0 ch1+4, bit 2 = port0 ch2+5
// bit 3 = port1 ch0+3, bit 4 = port1 ch1+4, bit 5 = port1 ch2+5
static constexpr uint8_t kFourOpEnableBit[6] = {
	0x01, 0x02, 0x04, // port 0
	0x08, 0x10, 0x20, // port 1
};

// Returns the 4-op partner of a global OPL3 channel index (0-17).
// Returns -1 if the channel is not pairable (6-8, 15-17).
// 0↔3, 1↔4, 2↔5, 9↔12, 10↔13, 11↔14.
inline int four_op_partner(int ch)
{
	static constexpr int kPartners[18] = {
		3, 4, 5,     // 0→3, 1→4, 2→5
		0, 1, 2,     // 3→0, 4→1, 5→2
		-1, -1, -1,  // 6,7,8 not pairable
		12, 13, 14,  // 9→12, 10→13, 11→14
		9, 10, 11,   // 12→9, 13→10, 14→11
		-1, -1, -1,  // 15,16,17 not pairable
	};
	if (ch < 0 || ch >= 18) return -1;
	return kPartners[ch];
}

// MIDI note to OPL3 F-Number and Block.
// F-Number is 10 bits (0-1023), Block is 3 bits (0-7).
// Computed for OPL3 master clock = 14.318 MHz.
// f = F-Num * 14318180 / (2^20 * 2^(7-Block))
struct NoteFreq {
	uint16_t f_num;
	uint8_t block;
};

// Get precomputed frequency data for a MIDI note (0-127).
const NoteFreq &note_freq(int midi_note);

// Map channel index to OPL3 channel and port.
// Indices 0-8:  port 0, channels 0-8
// Indices 9-17: port 1, channels 0-8
// Indices 0-15 also correspond to MIDI channels for process_midi().
struct ChannelMap {
	uint8_t opl_ch;     // 0-8 channel offset within port
	uint16_t port_base; // 0x000 for port 0, 0x100 for port 1
};

extern const ChannelMap kChannelToOPL3[18];

} // namespace opl3
} // namespace retrowave
