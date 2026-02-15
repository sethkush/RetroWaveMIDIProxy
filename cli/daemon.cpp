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

#include "daemon.h"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <dirent.h>
#include <sys/stat.h>

// The same RetroWaveOPL3 chip class used in the GUI, adapted for CLI.
class RetroWaveOPL3CLI final : public OPLChipBaseT<RetroWaveOPL3CLI> {
public:
	retrowave::OPL3HardwareBuffer *hw_;

	RetroWaveOPL3CLI(retrowave::OPL3HardwareBuffer *hw) : hw_(hw) {
		hw_->reset();

		writeReg(0x004, 96);
		writeReg(0x004, 128);
		writeReg(0x105, 0x0);
		writeReg(0x105, 0x1);
		writeReg(0x105, 0x0);
		writeReg(0x001, 32);
		writeReg(0x105, 0x1);
	}

	bool canRunAtPcmRate() const override { return true; }

	void writeReg(uint16_t addr, uint8_t data) override {
		hw_->queue(addr, data);
	}

	void nativePreGenerate() override {}
	void nativePostGenerate() override {}
	void nativeGenerate(int16_t *frame) override {}
	const char *emulatorName() override { return "RetroWave"; }
	ChipType chipType() override { return CHIPTYPE_OPL3; }
};

Daemon::Daemon()
{
	router_.set_direct_mode(&direct_mode_);
	router_.set_voice_allocator(&voice_alloc_);

	// Wire up MIDI output for SysEx responses (patch dumps, voice queries)
	auto midi_out_fn = [this](const std::vector<uint8_t> &msg) {
		if (midiout_ && midiout_->isPortOpen())
			midiout_->sendMessage(&msg);
	};
	direct_mode_.set_midi_output(midi_out_fn);
	voice_alloc_.set_midi_output(midi_out_fn);
}

Daemon::~Daemon()
{
	cleanup();
}

bool Daemon::init_serial()
{
	if (serial_port_name_.empty()) {
		fprintf(stderr, "Error: no serial port specified\n");
		return false;
	}

	if (!serial_.open(serial_port_name_)) {
		fprintf(stderr, "Error: failed to open serial port: %s\n",
		        serial_port_name_.c_str());
		return false;
	}

	return true;
}

bool Daemon::init_midi()
{
	try {
		midiin_ = new RtMidiIn(RtMidi::UNSPECIFIED, "RetroWaveMIDI", 1024);
		midiout_ = new RtMidiOut(RtMidi::UNSPECIFIED, "RetroWaveMIDI");
	} catch (RtMidiError &e) {
		fprintf(stderr, "Error: failed to initialize RtMidi: %s\n",
		        e.getMessage().c_str());
		return false;
	}

	midiin_->setCallback(&midi_on_receive, this);
	midiin_->setErrorCallback(&midi_on_error, this);

	// Enable SysEx reception
	midiin_->ignoreTypes(false, true, true);

	try {
		if (midi_virtual_) {
			midiin_->openVirtualPort("RetroWaveMIDI MIDI In");
			midiout_->openVirtualPort("RetroWaveMIDI MIDI Out");
			fprintf(stderr, "Opened virtual MIDI ports\n");
		} else {
			midiin_->openPort(midi_port_);
			if (midi_port_ < static_cast<int>(midiout_->getPortCount()))
				midiout_->openPort(midi_port_);
			else
				midiout_->openVirtualPort("RetroWaveMIDI MIDI Out");
			fprintf(stderr, "Opened MIDI port %d: %s\n", midi_port_,
			        midiin_->getPortName(midi_port_).c_str());
		}
	} catch (RtMidiError &e) {
		fprintf(stderr, "Error: failed to open MIDI port: %s\n",
		        e.getMessage().c_str());
		return false;
	}

	return true;
}

bool Daemon::init_adlmidi()
{
	if (router_.mode() == retrowave::RoutingMode::Direct) {
		direct_mode_.init();
		hw_buf_.flush();
		return true;
	}

	adl_midi_player_ = adl_init(1000);
	if (!adl_midi_player_) {
		fprintf(stderr, "Error: failed to initialize ADLMIDI\n");
		return false;
	}

	adl_setNumChips(adl_midi_player_, 1);
	adl_setSoftPanEnabled(adl_midi_player_, 1);
	adl_setVolumeRangeModel(adl_midi_player_, volmodel_id_);

	int rc;
	if (bank_path_.empty()) {
		rc = adl_setBank(adl_midi_player_, bank_id_);
	} else {
		rc = adl_openBankFile(adl_midi_player_, bank_path_.c_str());
	}

	if (rc) {
		fprintf(stderr, "Error: failed to open bank: %s\n",
		        adl_errorInfo(adl_midi_player_));
		adl_close(adl_midi_player_);
		adl_midi_player_ = nullptr;
		return false;
	}

	auto *real_midiplay = static_cast<MIDIplay *>(adl_midi_player_->adl_midiPlayer);
	auto *synth = real_midiplay->m_synth.get();
	auto &chips = synth->m_chips;

	auto *opl3 = new RetroWaveOPL3CLI(&hw_buf_);
	assert(chips.size() == 1);
	chips[0].reset(opl3);

	adl_midi_sequencer_ = real_midiplay->m_sequencer.get();

	synth->updateChannelCategories();
	synth->silenceAll();

	for (unsigned i = 0; i < 16; i++)
		adl_midi_sequencer_->setChannelEnabled(i, true);
	adl_midi_sequencer_->m_trackDisable.resize(16);

	return true;
}

