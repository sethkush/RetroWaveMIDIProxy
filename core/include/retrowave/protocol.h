/*
    This file is part of RetroWaveMIDIProxy
    Copyright (C) 2023 Reimu NotMoe <reimu@sudomaker.com>
    Copyright (C) 2023 Yukino Song <yukino@sudomaker.com>

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

namespace retrowave {

// Encodes raw bytes into the RetroWave serial wire protocol.
// buf_out must be at least (len_in * 2 + 8) bytes.
// Returns the number of bytes written to buf_out.
size_t protocol_serial_pack(const uint8_t *buf_in, size_t len_in, uint8_t *buf_out);

} // namespace retrowave
