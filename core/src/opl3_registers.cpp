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

#include <retrowave/opl3_registers.h>
#include <cmath>

namespace retrowave {
namespace opl3 {

// OPL3 frequency formula: f = F-Num * 49716 / 2^(20 - Block)
// We want: F-Num = f * 2^(20-Block) / 49716, choosing Block so F-Num fits in 10 bits.
static constexpr double kOPL3FreqBase = 49716.0;

static NoteFreq compute_note_freq(int note)
{
	double freq = 440.0 * std::pow(2.0, (note - 69.0) / 12.0);

	for (int block = 0; block < 8; ++block) {
		double divisor = kOPL3FreqBase / static_cast<double>(1 << (20 - block));
		double f_num_d = freq / divisor;
		int f_num = static_cast<int>(f_num_d + 0.5);

		if (f_num <= 1023) {
			if (f_num < 0) f_num = 0;
			return {static_cast<uint16_t>(f_num), static_cast<uint8_t>(block)};
		}
	}

	return {1023, 7};
}

struct NoteFreqTable {
	NoteFreq entries[128];
	NoteFreqTable() {
		for (int i = 0; i < 128; ++i)
			entries[i] = compute_note_freq(i);
	}
};

static const NoteFreqTable &get_table()
{
	static const NoteFreqTable table;
	return table;
}

const NoteFreq &note_freq(int midi_note)
{
	static const NoteFreqTable &t = get_table();
	if (midi_note < 0) midi_note = 0;
	if (midi_note > 127) midi_note = 127;
	return t.entries[midi_note];
}

const ChannelMap kChannelToOPL3[18] = {
	{0, 0x000}, // 0  -> port 0 ch 0
	{1, 0x000}, // 1  -> port 0 ch 1
	{2, 0x000}, // 2  -> port 0 ch 2
	{3, 0x000}, // 3  -> port 0 ch 3
	{4, 0x000}, // 4  -> port 0 ch 4
	{5, 0x000}, // 5  -> port 0 ch 5
	{6, 0x000}, // 6  -> port 0 ch 6
	{7, 0x000}, // 7  -> port 0 ch 7
	{8, 0x000}, // 8  -> port 0 ch 8
	{0, 0x100}, // 9  -> port 1 ch 0
	{1, 0x100}, // 10 -> port 1 ch 1
	{2, 0x100}, // 11 -> port 1 ch 2
	{3, 0x100}, // 12 -> port 1 ch 3
	{4, 0x100}, // 13 -> port 1 ch 4
	{5, 0x100}, // 14 -> port 1 ch 5
	{6, 0x100}, // 15 -> port 1 ch 6
	{7, 0x100}, // 16 -> port 1 ch 7
	{8, 0x100}, // 17 -> port 1 ch 8
};

} // namespace opl3
} // namespace retrowave
