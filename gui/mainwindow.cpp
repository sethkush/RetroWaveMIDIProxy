/*
    This file is part of RetroWaveMIDIProxyGUI
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


#include "mainwindow.h"
#include "./ui_mainwindow.h"

class RetroWaveOPL3 final : public OPLChipBaseT<RetroWaveOPL3> {
public:
	MainWindow *mw_;

public:
	RetroWaveOPL3(MainWindow *mw) : mw_(mw) {
		auto &hw = mw_->hw_buf();
		hw.reset();

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
		mw_->hw_buf().queue(addr, data);
	};

	void nativePreGenerate() override {}
	void nativePostGenerate() override {}
	void nativeGenerate(int16_t *frame) override {};
	const char *emulatorName() override { return "RetroWave"; };
	ChipType chipType() override {return CHIPTYPE_OPL3;};
};

MainWindow::MainWindow(QWidget *parent)
	: QMainWindow(parent)
	, ui(new Ui::MainWindow)
{
	ui->setupUi(this);

	midi_init();

	midi_router_.set_direct_mode(&direct_mode_);

	bank_refresh_available();
	spp_refresh_available();
	midi_refresh_available();

	ui->statusbar->showMessage("READY");

}

MainWindow::~MainWindow()
{
	if (started)
		stop();
	delete midiin;
	delete midiout;
	delete ui;
}

void MainWindow::spp_refresh_available() {
	ui->cb_spp_list->clear();

	for (const auto &info : QSerialPortInfo::availablePorts()) {
		QString disp;
		auto nom = info.portName();
		auto desc = info.description();
		if (!desc.isEmpty())
			disp = nom + " | " + desc;
		else
			disp = std::move(nom);

		ui->cb_spp_list->addItem(disp, info.portName());
	}
}

void MainWindow::bank_refresh_available() {
	ui->cb_bank_list->clear();

	ui->cb_bank_list->addItem(tr("<Choose from file>"));

	for (size_t i=0; i<g_embeddedBanksCount; i++) {
		auto &it = g_embeddedBanks[i];

		ui->cb_bank_list->addItem(QString::number(i) + " - " + it.title, QVariant::fromValue(i));
	}

	ui->cb_bank_list->setCurrentIndex(59);
}

void MainWindow::midi_refresh_available() {
	unsigned count = midiin->getPortCount();

	ui->cb_midiport_list->clear();

	ui->cb_midiport_list->addItem(tr("<Virtual Port>"), QVariant::fromValue(-1));

	for (unsigned i=0; i<count; i++) {
		std::string name = midiin->getPortName(i);

		ui->cb_midiport_list->addItem(QString::number(i) + tr(" - ") + QString::fromStdString(name), QVariant::fromValue(i));
	}
}

void MainWindow::on_btn_start_stop_clicked()
{
	if (!started) {
		if (start()) {
			ui->cb_bank_list->setEnabled(false);
			ui->cb_spp_list->setEnabled(false);
			ui->cb_midiport_list->setEnabled(false);
			ui->cb_volmodel_list->setEnabled(false);
			ui->cb_mode_list->setEnabled(false);
			ui->btn_midi_refresh->setEnabled(false);
			ui->btn_spp_refresh->setEnabled(false);
			ui->btn_start_stop->setText(tr("Stop"));
			ui->statusbar->showMessage(tr("Started"));
		} else {
			return;
		}
	} else {
		stop();
		ui->cb_bank_list->setEnabled(true);
		ui->cb_spp_list->setEnabled(true);
		ui->cb_midiport_list->setEnabled(true);
		ui->cb_volmodel_list->setEnabled(true);
		ui->cb_mode_list->setEnabled(true);
		ui->btn_midi_refresh->setEnabled(true);
		ui->btn_spp_refresh->setEnabled(true);
		ui->btn_start_stop->setText(tr("Start"));
		ui->statusbar->showMessage(tr("Stopped"));
	}

	started = !started;
}

void MainWindow::on_btn_midi_refresh_clicked()
{
	midi_refresh_available();
}

void MainWindow::on_btn_spp_refresh_clicked() {
	spp_refresh_available();
}

void MainWindow::on_cb_bank_list_activated(int index)
{
	if (index == 0) {
		bank_path = QFileDialog::getOpenFileName(this, tr("Open Bank File"), bank_last_dir);
		if (!bank_path.isEmpty()) {
			ui->cb_bank_list->insertItem(1, bank_path, QVariant::fromValue(-1));
		}
		ui->cb_bank_list->setCurrentIndex(1);
	}

	bank_id = ui->cb_bank_list->currentData().toInt();

	if (bank_id == -1) {
		bank_path = ui->cb_bank_list->currentText();
		ui->statusbar->showMessage(tr("Bank file selected."));
	} else {
		ui->statusbar->showMessage(tr("Bank ") + QString::number(bank_id) + tr(" selected."));
	}

}

void MainWindow::on_cb_volmodel_list_activated(int index)
{
	volmodel_id = index;
	ui->statusbar->showMessage(tr("Volume model ") + ui->cb_volmodel_list->currentText() + tr(" selected."));
}

void MainWindow::on_cb_midiport_list_activated(int index)
{
	midi_port = ui->cb_midiport_list->itemData(index).toInt();

	if (midi_port == -1) {
		ui->statusbar->showMessage(tr("Virtual MIDI port selected."));
	} else {
		ui->statusbar->showMessage(tr("System MIDI port ") + QString::number(midi_port) + tr(" selected."));
	}
}

void MainWindow::on_cb_mode_list_activated(int index)
{
	bool is_direct = (index == 1);
	midi_router_.set_mode(is_direct ? retrowave::RoutingMode::Direct
	                                : retrowave::RoutingMode::Bank);

	// Disable bank-specific controls in direct mode
	ui->cb_bank_list->setEnabled(!is_direct && !started);
	ui->cb_volmodel_list->setEnabled(!is_direct && !started);

	ui->statusbar->showMessage(is_direct ? tr("Direct mode selected.")
	                                     : tr("Bank mode selected."));
}

bool MainWindow::start() {
	int rc;

	if (!serial_.open(ui->cb_spp_list->currentData().toString().toStdString())) {
		QMessageBox::warning(this, tr("Error"), tr("Failed to open serial port"));
		return false;
	}

	if (midi_port == -1) {
		midiin->openVirtualPort(QApplication::applicationName().toStdString() + " MIDI In");
		midiout->openVirtualPort(QApplication::applicationName().toStdString() + " MIDI Out");
	} else {
		midiin->openPort(midi_port);
		// Open matching output port (or virtual if no match)
		if (midi_port < static_cast<int>(midiout->getPortCount()))
			midiout->openPort(midi_port);
		else
			midiout->openVirtualPort(QApplication::applicationName().toStdString() + " MIDI Out");
	}

	// Enable SysEx reception (disabled by default in rtmidi)
	midiin->ignoreTypes(false, true, true);

	if (midi_router_.mode() == retrowave::RoutingMode::Direct) {
		// Direct mode: init OPL3 state directly, no libADLMIDI needed
		direct_mode_.init();
		hw_buf_.flush();

		tmr_adl = new QTimer(this);
		tmr_adl->setTimerType(Qt::PreciseTimer);
		connect(tmr_adl, SIGNAL(timeout()), this, SLOT(a_adl_timer_timeout()));
		tmr_adl->start(1);

		return true;
	}

	// Bank mode: use libADLMIDI
	adl_midi_player = adl_init(1000);

	if (!adl_midi_player) {
		QMessageBox::warning(this, tr("Error"), tr("Failed to initialize ADLMIDI"));
		abort();
	}

	adl_setNumChips(adl_midi_player, 1);
	adl_setSoftPanEnabled(adl_midi_player, 1);
	adl_setVolumeRangeModel(adl_midi_player, volmodel_id);
	if (bank_id != -1) {
		rc = adl_setBank(adl_midi_player, bank_id);
	} else {
		rc = adl_openBankFile(adl_midi_player, bank_path.toStdString().c_str());
	}

	if (rc) {
		QMessageBox::warning(this, tr("Error"), tr("Failed to open bank: ") + tr(adl_errorInfo(adl_midi_player)));
		delete adl_midi_player;
		adl_midi_player = nullptr;
		return false;
	}

	auto *real_midiplay = static_cast<MIDIplay *>(adl_midi_player->adl_midiPlayer);
	auto *synth = real_midiplay->m_synth.get();
	auto &chips = synth->m_chips;
	assert(chips.size() == 1);
	auto *vgmopl3 = new RetroWaveOPL3(this);
	chips[0].reset(vgmopl3);

	adl_midi_sequencer = real_midiplay->m_sequencer.get();

	synth->updateChannelCategories();
	synth->silenceAll();


	for (unsigned i=0; i<16; i++) {
		adl_midi_sequencer->setChannelEnabled(i, true);
	}

	adl_midi_sequencer->m_trackDisable.resize(16);

	tmr_adl = new QTimer(this);
	tmr_adl->setTimerType(Qt::PreciseTimer);

	connect(tmr_adl, SIGNAL(timeout()),this, SLOT(a_adl_timer_timeout()));
	tmr_adl->start(1);

	return true;
}

void MainWindow::stop() {
	midiin->closePort();
	midiout->closePort();

	tmr_adl->stop();
	delete tmr_adl;
	tmr_adl = nullptr;

	adl_midi_sequencer = nullptr;

	if (adl_midi_player) {
		adl_close(adl_midi_player);
		adl_midi_player = nullptr;
	}

	serial_.close();
}


void MainWindow::midi_init() {
	midiin = new RtMidiIn(RtMidi::UNSPECIFIED, QCoreApplication::applicationName().toStdString(), 1024);

	if (!midiin) {
		QMessageBox::warning(this, tr("Error"), tr("Failed to initialize RtMidi"));
		abort();
	}

	midiin->setCallback(&midi_on_receive, this);
	midiin->setErrorCallback(&midi_on_error, this);

	midiout = new RtMidiOut(RtMidi::UNSPECIFIED, QCoreApplication::applicationName().toStdString());

	// Wire up MIDI output for SysEx patch dumps
	direct_mode_.set_midi_output([this](const std::vector<uint8_t> &msg) {
		if (midiout && midiout->isPortOpen()) {
			midiout->sendMessage(&msg);
		}
	});
}

void MainWindow::midi_on_receive(double timeStamp, std::vector<unsigned char> *message, void *userData) {
	auto *ctx = (MainWindow *)userData;
	std::lock_guard<std::mutex> lg(ctx->hw_buf().mutex());

	// Try direct mode first
	if (ctx->midi_router_.process(message->data(), message->size()))
		return;

	// Bank mode: forward to libADLMIDI sequencer
	auto *ams = ctx->adl_midi_sequencer;
	if (!ams) return;

	const uint8_t *pp = message->data();
	int s = 0;

	auto evt = ams->parseEvent(&pp, pp + message->size(), s);
	int32_t s2 = 0;

	ams->handleEvent(0, evt, s2);
}

void MainWindow::midi_on_error(RtMidiError::Type type, const std::string &errorText, void *userData) {

}

void MainWindow::a_adl_timer_timeout() {
	std::lock_guard<std::mutex> lg(hw_buf_.mutex());

	if (midi_router_.mode() == retrowave::RoutingMode::Bank && adl_midi_player) {
		int16_t discard[8];
		adl_generate(adl_midi_player, 2, discard);
	}

	hw_buf_.flush();
}
