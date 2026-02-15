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

#include <QMainWindow>
#include <QComboBox>
#include <QPushButton>
#include <QDial>
#include <QCheckBox>
#include <QLabel>
#include <QGroupBox>
#include <QTabWidget>
#include <QTimer>
#include <QSpinBox>
#include <QListWidget>
#include <QSerialPortInfo>

#include <RtMidi.h>

#include <retrowave/opl3_hw.h>
#include <retrowave/opl3_state.h>
#include <retrowave/direct_mode.h>
#include <retrowave/voice_allocator.h>
#include "fm_diagram_widget.h"
#include "serial_qt.h"

extern "C" {
#include <wopl_file.h>
}

class PanelWindow : public QMainWindow {
	Q_OBJECT

public:
	explicit PanelWindow(QWidget *parent = nullptr);
	~PanelWindow();

	struct OperatorWidgets {
		QDial *attack = nullptr, *decay = nullptr, *sustain = nullptr, *release = nullptr;
		QComboBox *waveform = nullptr;
		QDial *freq_mult = nullptr, *out_level = nullptr, *ksl = nullptr;
		QCheckBox *tremolo = nullptr, *vibrato = nullptr, *sustain_mode = nullptr, *ksr = nullptr;
	};

	// Public for use by static helper functions
	struct MidiTab {
		int midi_ch = 0;
		QWidget *content = nullptr;
		QGroupBox *op3_group = nullptr;
		QGroupBox *op4_group = nullptr;
		QCheckBox *four_op_cb = nullptr;
		FMDiagramWidget *fm_diagram = nullptr;
		QComboBox *alg_combo = nullptr;

		// Operator widget references (indexed by nrpn_msb: 0=OP1, 1=OP2, 2=OP3, 3=OP4)
		OperatorWidgets op_widgets[4];
		QDial *feedback_dial = nullptr;
		QCheckBox *pan_left_cb = nullptr;
		QCheckBox *pan_right_cb = nullptr;

		// Instrument browser
		QComboBox *bank_combo = nullptr;
		QListWidget *inst_list = nullptr;
		QCheckBox *show_blank_cb = nullptr;

		// Routing
		QPushButton *route_btns[18] = {};
		QSpinBox *unison_spin = nullptr;
		QDial *detune_dial = nullptr;
		QCheckBox *pan_split_cb = nullptr;
		QLabel *poly_label = nullptr;
		QLabel *detune_label = nullptr;
	};

private slots:
	void on_start_stop_clicked();
	void on_serial_refresh_clicked();
	void on_midi_refresh_clicked();
	void on_flush_timer();

private:
	// Hardware chain
	retrowave::QtSerialPort serial_;
	retrowave::OPL3HardwareBuffer hw_buf_{serial_};
	retrowave::OPL3State opl3_state_{hw_buf_};
	retrowave::DirectMode direct_mode_{opl3_state_};
	retrowave::VoiceAllocator voice_alloc_{direct_mode_, opl3_state_};

	// MIDI
	RtMidiIn *midiin_ = nullptr;
	bool running_ = false;
	QTimer *flush_timer_ = nullptr;

	// Send NRPN to all OPL3 channels assigned to a MIDI channel.
	void send_nrpn_to_midi_ch(uint8_t midi_ch, uint8_t msb, uint8_t lsb, uint8_t value);

	static void midi_callback(double ts, std::vector<unsigned char> *msg, void *user);

	// UI
	void build_ui();
	QWidget *build_midi_channel_tab(int midi_ch);
	QGroupBox *build_operator_group(const QString &title, uint8_t midi_ch, uint8_t nrpn_msb,
	    int def_attack, int def_decay, int def_sustain, int def_release,
	    int def_waveform, int def_freqmult, int def_outlevel, int def_ksl,
	    bool def_tremolo, bool def_vibrato, bool def_sustainmode, bool def_ksr);
	void on_four_op_toggled(int midi_ch, bool enabled);
	void on_perc_mode_toggled(bool enabled);
	void on_drum_routing_changed(int drum_idx, int midi_ch);
	void on_routing_changed(int midi_ch);
	void on_unison_changed(int midi_ch, int unison);
	void on_detune_changed(int midi_ch, int cents);
	void on_algorithm_changed(int midi_ch, int alg_index);

	void refresh_serial_ports();
	void refresh_midi_ports();
	void update_poly_label(int midi_ch);
	void apply_voice_config(int midi_ch);
	void refresh_all_button_states();

	// Top bar widgets
	QComboBox *cb_serial_ = nullptr;
	QComboBox *cb_midi_ = nullptr;
	QPushButton *btn_start_ = nullptr;

	// MIDI channel tabs
	QTabWidget *tab_widget_ = nullptr;
	MidiTab midi_tabs_[16];

	// Percussion routing
	QGroupBox *perc_group_ = nullptr;
	QComboBox *drum_ch_combos_[5] = {};  // BD, SD, TT, CY, HH

	// Bank browser
	struct LoadedBank {
		QString name;
		QString path;
		WOPLFile *wopl = nullptr;
	};
	std::vector<LoadedBank> loaded_banks_;

	void scan_bank_directory();
	void load_bank_file(const QString &path);
	void populate_bank_combos();
	void on_bank_selected(int midi_ch, int bank_index);
	void on_instrument_apply(int midi_ch);
	void apply_instrument(int midi_ch, const WOPLInstrument &inst);
	void filter_instrument_list(int midi_ch);
};