void Daemon::cleanup()
{
	if (midiin_) {
		midiin_->closePort();
		delete midiin_;
		midiin_ = nullptr;
	}

	if (midiout_) {
		midiout_->closePort();
		delete midiout_;
		midiout_ = nullptr;
	}

	adl_midi_sequencer_ = nullptr;

	if (adl_midi_player_) {
		adl_close(adl_midi_player_);
		adl_midi_player_ = nullptr;
	}

	serial_.close();
}

int Daemon::run()
{
	if (!init_serial())
		return 1;
	if (!init_midi())
		return 1;
	if (!init_adlmidi())
		return 1;

	fprintf(stderr, "Running in %s mode. Press Ctrl+C to stop.\n",
	        router_.mode() == retrowave::RoutingMode::Direct ? "direct" : "bank");

	struct timespec ts;
	ts.tv_sec = 0;
	ts.tv_nsec = 1000000; // 1ms

	while (!should_stop_) {
		{
			std::lock_guard<std::mutex> lg(hw_buf_.mutex());

			if (router_.mode() == retrowave::RoutingMode::Bank && adl_midi_player_) {
				int16_t discard[8];
				adl_generate(adl_midi_player_, 2, discard);
			}

			hw_buf_.flush();
		}

		nanosleep(&ts, nullptr);
	}

	fprintf(stderr, "Shutting down...\n");
	cleanup();
	return 0;
}

void Daemon::midi_on_receive(double timeStamp, std::vector<unsigned char> *message, void *userData)
{
	auto *ctx = static_cast<Daemon *>(userData);
	std::lock_guard<std::mutex> lg(ctx->hw_buf_.mutex());

	if (ctx->router_.process(message->data(), message->size()))
		return;

	auto *ams = ctx->adl_midi_sequencer_;
	if (!ams) return;

	const uint8_t *pp = message->data();
	int s = 0;
	auto evt = ams->parseEvent(&pp, pp + message->size(), s);
	int32_t s2 = 0;
	ams->handleEvent(0, evt, s2);
}

void Daemon::midi_on_error(RtMidiError::Type type, const std::string &errorText, void *userData)
{
	fprintf(stderr, "MIDI error: %s\n", errorText.c_str());
}

void Daemon::list_midi_ports()
{
	try {
		RtMidiIn midi(RtMidi::UNSPECIFIED, "RetroWaveMIDI", 1024);
		unsigned count = midi.getPortCount();
		printf("Available MIDI input ports:\n");
		for (unsigned i = 0; i < count; i++)
			printf("  %u: %s\n", i, midi.getPortName(i).c_str());
		if (count == 0)
			printf("  (none)\n");
	} catch (RtMidiError &e) {
		fprintf(stderr, "Error: %s\n", e.getMessage().c_str());
	}
}

void Daemon::list_serial_ports()
{
	printf("Available serial ports:\n");
	DIR *dir = opendir("/dev");
	if (!dir) {
		fprintf(stderr, "Error: cannot read /dev\n");
		return;
	}

	bool found = false;
	struct dirent *ent;
	while ((ent = readdir(dir)) != nullptr) {
		if (strncmp(ent->d_name, "ttyUSB", 6) == 0 ||
		    strncmp(ent->d_name, "ttyACM", 6) == 0 ||
		    strncmp(ent->d_name, "ttyAMA", 6) == 0 ||
		    strncmp(ent->d_name, "ttyS", 4) == 0) {
			printf("  /dev/%s\n", ent->d_name);
			found = true;
		}
	}
	closedir(dir);

	if (!found)
		printf("  (none)\n");
}

void Daemon::list_banks()
{
	printf("Available banks:\n");
	for (size_t i = 0; i < g_embeddedBanksCount; i++)
		printf("  %3zu: %s\n", i, g_embeddedBanks[i].title);
}
