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

#include <retrowave/midi_router.h>
#include <retrowave/direct_mode.h>
#include <retrowave/voice_allocator.h>

namespace retrowave {

MidiRouter::MidiRouter() = default;

void MidiRouter::set_mode(RoutingMode mode)
{
	mode_ = mode;
}

bool MidiRouter::process(const uint8_t *data, size_t len)
{
	if (mode_ == RoutingMode::Bank)
		return false;

	if (len == 0)
		return false;

	if (voice_alloc_) {
		voice_alloc_->process_midi(data, len);
		return true;
	}

	if (!direct_)
		return false;

	direct_->process_midi(data, len);
	return true;
}

} // namespace retrowave
