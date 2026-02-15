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

#include <retrowave/opl3_hw.h>
#include <retrowave/protocol.h>

namespace retrowave {

OPL3HardwareBuffer::OPL3HardwareBuffer(SerialPort &serial)
	: serial_(serial)
{
	buf_.reserve(512);
	reset();
}

void OPL3HardwareBuffer::reset()
{
	buf_.clear();
	buf_.push_back(0x21 << 1);
	buf_.push_back(0x12);
}

void OPL3HardwareBuffer::queue(uint16_t addr, uint8_t data)
{
	bool port1 = (addr & 0x100) != 0;

	buf_.push_back(port1 ? 0xe5 : 0xe1);
	buf_.push_back(addr & 0xff);
	buf_.push_back(port1 ? 0xe7 : 0xe3);
	buf_.push_back(data);
	buf_.push_back(0xfb);
	buf_.push_back(data);
}

void OPL3HardwareBuffer::flush()
{
	std::vector<uint8_t> packed(buf_.size() * 2 + 8);

	size_t packed_len = protocol_serial_pack(buf_.data(), buf_.size(),
	                                         packed.data());
	serial_.write(packed.data(), packed_len);
	reset();
}

} // namespace retrowave
