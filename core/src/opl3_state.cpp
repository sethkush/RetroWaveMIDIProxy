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

#include <retrowave/opl3_state.h>
#include <retrowave/opl3_registers.h>

namespace retrowave {

OPL3State::OPL3State(OPL3HardwareBuffer &hw)
	: hw_(hw)
{
	std::memset(regs_, 0, sizeof(regs_));
}

uint8_t OPL3State::read(uint16_t addr) const
{
	bool port1 = (addr & 0x100) != 0;
	return regs_[(port1 ? 256 : 0) + (addr & 0xFF)];
}

void OPL3State::write(uint16_t addr, uint8_t data)
{
	bool port1 = (addr & 0x100) != 0;
	regs_[(port1 ? 256 : 0) + (addr & 0xFF)] = data;
	hw_.queue(addr, data);
}

void OPL3State::modify_bits(uint16_t addr, uint8_t mask, uint8_t value)
{
	uint8_t cur = read(addr);
	uint8_t updated = (cur & ~mask) | (value & mask);
	write(addr, updated);
}

void OPL3State::reset()
{
	std::memset(regs_, 0, sizeof(regs_));

	// OPL3 init sequence (matches RetroWaveOPL3 constructor)
	hw_.queue(0x004, 96);
	hw_.queue(0x004, 128);
	hw_.queue(0x105, 0x00);
	hw_.queue(0x105, 0x01);
	hw_.queue(0x105, 0x00);
	hw_.queue(0x001, 32);
	hw_.queue(0x105, 0x01);

	regs_[256 + 0x05] = 0x01; // Track OPL3 enable in shadow

	// Clear all operator and channel registers
	for (int port = 0; port < 2; ++port) {
		uint16_t base = port ? 0x100 : 0x000;
		for (uint8_t reg = 0x20; reg <= 0x35; ++reg)
			hw_.queue(base | reg, 0);
		for (uint8_t reg = 0x40; reg <= 0x55; ++reg)
			hw_.queue(base | reg, 0x3F); // Max attenuation
		for (uint8_t reg = 0x60; reg <= 0x75; ++reg)
			hw_.queue(base | reg, 0);
		for (uint8_t reg = 0x80; reg <= 0x95; ++reg)
			hw_.queue(base | reg, 0x0F); // Fastest release
		for (uint8_t reg = 0xA0; reg <= 0xA8; ++reg)
			hw_.queue(base | reg, 0);
		for (uint8_t reg = 0xB0; reg <= 0xB8; ++reg)
			hw_.queue(base | reg, 0); // Key-off, zero freq
		for (uint8_t reg = 0xC0; reg <= 0xC8; ++reg)
			hw_.queue(base | reg, 0x30); // Both speakers on
		for (uint8_t reg = 0xE0; reg <= 0xF5; ++reg)
			hw_.queue(base | reg, 0);
	}
}

} // namespace retrowave
