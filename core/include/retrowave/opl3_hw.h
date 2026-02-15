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
#include <mutex>
#include <vector>

#include <retrowave/serial_port.h>

namespace retrowave {

// Buffers OPL3 register writes and flushes them to serial as packed protocol frames.
// Thread-safe: the mutex must be held by callers across queue/flush/reset sequences.
class OPL3HardwareBuffer {
public:
	explicit OPL3HardwareBuffer(SerialPort &serial);

	// Reset buffer to initial state (RetroWave command header).
	void reset();

	// Queue a single OPL3 register write. addr bit 0x100 selects port A vs B.
	void queue(uint16_t addr, uint8_t data);

	// Pack and flush the buffer contents to serial, then reset.
	void flush();

	std::mutex &mutex() { return mutex_; }

private:
	SerialPort &serial_;
	std::vector<uint8_t> buf_;
	std::mutex mutex_;
};

} // namespace retrowave
