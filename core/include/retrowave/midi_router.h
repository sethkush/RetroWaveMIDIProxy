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
#include <vector>

namespace retrowave {

class DirectMode;
class VoiceAllocator;

enum class RoutingMode {
	Bank,    // libADLMIDI handles everything
	Direct,  // Direct OPL3 register control via CC/NRPN/SysEx
};

// Routes incoming MIDI messages to either bank mode (libADLMIDI) or direct mode.
// In bank mode, the caller is responsible for forwarding to libADLMIDI's sequencer.
// In direct mode, this class delegates to DirectMode.
class MidiRouter {
public:
	MidiRouter();

	void set_mode(RoutingMode mode);
	RoutingMode mode() const { return mode_; }

	void set_direct_mode(DirectMode *dm) { direct_ = dm; }
	void set_voice_allocator(VoiceAllocator *va) { voice_alloc_ = va; }

	// Process a raw MIDI message. Returns true if handled (direct mode),
	// false if the caller should forward to libADLMIDI (bank mode).
	bool process(const uint8_t *data, size_t len);

private:
	RoutingMode mode_ = RoutingMode::Bank;
	DirectMode *direct_ = nullptr;
	VoiceAllocator *voice_alloc_ = nullptr;
};

} // namespace retrowave
