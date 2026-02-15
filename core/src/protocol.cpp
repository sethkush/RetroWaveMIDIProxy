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

#include <retrowave/protocol.h>

namespace retrowave {

size_t protocol_serial_pack(const uint8_t *buf_in, size_t len_in, uint8_t *buf_out)
{
	size_t in_cursor = 0;
	size_t out_cursor = 0;

	buf_out[out_cursor] = 0x00;
	out_cursor += 1;

	uint8_t shift_count = 0;

	while (in_cursor < len_in) {
		uint8_t cur_byte_out = buf_in[in_cursor] >> shift_count;
		if (in_cursor > 0)
			cur_byte_out |= (buf_in[in_cursor - 1] << (8 - shift_count));

		cur_byte_out |= 0x01;
		buf_out[out_cursor] = cur_byte_out;

		shift_count += 1;
		in_cursor += 1;
		out_cursor += 1;
		if (shift_count > 7) {
			shift_count = 0;
			in_cursor -= 1;
		}
	}

	if (shift_count) {
		buf_out[out_cursor] = buf_in[in_cursor - 1] << (8 - shift_count);
		buf_out[out_cursor] |= 0x01;
		out_cursor += 1;
	}

	buf_out[out_cursor] = 0x02;
	out_cursor += 1;

	return out_cursor;
}

} // namespace retrowave
