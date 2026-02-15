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

#include <atomic>
#include <mutex>
#include <string>

#include <retrowave/serial_posix.h>
#include <retrowave/opl3_hw.h>
#include <retrowave/opl3_state.h>
#include <retrowave/direct_mode.h>
#include <retrowave/voice_allocator.h>
#include <retrowave/midi_router.h>

#include <RtMidi.h>
#include <adlmidi.h>
#include <adlmidi_midiplay.hpp>
#include <adlmidi_opl3.hpp>
#include <chips/opl_chip_base.h>
#include <midi_sequencer.hpp>

class Daemon {
public:
	Daemon();
	~Daemon();

	// Configuration (set before run())
	void set_serial_port(const std::string &port) { serial_port_name_ = port; }
	void set_midi_port(int port) { midi_port_ = port; }
	void set_midi_virtual(bool v) { midi_virtual_ = v; }
	void set_mode(retrowave::RoutingMode mode) { router_.set_mode(mode); }
	void set_bank_id(int id) { bank_id_ = id; }
	void set_bank_path(const std::string &path) { bank_path_ = path; }
	void set_volume_model(int model) { volmodel_id_ = model; }

	// Run the main loop (blocks until should_stop_ is set)
	int run();

	// Signal the daemon to stop
	void request_stop() { should_stop_ = true; }

	// List available devices
	static void list_midi_ports();
	static void list_serial_ports();
	static void list_banks();

private:
	bool init_serial();
	bool init_midi();
	bool init_adlmidi();
	void cleanup();

	static void midi_on_receive(double timeStamp, std::vector<unsigned char> *message, void *userData);
	static void midi_on_error(RtMidiError::Type type, const std::string &errorText, void *userData);

	retrowave::PosixSerialPort serial_;
	retrowave::OPL3HardwareBuffer hw_buf_{serial_};
	retrowave::OPL3State opl3_state_{hw_buf_};
	retrowave::DirectMode direct_mode_{opl3_state_};
	retrowave::VoiceAllocator voice_alloc_{direct_mode_, opl3_state_};
	retrowave::MidiRouter router_;

	std::string serial_port_name_;
	int midi_port_ = -1;
	bool midi_virtual_ = true;

	int bank_id_ = 58;
	std::string bank_path_;
	int volmodel_id_ = 0;

	RtMidiIn *midiin_ = nullptr;
	RtMidiOut *midiout_ = nullptr;
	ADL_MIDIPlayer *adl_midi_player_ = nullptr;
	MidiSequencer *adl_midi_sequencer_ = nullptr;

	std::atomic<bool> should_stop_{false};
};
