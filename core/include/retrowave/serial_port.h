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

#include <cstddef>
#include <cstdint>
#include <string>

namespace retrowave {

// Abstract serial port interface. Implemented by QSerialPort wrapper (GUI)
// and POSIX termios (CLI).
class SerialPort {
public:
	virtual ~SerialPort() = default;

	virtual bool open(const std::string &port_name) = 0;
	virtual void close() = 0;
	virtual bool is_open() const = 0;
	virtual bool write(const uint8_t *data, size_t len) = 0;
};

} // namespace retrowave
