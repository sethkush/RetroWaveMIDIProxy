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
#include <cstring>

#include <retrowave/opl3_hw.h>

namespace retrowave {

// Shadow register file for the OPL3. Tracks all written values so we can
// do read-modify-write for bitfield operations (OPL3 is write-only).
// 256 registers per port, 2 ports = 512 bytes.
class OPL3State {
public:
	explicit OPL3State(OPL3HardwareBuffer &hw);

	// Read the shadow value (does not access hardware).
	uint8_t read(uint16_t addr) const;

	// Write a value and send to hardware.
	void write(uint16_t addr, uint8_t data);

	// Modify specific bits: clears bits in mask, then ORs in (value & mask).
	void modify_bits(uint16_t addr, uint8_t mask, uint8_t value);

	// Reset all shadow registers to 0 and send OPL3 init sequence.
	void reset();

private:
	OPL3HardwareBuffer &hw_;
	uint8_t regs_[512]; // [0..255] = port 0, [256..511] = port 1
};

} // namespace retrowave
