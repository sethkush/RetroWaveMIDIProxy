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

#include "panelwindow.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QMessageBox>
#include <QStatusBar>
#include <QApplication>
#include <QScrollArea>
#include <QFrame>
#include <QFileDialog>
#include <QDir>
#include <QFileInfo>

// Standard General MIDI program names (fallback when WOPL inst_name is blank)
static const char *kGMNames[128] = {
	"Acoustic Grand Piano", "Bright Acoustic Piano", "Electric Grand Piano",
	"Honky-tonk Piano", "Electric Piano 1", "Electric Piano 2", "Harpsichord",
	"Clavi", "Celesta", "Glockenspiel", "Music Box", "Vibraphone", "Marimba",
	"Xylophone", "Tubular Bells", "Dulcimer", "Drawbar Organ", "Percussive Organ",
	"Rock Organ", "Church Organ", "Reed Organ", "Accordion", "Harmonica",
	"Tango Accordion", "Acoustic Guitar (nylon)", "Acoustic Guitar (steel)",
	"Electric Guitar (jazz)", "Electric Guitar (clean)", "Electric Guitar (muted)",
	"Overdriven Guitar", "Distortion Guitar", "Guitar Harmonics",
	"Acoustic Bass", "Electric Bass (finger)", "Electric Bass (pick)",
	"Fretless Bass", "Slap Bass 1", "Slap Bass 2", "Synth Bass 1", "Synth Bass 2",
	"Violin", "Viola", "Cello", "Contrabass", "Tremolo Strings",
	"Pizzicato Strings", "Orchestral Harp", "Timpani", "String Ensemble 1",
	"String Ensemble 2", "Synth Strings 1", "Synth Strings 2", "Choir Aahs",
	"Voice Oohs", "Synth Voice", "Orchestra Hit", "Trumpet", "Trombone", "Tuba",
	"Muted Trumpet", "French Horn", "Brass Section", "Synth Brass 1",
	"Synth Brass 2", "Soprano Sax", "Alto Sax", "Tenor Sax", "Baritone Sax",
	"Oboe", "English Horn", "Bassoon", "Clarinet", "Piccolo", "Flute", "Recorder",
	"Pan Flute", "Blown Bottle", "Shakuhachi", "Whistle", "Ocarina",
	"Lead 1 (square)", "Lead 2 (sawtooth)", "Lead 3 (calliope)",
	"Lead 4 (chiff)", "Lead 5 (charang)", "Lead 6 (voice)",
	"Lead 7 (fifths)", "Lead 8 (bass + lead)",
	"Pad 1 (new age)", "Pad 2 (warm)", "Pad 3 (polysynth)", "Pad 4 (choir)",
	"Pad 5 (bowed)", "Pad 6 (metallic)", "Pad 7 (halo)", "Pad 8 (sweep)",
	"FX 1 (rain)", "FX 2 (soundtrack)", "FX 3 (crystal)", "FX 4 (atmosphere)",
	"FX 5 (brightness)", "FX 6 (goblins)", "FX 7 (echoes)", "FX 8 (sci-fi)",
	"Sitar", "Banjo", "Shamisen", "Koto", "Kalimba", "Bag pipe", "Fiddle",
	"Shanai", "Tinkle Bell", "Agogo", "Steel Drums", "Woodblock",
	"Taiko Drum", "Melodic Tom", "Synth Drum", "Reverse Cymbal",
	"Guitar Fret Noise", "Breath Noise", "Seashore", "Bird Tweet",
	"Telephone Ring", "Helicopter", "Applause", "Gunshot"
};

// OPL3 channel name for button labels
static QString opl3_ch_name(int ch)
{
	int port = ch / 9;
	int local = ch % 9 + 1;
	return "P" + QString::number(port) + ":" + QString::number(local);
}

PanelWindow::PanelWindow(QWidget *parent)
	: QMainWindow(parent)
{
	setWindowTitle("RetroWave OPL3 Panel");
	resize(1000, 780);

	build_ui();

	try {
		midiin_ = new RtMidiIn(RtMidi::UNSPECIFIED, "RetroWave OPL3 Panel", 1024);
		midiin_->setCallback(&midi_callback, this);
	} catch (RtMidiError &e) {
		QMessageBox::warning(this, "Error", QString::fromStdString(e.getMessage()));
	}

	refresh_serial_ports();
	refresh_midi_ports();
	scan_bank_directory();

	statusBar()->showMessage("Ready");
}

PanelWindow::~PanelWindow()
{
	if (running_)
		on_start_stop_clicked();
	delete midiin_;
	for (auto &bank : loaded_banks_)
		if (bank.wopl)
			WOPL_Free(bank.wopl);
}

// --- UI helpers ---

static QWidget *make_dial_cell(const QString &label, int min, int max, int def,
                                QDial *&dial_out, QLabel *&label_out)
{
	auto *w = new QWidget;
	auto *vb = new QVBoxLayout(w);
	vb->setContentsMargins(4, 2, 4, 2);
	vb->setSpacing(2);

	dial_out = new QDial;
	dial_out->setMinimum(min);
	dial_out->setMaximum(max);
	dial_out->setValue(def);
	dial_out->setNotchesVisible(true);
	dial_out->setFixedSize(56, 56);

	label_out = new QLabel(label + ": " + QString::number(def));
	label_out->setAlignment(Qt::AlignCenter);

	vb->addWidget(dial_out, 0, Qt::AlignCenter);
	vb->addWidget(label_out, 0, Qt::AlignCenter);

	return w;
}

// OPL3 frequency multiplier lookup table
static const char *kFreqMultRatio[] = {
	"\302\275", "1", "2", "3", "4", "5", "6", "7",
	"8", "9", "10", "10", "12", "12", "15", "15"
};

static QString format_freqmult_label(int val)
{
	return "Mult: " + QString::number(val) + " (" +
	       QString::fromUtf8(kFreqMultRatio[val & 15]) + "\303\227)";
}

static QString format_outlevel_label(int val)
{
	double db = -(val * 0.75);
	return "Level: " + QString::number(val) + " (" +
	       QString::number(db, 'f', 1) + "dB)";
}

QGroupBox *PanelWindow::build_operator_group(
    const QString &title, uint8_t midi_ch, uint8_t nrpn_msb,
    int def_attack, int def_decay, int def_sustain, int def_release,
    int def_waveform, int def_freqmult, int def_outlevel, int def_ksl,
    bool def_tremolo, bool def_vibrato, bool def_sustainmode, bool def_ksr)
{
	auto *group = new QGroupBox(title);
	group->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
	auto *grid = new QGridLayout(group);
	grid->setVerticalSpacing(8);
	grid->setRowStretch(1, 1);
	grid->setRowStretch(3, 1);

	// Row 0: ADSR dials
	QDial *dAttack, *dDecay, *dSustain, *dRelease;
	QLabel *lAttack, *lDecay, *lSustain, *lRelease;

	grid->addWidget(make_dial_cell("Atk", 0, 15, def_attack, dAttack, lAttack), 0, 0);
	grid->addWidget(make_dial_cell("Dec", 0, 15, def_decay, dDecay, lDecay), 0, 1);
	grid->addWidget(make_dial_cell("Sus", 0, 15, def_sustain, dSustain, lSustain), 0, 2);
	grid->addWidget(make_dial_cell("Rel", 0, 15, def_release, dRelease, lRelease), 0, 3);

	// Row 0 col 4: Envelope diagram to the right of ADSR dials
	auto *envelope = new EnvelopeWidget;
	envelope->setAttack(def_attack);
	envelope->setDecay(def_decay);
	envelope->setSustain(def_sustain);
	envelope->setRelease(def_release);
	grid->addWidget(envelope, 0, 4);

	// Row 1: Waveform combo, FreqMult, OutLevel, KSL
	QDial *dFreqMult, *dOutLevel, *dKSL;
	QLabel *lFreqMult, *lOutLevel, *lKSL;

	// Waveform cell: combo + label on left, diagram top-aligned on right
	auto *wfWidget = new QWidget;
	auto *wfHb = new QHBoxLayout(wfWidget);
	wfHb->setContentsMargins(4, 2, 4, 2);
	wfHb->setSpacing(4);
	auto *wfLeftCol = new QVBoxLayout;
	wfLeftCol->setSpacing(1);
	auto *cbWaveform = new QComboBox;
	cbWaveform->addItems({"Sine", "Half-Sine", "Abs-Sine", "Pulse-Sine",
	                       "Sine (Even)", "Abs-Sine (Even)", "Square", "Derived Sq"});
	cbWaveform->setCurrentIndex(def_waveform);
	auto *lWaveform = new QLabel("Wave");
	lWaveform->setAlignment(Qt::AlignCenter);
	wfLeftCol->addStretch();
	wfLeftCol->addWidget(cbWaveform);
	wfLeftCol->addWidget(lWaveform, 0, Qt::AlignCenter);
	wfLeftCol->addStretch();
	auto *wfDiagram = new WaveformWidget;
	wfDiagram->setWaveform(def_waveform);
	wfHb->addLayout(wfLeftCol);
	wfHb->addWidget(wfDiagram, 0, Qt::AlignVCenter);

	grid->addWidget(wfWidget, 2, 0, 1, 2);
	grid->addWidget(make_dial_cell("Mult", 0, 15, def_freqmult, dFreqMult, lFreqMult), 2, 2);
	grid->addWidget(make_dial_cell("Level", 0, 63, def_outlevel, dOutLevel, lOutLevel), 2, 3);

	// KSL cell: dial + label on left, curve top-aligned on right
	auto *kslWidget = new QWidget;
	auto *kslHb = new QHBoxLayout(kslWidget);
	kslHb->setContentsMargins(4, 2, 4, 2);
	kslHb->setSpacing(4);
	auto *kslLeftCol = new QVBoxLayout;
	kslLeftCol->setSpacing(2);
	dKSL = new QDial;
	dKSL->setMinimum(0);
	dKSL->setMaximum(3);
	dKSL->setValue(def_ksl);
	dKSL->setNotchesVisible(true);
	dKSL->setFixedSize(56, 56);
	lKSL = new QLabel("KSL: " + QString::number(def_ksl));
	lKSL->setAlignment(Qt::AlignCenter);
	kslLeftCol->addWidget(dKSL, 0, Qt::AlignCenter);
	kslLeftCol->addWidget(lKSL, 0, Qt::AlignCenter);
	auto *kslCurve = new KSLCurveWidget;
	kslCurve->setKSL(def_ksl);
	kslHb->addLayout(kslLeftCol);
	kslHb->addWidget(kslCurve, 0, Qt::AlignVCenter);
	grid->addWidget(kslWidget, 2, 4);

	// Set initial enhanced label texts
	lFreqMult->setText(format_freqmult_label(def_freqmult));
	lOutLevel->setText(format_outlevel_label(def_outlevel));

	// Row 3: Checkboxes
	auto *cbTremolo = new QCheckBox("Tremolo");
	auto *cbVibrato = new QCheckBox("Vibrato");
	auto *cbSustainMode = new QCheckBox("Sustain");
	auto *cbKSR = new QCheckBox("KSR");

	cbTremolo->setChecked(def_tremolo);
	cbVibrato->setChecked(def_vibrato);
	cbSustainMode->setChecked(def_sustainmode);
	cbKSR->setChecked(def_ksr);

	auto *cbRow = new QHBoxLayout;
	cbRow->addStretch();
	cbRow->addWidget(cbTremolo);
	cbRow->addStretch();
	cbRow->addWidget(cbVibrato);
	cbRow->addStretch();
	cbRow->addWidget(cbSustainMode);
	cbRow->addStretch();
	cbRow->addWidget(cbKSR);
	cbRow->addStretch();
	auto *cbWidget = new QWidget;
	cbWidget->setLayout(cbRow);
	cbWidget->setContentsMargins(0, 0, 0, 4);
	grid->addWidget(cbWidget, 4, 0, 1, 5);

	// --- Tooltips ---
	dAttack->setToolTip("Attack rate (0 = slowest, 15 = fastest)\nHow quickly the sound reaches full volume");
	dDecay->setToolTip("Decay rate (0 = slowest, 15 = fastest)\nHow quickly the sound falls to the sustain level");
	dSustain->setToolTip("Sustain level (0 = loudest, 15 = quietest)\nVolume held while key is pressed.\nNote: higher values = quieter!");
	dRelease->setToolTip("Release rate (0 = slowest, 15 = fastest)\nHow quickly the sound fades after key release");
	cbWaveform->setToolTip("Operator waveform shape\n0: Sine  1: Half-Sine  2: Abs-Sine  3: Pulse-Sine\n4: Sine(Even)  5: Abs-Sine(Even)  6: Square  7: Derived Sq");
	dFreqMult->setToolTip("Frequency multiplier (harmonic ratio)\n0=\302\275\303\227  1=1\303\227  2=2\303\227 ... 15=15\303\227\nSets the harmonic partial for this operator");
	dOutLevel->setToolTip("Output level in 0.75 dB steps\n0 = 0 dB (loudest), 63 = -47.25 dB (quietest)\nFor carriers: controls note volume\nFor modulators: controls modulation depth");
	dKSL->setToolTip("Key Scale Level — attenuation per octave\n0: Off  1: 1.5 dB/oct  2: 3.0 dB/oct  3: 6.0 dB/oct\nHigher notes get quieter, simulating natural instruments");
	cbTremolo->setToolTip("Amplitude modulation by the LFO\nAdds a trembling volume effect");
	cbVibrato->setToolTip("Frequency modulation by the LFO\nAdds a vibrato pitch wobble");
	cbSustainMode->setToolTip("When ON: sound holds at sustain level until key release\nWhen OFF: sound decays through sustain to silence");
	cbKSR->setToolTip("Key Scale Rate — higher notes have faster envelopes\nWhen ON: envelope rates scale with pitch");

	// --- Signal connections (manual lambdas for ADSR → envelope, FreqMult → ratio, OutLevel → dB, KSL → curve) ---

	// ADSR: update label + envelope widget + send NRPN
	connect(dAttack, &QDial::valueChanged, this, [=](int val) {
		lAttack->setText("Atk: " + QString::number(val));
		envelope->setAttack(val);
		send_nrpn_to_midi_ch(midi_ch, nrpn_msb, 0, static_cast<uint8_t>(val << 3));
	});
	connect(dDecay, &QDial::valueChanged, this, [=](int val) {
		lDecay->setText("Dec: " + QString::number(val));
		envelope->setDecay(val);
		send_nrpn_to_midi_ch(midi_ch, nrpn_msb, 1, static_cast<uint8_t>(val << 3));
	});
	connect(dSustain, &QDial::valueChanged, this, [=](int val) {
		lSustain->setText("Sus: " + QString::number(val));
		envelope->setSustain(val);
		send_nrpn_to_midi_ch(midi_ch, nrpn_msb, 2, static_cast<uint8_t>(val << 3));
	});
	connect(dRelease, &QDial::valueChanged, this, [=](int val) {
		lRelease->setText("Rel: " + QString::number(val));
		envelope->setRelease(val);
		send_nrpn_to_midi_ch(midi_ch, nrpn_msb, 3, static_cast<uint8_t>(val << 3));
	});

	// FreqMult: update label with harmonic ratio
	connect(dFreqMult, &QDial::valueChanged, this, [=](int val) {
		lFreqMult->setText(format_freqmult_label(val));
		send_nrpn_to_midi_ch(midi_ch, nrpn_msb, 5, static_cast<uint8_t>(val << 3));
	});

	// OutLevel: update label with dB readout
	connect(dOutLevel, &QDial::valueChanged, this, [=](int val) {
		lOutLevel->setText(format_outlevel_label(val));
		send_nrpn_to_midi_ch(midi_ch, nrpn_msb, 6, static_cast<uint8_t>(val << 1));
	});

	// KSL: update label + curve widget
	connect(dKSL, &QDial::valueChanged, this, [=](int val) {
		lKSL->setText("KSL: " + QString::number(val));
		kslCurve->setKSL(val);
		send_nrpn_to_midi_ch(midi_ch, nrpn_msb, 7, static_cast<uint8_t>(val << 5));
	});

	// Waveform: NRPN LSB 4, scale val << 4
	connect(cbWaveform, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
	        [this, midi_ch, nrpn_msb, wfDiagram](int val) {
		wfDiagram->setWaveform(val);
		send_nrpn_to_midi_ch(midi_ch, nrpn_msb, 4, static_cast<uint8_t>(val << 4));
	});

	// Checkboxes: NRPN LSB 8-11, value 0 or 127
	auto connect_cb = [this, midi_ch](QCheckBox *cb, uint8_t msb, uint8_t lsb) {
		connect(cb, &QCheckBox::toggled, this, [=](bool on) {
			send_nrpn_to_midi_ch(midi_ch, msb, lsb, on ? 127 : 0);
		});
	};

	connect_cb(cbTremolo,     nrpn_msb, 8);
	connect_cb(cbVibrato,     nrpn_msb, 9);
	connect_cb(cbSustainMode, nrpn_msb, 10);
	connect_cb(cbKSR,         nrpn_msb, 11);

	// Store widget pointers for programmatic access (instrument browser)
	auto &ow = midi_tabs_[midi_ch].op_widgets[nrpn_msb];
	ow.attack = dAttack;
	ow.decay = dDecay;
	ow.sustain = dSustain;
	ow.release = dRelease;
	ow.waveform = cbWaveform;
	ow.freq_mult = dFreqMult;
	ow.out_level = dOutLevel;
	ow.ksl = dKSL;
	ow.tremolo = cbTremolo;
	ow.vibrato = cbVibrato;
	ow.sustain_mode = cbSustainMode;
	ow.ksr = cbKSR;

	return group;
}

QWidget *PanelWindow::build_midi_channel_tab(int midi_ch)
{
	auto ch = static_cast<uint8_t>(midi_ch);
	auto *page = new QWidget;
	auto *layout = new QVBoxLayout(page);
	layout->setSpacing(6);

	// === Routing Section ===
	auto *routeGroup = new QGroupBox("Routing");
	auto *routeLayout = new QVBoxLayout(routeGroup);

	// OPL3 channel toggle buttons (2 rows of 9)
	auto *btnRow1 = new QHBoxLayout;
	auto *btnRow2 = new QHBoxLayout;

	for (int i = 0; i < 18; ++i) {
		auto *btn = new QPushButton(opl3_ch_name(i));
		btn->setCheckable(true);
		btn->setFixedSize(48, 26);
		btn->setStyleSheet("QPushButton:checked { background-color: #4a90d9; color: white; }");

		// Default mapping: MIDI ch N → OPL3 ch N
		if (i == midi_ch)
			btn->setChecked(true);

		midi_tabs_[midi_ch].route_btns[i] = btn;

		connect(btn, &QPushButton::toggled, this, [this, midi_ch](bool) {
			on_routing_changed(midi_ch);
		});

		if (i < 9)
			btnRow1->addWidget(btn);
		else
			btnRow2->addWidget(btn);
	}

	routeLayout->addLayout(btnRow1);
	routeLayout->addLayout(btnRow2);

	// Unison / Detune / Poly row
	auto *uniRow = new QHBoxLayout;

	uniRow->addWidget(new QLabel("Unison:"));
	auto *uniSpin = new QSpinBox;
	uniSpin->setMinimum(1);
	uniSpin->setMaximum(18);
	uniSpin->setValue(1);
	uniSpin->setFixedWidth(50);
	midi_tabs_[midi_ch].unison_spin = uniSpin;
	uniRow->addWidget(uniSpin);

	connect(uniSpin, QOverload<int>::of(&QSpinBox::valueChanged), this,
	        [this, midi_ch](int val) { on_unison_changed(midi_ch, val); });

	uniRow->addSpacing(12);

	QDial *detuneDial;
	QLabel *detuneLabel;
	uniRow->addWidget(make_dial_cell("Detune", 0, 100, 10, detuneDial, detuneLabel));
	midi_tabs_[midi_ch].detune_dial = detuneDial;
	midi_tabs_[midi_ch].detune_label = detuneLabel;
	detuneDial->setFixedSize(40, 40);

	connect(detuneDial, &QDial::valueChanged, this, [this, midi_ch, detuneLabel](int val) {
		detuneLabel->setText("Detune: " + QString::number(val));
		on_detune_changed(midi_ch, val);
	});

	uniRow->addSpacing(12);

	auto *panSplitCb = new QCheckBox("Stereo Split");
	panSplitCb->setToolTip("Spread unison voices across the stereo field");
	midi_tabs_[midi_ch].pan_split_cb = panSplitCb;
	uniRow->addWidget(panSplitCb);

	connect(panSplitCb, &QCheckBox::toggled, this, [this, midi_ch](bool on) {
		retrowave::VoiceConfig config = voice_alloc_.voice_config(static_cast<uint8_t>(midi_ch));
		config.pan_split = on;
		if (running_) {
			std::lock_guard<std::mutex> lg(hw_buf_.mutex());
			voice_alloc_.set_voice_config(static_cast<uint8_t>(midi_ch), config);
		} else {
			voice_alloc_.set_voice_config(static_cast<uint8_t>(midi_ch), config);
		}
	});

	uniRow->addSpacing(12);

	auto *polyLabel = new QLabel("Poly: 1 voice");
	polyLabel->setStyleSheet("font-weight: bold;");
	midi_tabs_[midi_ch].poly_label = polyLabel;
	uniRow->addWidget(polyLabel);
	uniRow->addStretch();

	routeLayout->addLayout(uniRow);
	layout->addWidget(routeGroup);

	// === Main content: FM Diagram + Operators ===
	auto *contentRow = new QHBoxLayout;

	// --- Left column: FM Diagram + Channel controls ---
	auto *leftCol = new QVBoxLayout;

	auto *fmDiagram = new FMDiagramWidget;
	midi_tabs_[midi_ch].fm_diagram = fmDiagram;
	leftCol->addWidget(fmDiagram);

	// Channel controls group
	auto *chGroup = new QGroupBox("Channel");
	auto *chGrid = new QGridLayout(chGroup);

	QDial *dFeedback;
	QLabel *lFeedback;
	chGrid->addWidget(make_dial_cell("FB", 0, 7, 4, dFeedback, lFeedback), 0, 0);
	midi_tabs_[midi_ch].feedback_dial = dFeedback;

	connect(dFeedback, &QDial::valueChanged, this, [=](int val) {
		lFeedback->setText("FB: " + QString::number(val));
		fmDiagram->setFeedback(static_cast<uint8_t>(val));
		send_nrpn_to_midi_ch(ch, 4, 0, static_cast<uint8_t>(val << 4));
	});

	// Algorithm selector (replaces bare AM checkbox)
	auto *algCombo = new QComboBox;
	algCombo->addItem("FM: OP1 \342\206\222 OP2 \342\206\222 Out");          // 2-op FM
	algCombo->addItem("AM: OP1 + OP2 \342\206\222 Out");                     // 2-op AM
	algCombo->setCurrentIndex(0);
	midi_tabs_[midi_ch].alg_combo = algCombo;
	chGrid->addWidget(new QLabel("Algorithm:"), 0, 1);
	chGrid->addWidget(algCombo, 1, 0, 1, 2);

	connect(algCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
	        [this, midi_ch](int idx) { on_algorithm_changed(midi_ch, idx); });

	auto *cbPanL = new QCheckBox("Pan Left");
	cbPanL->setChecked(true);
	chGrid->addWidget(cbPanL, 2, 0);
	midi_tabs_[midi_ch].pan_left_cb = cbPanL;

	auto *cbPanR = new QCheckBox("Pan Right");
	cbPanR->setChecked(true);
	chGrid->addWidget(cbPanR, 2, 1);
	midi_tabs_[midi_ch].pan_right_cb = cbPanR;

	auto connect_pan = [this, ch](QCheckBox *cb, uint8_t lsb) {
		connect(cb, &QCheckBox::toggled, this, [=](bool on) {
			send_nrpn_to_midi_ch(ch, 4, lsb, on ? 127 : 0);
		});
	};
	connect_pan(cbPanL, 2);
	connect_pan(cbPanR, 3);

	// 4-Op Enable
	auto *fourOpCb = new QCheckBox("4-Op Enable");
	fourOpCb->setChecked(false);
	chGrid->addWidget(fourOpCb, 3, 0, 1, 2);
	midi_tabs_[midi_ch].four_op_cb = fourOpCb;

	connect(fourOpCb, &QCheckBox::toggled, this, [this, midi_ch](bool on) {
		on_four_op_toggled(midi_ch, on);
	});

	leftCol->addWidget(chGroup);

	// --- Instrument Browser ---
	auto *instGroup = new QGroupBox("Instruments");
	auto *instLayout = new QVBoxLayout(instGroup);
	instLayout->setSpacing(4);

	auto *bankRow = new QHBoxLayout;
	bankRow->addWidget(new QLabel("Bank:"));
	auto *bankCombo = new QComboBox;
	bankCombo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
	bankRow->addWidget(bankCombo);
	instLayout->addLayout(bankRow);
	midi_tabs_[midi_ch].bank_combo = bankCombo;

	auto *loadBtn = new QPushButton("Load File...");
	instLayout->addWidget(loadBtn);

	auto *instList = new QListWidget;
	instList->setMinimumHeight(120);
	instLayout->addWidget(instList, 1);
	midi_tabs_[midi_ch].inst_list = instList;

	auto *instBottomRow = new QHBoxLayout;
	auto *showBlankCb = new QCheckBox("Show blank");
	instBottomRow->addWidget(showBlankCb);
	midi_tabs_[midi_ch].show_blank_cb = showBlankCb;
	instBottomRow->addStretch();
	auto *applyBtn = new QPushButton("Apply");
	instBottomRow->addWidget(applyBtn);
	instLayout->addLayout(instBottomRow);

	connect(bankCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
	        [this, midi_ch](int idx) { on_bank_selected(midi_ch, idx); });

	connect(loadBtn, &QPushButton::clicked, this, [this, midi_ch]() {
		QString path = QFileDialog::getOpenFileName(this, "Load WOPL Bank File",
		    QString(), "WOPL Bank Files (*.wopl);;All Files (*)");
		if (!path.isEmpty()) {
			load_bank_file(path);
			populate_bank_combos();
			// Select the newly loaded bank
			auto &tab = midi_tabs_[midi_ch];
			if (tab.bank_combo && !loaded_banks_.empty())
				tab.bank_combo->setCurrentIndex(static_cast<int>(loaded_banks_.size()) - 1);
		}
	});

	connect(showBlankCb, &QCheckBox::toggled, this, [this, midi_ch](bool) {
		filter_instrument_list(midi_ch);
	});

	connect(applyBtn, &QPushButton::clicked, this, [this, midi_ch]() {
		on_instrument_apply(midi_ch);
	});

	leftCol->addWidget(instGroup, 1);

	auto *leftPane = new QWidget;
	leftPane->setFixedWidth(280);
	leftPane->setLayout(leftCol);
	contentRow->addWidget(leftPane);

	// --- Right column: Operator groups ---
	auto *rightCol = new QVBoxLayout;

	// OP1 + OP2 row
	auto *opRow12 = new QHBoxLayout;
	opRow12->addWidget(build_operator_group("OP1 (Modulator)", ch, 0,
	    15, 4, 2, 4, 0, 1, 32, 0,
	    false, false, true, false));
	opRow12->addWidget(build_operator_group("OP2 (Carrier)", ch, 1,
	    15, 4, 2, 6, 0, 1, 0, 0,
	    false, false, true, false));
	rightCol->addLayout(opRow12);

	// OP3 + OP4 row (hidden by default)
	auto *opRow34 = new QHBoxLayout;
	auto *op3 = build_operator_group("OP3 (Modulator 2)", ch, 2,
	    15, 4, 2, 4, 0, 1, 32, 0,
	    false, false, true, false);
	auto *op4 = build_operator_group("OP4 (Carrier 2)", ch, 3,
	    15, 4, 2, 6, 0, 1, 0, 0,
	    false, false, true, false);
	op3->setVisible(false);
	op4->setVisible(false);
	opRow34->addWidget(op3);
	opRow34->addWidget(op4);
	rightCol->addLayout(opRow34);

	contentRow->addLayout(rightCol, 1);
	layout->addLayout(contentRow, 1);

	// Store references
	midi_tabs_[midi_ch].midi_ch = midi_ch;
	midi_tabs_[midi_ch].content = page;
	midi_tabs_[midi_ch].op3_group = op3;
	midi_tabs_[midi_ch].op4_group = op4;

	return page;
}

void PanelWindow::build_ui()
{
	auto *central = new QWidget;
	setCentralWidget(central);
	auto *mainLayout = new QVBoxLayout(central);

	// --- Top bar ---
	auto *topBar = new QHBoxLayout;

	topBar->addWidget(new QLabel("Serial:"));
	cb_serial_ = new QComboBox;
	cb_serial_->setMinimumWidth(180);
	topBar->addWidget(cb_serial_);
	auto *btnSerialRefresh = new QPushButton("Refresh");
	connect(btnSerialRefresh, &QPushButton::clicked, this, &PanelWindow::on_serial_refresh_clicked);
	topBar->addWidget(btnSerialRefresh);

	topBar->addSpacing(12);

	topBar->addWidget(new QLabel("MIDI In:"));
	cb_midi_ = new QComboBox;
	cb_midi_->setMinimumWidth(180);
	topBar->addWidget(cb_midi_);
	auto *btnMidiRefresh = new QPushButton("Refresh");
	connect(btnMidiRefresh, &QPushButton::clicked, this, &PanelWindow::on_midi_refresh_clicked);
	topBar->addWidget(btnMidiRefresh);

	topBar->addSpacing(12);

	btn_start_ = new QPushButton("Start");
	connect(btn_start_, &QPushButton::clicked, this, &PanelWindow::on_start_stop_clicked);
	topBar->addWidget(btn_start_);

	topBar->addStretch();
	mainLayout->addLayout(topBar);

	// --- Global group ---
	auto *glGroup = new QGroupBox("Global");
	auto *glLayout = new QHBoxLayout(glGroup);

	auto *cbTremDepth = new QCheckBox("Tremolo Depth");
	auto *cbVibDepth = new QCheckBox("Vibrato Depth");
	auto *cbPercMode = new QCheckBox("Percussion Mode");
	glLayout->addWidget(cbTremDepth);
	glLayout->addWidget(cbVibDepth);
	glLayout->addWidget(cbPercMode);
	glLayout->addStretch();

	// Global NRPNs use any OPL3 channel (channel 0 works since they're global regs)
	connect(cbTremDepth, &QCheckBox::toggled, this, [this](bool on) {
		send_nrpn_to_midi_ch(0, 5, 0, on ? 127 : 0);
	});
	connect(cbVibDepth, &QCheckBox::toggled, this, [this](bool on) {
		send_nrpn_to_midi_ch(0, 5, 1, on ? 127 : 0);
	});
	connect(cbPercMode, &QCheckBox::toggled, this, [this](bool on) {
		on_perc_mode_toggled(on);
	});

	mainLayout->addWidget(glGroup);

	// --- Percussion routing group (hidden until perc mode enabled) ---
	perc_group_ = new QGroupBox("Percussion Routing");
	auto *percLayout = new QGridLayout(perc_group_);

	static const char *kDrumNames[5] = {"Bass Drum", "Snare", "Tom-Tom", "Cymbal", "Hi-Hat"};
	static const char *kDrumFreqNote[5] = {"(Ch 6)", "(Ch 7)", "(Ch 8)", "(Ch 8)", "(Ch 7)"};

	for (int d = 0; d < 5; ++d) {
		auto *label = new QLabel(QString("%1 %2").arg(kDrumNames[d]).arg(kDrumFreqNote[d]));
		percLayout->addWidget(label, d, 0);

		auto *combo = new QComboBox;
		combo->addItem("Off", -1);
		for (int ch = 0; ch < 16; ++ch)
			combo->addItem("MIDI " + QString::number(ch + 1), ch);
		combo->setCurrentIndex(0);
		drum_ch_combos_[d] = combo;
		percLayout->addWidget(combo, d, 1);

		connect(combo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
		        [this, d](int idx) {
			int midi_ch = (idx == 0) ? -1 : idx - 1;
			on_drum_routing_changed(d, midi_ch);
		});
	}

	perc_group_->setVisible(false);
	mainLayout->addWidget(perc_group_);

	// --- MIDI channel tabs ---
	tab_widget_ = new QTabWidget;
	for (int i = 0; i < 16; ++i) {
		auto *page = build_midi_channel_tab(i);
		tab_widget_->addTab(page, "MIDI " + QString::number(i + 1));
	}
	mainLayout->addWidget(tab_widget_);
}

// --- Algorithm changed ---

void PanelWindow::on_algorithm_changed(int midi_ch, int alg_index)
{
	auto &tab = midi_tabs_[midi_ch];
	auto ch = static_cast<uint8_t>(midi_ch);

	if (tab.four_op_cb && tab.four_op_cb->isChecked()) {
		// 4-op mode: alg_index selects from 4 algorithms
		// Map index to conn1, conn2 bits
		uint8_t conn1 = 0, conn2 = 0;
		switch (alg_index) {
		case 0: conn1 = 0; conn2 = 0; break; // serial chain
		case 1: conn1 = 1; conn2 = 0; break; // parallel first pair
		case 2: conn1 = 0; conn2 = 1; break; // parallel FM pairs
		case 3: conn1 = 1; conn2 = 1; break; // 3 additive
		}
		tab.fm_diagram->setFourOpAlgorithm(conn1, conn2);

		// Write connection bits to both channel pairs
		send_nrpn_to_midi_ch(ch, 4, 1, conn1 ? 127 : 0);
		// For the paired channel's connection bit, we need to write to the
		// secondary channel in the 4-op pair. Since we're broadcasting,
		// we use a second NRPN with the paired channel's connection.
		// The nrpn_channel handler writes conn bit to C0+opl_ch.
		// For 4-op, the second connection bit is on the paired channel.
		// We handle this by also applying conn2 to OP3/OP4's parent channel.
		// This requires knowing which OPL3 channels are paired. For now,
		// we write it via the 4-op specific NRPN (param 5).
		send_nrpn_to_midi_ch(ch, 4, 5, conn2 ? 127 : 0);
	} else {
		// 2-op mode: index 0 = FM, index 1 = AM
		bool am = (alg_index == 1);
		tab.fm_diagram->setConnection(am);
		send_nrpn_to_midi_ch(ch, 4, 1, am ? 127 : 0);
	}
}

// --- 4-Op toggle ---

void PanelWindow::on_four_op_toggled(int midi_ch, bool enabled)
{
	auto &tab = midi_tabs_[midi_ch];

	if (enabled) {
		if (tab.op3_group) tab.op3_group->setVisible(true);
		if (tab.op4_group) tab.op4_group->setVisible(true);
		tab.fm_diagram->setFourOp(true);

		// Switch algorithm combo to 4-op algorithms
		if (tab.alg_combo) {
			tab.alg_combo->blockSignals(true);
			tab.alg_combo->clear();
			tab.alg_combo->addItem("1: OP1\342\206\222OP2\342\206\222OP3\342\206\222OP4\342\206\222Out");
			tab.alg_combo->addItem("2: (OP1+OP2)\342\206\222OP3\342\206\222OP4\342\206\222Out");
			tab.alg_combo->addItem("3: OP1\342\206\222OP2 + OP3\342\206\222OP4\342\206\222Out");
			tab.alg_combo->addItem("4: OP1 + OP2 + OP3\342\206\222OP4\342\206\222Out");
			tab.alg_combo->setCurrentIndex(0);
			tab.alg_combo->blockSignals(false);
		}
	} else {
		if (tab.op3_group) tab.op3_group->setVisible(false);
		if (tab.op4_group) tab.op4_group->setVisible(false);
		tab.fm_diagram->setFourOp(false);

		// Switch algorithm combo back to 2-op
		if (tab.alg_combo) {
			tab.alg_combo->blockSignals(true);
			tab.alg_combo->clear();
			tab.alg_combo->addItem("FM: OP1 \342\206\222 OP2 \342\206\222 Out");
			tab.alg_combo->addItem("AM: OP1 + OP2 \342\206\222 Out");
			tab.alg_combo->setCurrentIndex(0);
			tab.alg_combo->blockSignals(false);
		}
	}

	// Uncheck non-pairable channels when entering 4-op mode
	if (enabled) {
		for (int ch = 0; ch < 18; ++ch) {
			if (retrowave::opl3::four_op_partner(ch) < 0) {
				auto *btn = tab.route_btns[ch];
				if (btn) {
					btn->blockSignals(true);
					btn->setChecked(false);
					btn->blockSignals(false);
				}
			}
		}
	}

	// Rebuild routing (handles 4-op pairing, exclusion, button states)
	on_routing_changed(midi_ch);

	// Send 4-op enable NRPN to all assigned OPL3 channels
	send_nrpn_to_midi_ch(static_cast<uint8_t>(midi_ch), 4, 4, enabled ? 127 : 0);

	// Trigger algorithm update
	on_algorithm_changed(midi_ch, tab.alg_combo ? tab.alg_combo->currentIndex() : 0);
}

// --- Routing changed ---

// Collect the checked route buttons for a tab, ignoring signals.
static std::vector<uint8_t> collect_checked(const PanelWindow::MidiTab &tab)
{
	std::vector<uint8_t> result;
	for (int i = 0; i < 18; ++i)
		if (tab.route_btns[i] && tab.route_btns[i]->isChecked())
			result.push_back(static_cast<uint8_t>(i));
	return result;
}

// Set a route button's checked state without emitting signals.
static void set_btn_checked(QPushButton *btn, bool checked)
{
	if (!btn) return;
	btn->blockSignals(true);
	btn->setChecked(checked);
	btn->blockSignals(false);
}

void PanelWindow::on_routing_changed(int midi_ch)
{
	using namespace retrowave::opl3;
	auto &tab = midi_tabs_[midi_ch];
	bool is_four_op = tab.four_op_cb && tab.four_op_cb->isChecked();

	// 1. Collect user-selected channels
	auto selected = collect_checked(tab);

	// 2. If 4-op, expand selection to include partners
	if (is_four_op) {
		std::vector<uint8_t> expanded = selected;
		for (uint8_t ch : selected) {
			int partner = four_op_partner(ch);
			if (partner >= 0) {
				bool found = false;
				for (uint8_t e : expanded)
					if (e == static_cast<uint8_t>(partner)) { found = true; break; }
				if (!found)
					expanded.push_back(static_cast<uint8_t>(partner));
			}
		}
		// Sync buttons: check auto-included partners
		for (uint8_t ch : expanded)
			set_btn_checked(tab.route_btns[ch], true);
		selected = expanded;
	}

	// 3. Exclusive routing: remove our channels from all other tabs.
	//    If the other tab has 4-op and we're stealing part of a pair,
	//    also steal the partner.
	for (int other = 0; other < 16; ++other) {
		if (other == midi_ch) continue;
		auto &other_tab = midi_tabs_[other];
		bool other_four_op = other_tab.four_op_cb && other_tab.four_op_cb->isChecked();
		bool other_changed = false;

		for (uint8_t ch : selected) {
			if (other_tab.route_btns[ch] && other_tab.route_btns[ch]->isChecked()) {
				set_btn_checked(other_tab.route_btns[ch], false);
				other_changed = true;

				// If the other tab has 4-op, also remove the partner
				if (other_four_op) {
					int partner = four_op_partner(ch);
					if (partner >= 0)
						set_btn_checked(other_tab.route_btns[partner], false);
				}
			}
		}
		if (other_changed)
			apply_voice_config(other);
	}

	// 4. Update button enabled/disabled states for all tabs
	refresh_all_button_states();

	// 5. Apply voice config for the changed tab
	apply_voice_config(midi_ch);
}

// Apply the current button states to VoiceAllocator for a single MIDI channel.
void PanelWindow::apply_voice_config(int midi_ch)
{
	auto &tab = midi_tabs_[midi_ch];
	auto assigned = collect_checked(tab);

	retrowave::VoiceConfig config = voice_alloc_.voice_config(static_cast<uint8_t>(midi_ch));
	config.opl3_channels = assigned;
	config.four_op = tab.four_op_cb && tab.four_op_cb->isChecked();
	if (running_) {
		std::lock_guard<std::mutex> lg(hw_buf_.mutex());
		voice_alloc_.set_voice_config(static_cast<uint8_t>(midi_ch), config);
	} else {
		voice_alloc_.set_voice_config(static_cast<uint8_t>(midi_ch), config);
	}

	// Update unison max and poly label
	int pool = voice_alloc_.poly_voice_count(static_cast<uint8_t>(midi_ch));
	int unison = std::max<int>(config.unison_count, 1);
	tab.unison_spin->setMaximum(std::max(pool * unison, 1));
	update_poly_label(midi_ch);
}

// Refresh enabled/disabled state of all route buttons across all tabs.
// A button is disabled if:
// - It's a percussion channel (6-8) and perc mode is active, OR
// - It's an auto-linked 4-op partner on a tab that has 4-op enabled
void PanelWindow::refresh_all_button_states()
{
	using namespace retrowave::opl3;
	bool perc_on = voice_alloc_.percussion_mode();

	// First pass: figure out which channels are "4-op locked" on each tab
	// (i.e., auto-included as a partner and should be disabled)
	bool locked_by[16][18] = {};  // locked_by[midi_ch][opl3_ch] = true if disabled

	for (int midi_ch = 0; midi_ch < 16; ++midi_ch) {
		auto &tab = midi_tabs_[midi_ch];
		if (!(tab.four_op_cb && tab.four_op_cb->isChecked())) continue;

		auto assigned = collect_checked(tab);
		for (uint8_t ch : assigned) {
			int partner = four_op_partner(ch);
			if (partner >= 0) {
				// The partner button on this tab is locked (user can toggle
				// either one, but we lock the secondary of each pair).
				// Determine which is primary (lower of the pair is always primary
				// in OPL3: 0,1,2 are primary over 3,4,5; 9,10,11 over 12,13,14)
				uint8_t secondary = static_cast<uint8_t>(
					(ch < static_cast<uint8_t>(partner)) ? partner : ch);
				locked_by[midi_ch][secondary] = true;
			}
		}
	}

	// Second pass: update all button states
	for (int midi_ch = 0; midi_ch < 16; ++midi_ch) {
		for (int ch = 0; ch < 18; ++ch) {
			auto *btn = midi_tabs_[midi_ch].route_btns[ch];
			if (!btn) continue;

			bool disabled = false;

			// Percussion constraint
			if (perc_on && ch >= 6 && ch <= 8)
				disabled = true;

			// 4-op locked on this tab
			if (locked_by[midi_ch][ch])
				disabled = true;

			// Non-pairable channel on a 4-op tab
			if (midi_tabs_[midi_ch].four_op_cb &&
			    midi_tabs_[midi_ch].four_op_cb->isChecked() &&
			    four_op_partner(ch) < 0)
				disabled = true;

			btn->setEnabled(!disabled);

			// Style: auto-linked 4-op partners get a dimmer color
			if (locked_by[midi_ch][ch] && btn->isChecked()) {
				btn->setStyleSheet(
					"QPushButton:checked { background-color: #3a7ab9; color: #ccc; }"
					"QPushButton:checked:!enabled { background-color: #3a7ab9; color: #ccc; }");
			} else {
				btn->setStyleSheet(
					"QPushButton:checked { background-color: #4a90d9; color: white; }");
			}
		}
	}
}

void PanelWindow::on_unison_changed(int midi_ch, int unison)
{
	retrowave::VoiceConfig config = voice_alloc_.voice_config(static_cast<uint8_t>(midi_ch));
	config.unison_count = static_cast<uint8_t>(unison);
	if (running_) {
		std::lock_guard<std::mutex> lg(hw_buf_.mutex());
		voice_alloc_.set_voice_config(static_cast<uint8_t>(midi_ch), config);
	} else {
		voice_alloc_.set_voice_config(static_cast<uint8_t>(midi_ch), config);
	}
	update_poly_label(midi_ch);
}

void PanelWindow::on_detune_changed(int midi_ch, int cents)
{
	retrowave::VoiceConfig config = voice_alloc_.voice_config(static_cast<uint8_t>(midi_ch));
	config.detune_cents = static_cast<uint8_t>(cents);
	if (running_) {
		std::lock_guard<std::mutex> lg(hw_buf_.mutex());
		voice_alloc_.set_voice_config(static_cast<uint8_t>(midi_ch), config);
	} else {
		voice_alloc_.set_voice_config(static_cast<uint8_t>(midi_ch), config);
	}
}

void PanelWindow::update_poly_label(int midi_ch)
{
	int poly = voice_alloc_.poly_voice_count(static_cast<uint8_t>(midi_ch));
	auto &tab = midi_tabs_[midi_ch];
	if (tab.poly_label) {
		if (poly == 1)
			tab.poly_label->setText("Poly: 1 voice");
		else
			tab.poly_label->setText("Poly: " + QString::number(poly) + " voices");
	}
}

// --- Percussion mode ---

void PanelWindow::on_perc_mode_toggled(bool enabled)
{
	if (running_) {
		std::lock_guard<std::mutex> lg(hw_buf_.mutex());
		voice_alloc_.set_percussion_mode(enabled);
	} else {
		voice_alloc_.set_percussion_mode(enabled);
	}

	perc_group_->setVisible(enabled);

	// Uncheck percussion channels (6-8) when perc mode enabled
	if (enabled) {
		for (int midi_ch = 0; midi_ch < 16; ++midi_ch) {
			bool changed = false;
			for (int ch = 6; ch <= 8; ++ch) {
				if (midi_tabs_[midi_ch].route_btns[ch] &&
				    midi_tabs_[midi_ch].route_btns[ch]->isChecked()) {
					midi_tabs_[midi_ch].route_btns[ch]->blockSignals(true);
					midi_tabs_[midi_ch].route_btns[ch]->setChecked(false);
					midi_tabs_[midi_ch].route_btns[ch]->blockSignals(false);
					changed = true;
				}
			}
			if (changed)
				apply_voice_config(midi_ch);
		}
	}

	refresh_all_button_states();
}

void PanelWindow::on_drum_routing_changed(int drum_idx, int midi_ch)
{
	auto drum = static_cast<retrowave::DirectMode::Drum>(drum_idx);
	if (running_) {
		std::lock_guard<std::mutex> lg(hw_buf_.mutex());
		voice_alloc_.set_drum_midi_channel(drum, midi_ch);
	} else {
		voice_alloc_.set_drum_midi_channel(drum, midi_ch);
	}
}

// --- Port refresh ---

void PanelWindow::refresh_serial_ports()
{
	cb_serial_->clear();
	for (const auto &info : QSerialPortInfo::availablePorts()) {
		QString disp = info.portName();
		auto desc = info.description();
		if (!desc.isEmpty())
			disp += " | " + desc;
		cb_serial_->addItem(disp, info.portName());
	}
}

void PanelWindow::refresh_midi_ports()
{
	cb_midi_->clear();
	if (!midiin_) return;

	unsigned count = midiin_->getPortCount();
	for (unsigned i = 0; i < count; ++i) {
		std::string name = midiin_->getPortName(i);
		cb_midi_->addItem(QString::number(i) + " - " + QString::fromStdString(name),
		                   QVariant::fromValue(static_cast<int>(i)));
	}
}

void PanelWindow::on_serial_refresh_clicked()
{
	refresh_serial_ports();
}

void PanelWindow::on_midi_refresh_clicked()
{
	refresh_midi_ports();
}

// --- Start / Stop ---

void PanelWindow::on_start_stop_clicked()
{
	if (!running_) {
		if (cb_serial_->count() == 0) {
			QMessageBox::warning(this, "Error", "No serial port selected");
			return;
		}

		if (!serial_.open(cb_serial_->currentData().toString().toStdString())) {
			QMessageBox::warning(this, "Error", "Failed to open serial port");
			return;
		}

		// Init OPL3 before opening MIDI (no contention yet)
		direct_mode_.init();
		hw_buf_.flush();

		// Start flush timer
		flush_timer_ = new QTimer(this);
		flush_timer_->setTimerType(Qt::PreciseTimer);
		connect(flush_timer_, &QTimer::timeout, this, &PanelWindow::on_flush_timer);
		flush_timer_->start(1);

		// Open MIDI input last (callback thread starts here)
		if (midiin_ && cb_midi_->count() > 0) {
			int port = cb_midi_->currentData().toInt();
			try {
				midiin_->openPort(port);
				midiin_->ignoreTypes(false, true, true);
			} catch (RtMidiError &e) {
				flush_timer_->stop();
				delete flush_timer_;
				flush_timer_ = nullptr;
				serial_.close();
				QMessageBox::warning(this, "Error",
				    "Failed to open MIDI port: " + QString::fromStdString(e.getMessage()));
				return;
			}
		}

		running_ = true;
		btn_start_->setText("Stop");
		cb_serial_->setEnabled(false);
		cb_midi_->setEnabled(false);
		statusBar()->showMessage("Running");
	} else {
		// Stop: close MIDI first to stop callback thread
		if (midiin_)
			midiin_->closePort();

		flush_timer_->stop();
		delete flush_timer_;
		flush_timer_ = nullptr;

		serial_.close();

		running_ = false;
		btn_start_->setText("Start");
		cb_serial_->setEnabled(true);
		cb_midi_->setEnabled(true);
		statusBar()->showMessage("Stopped");
	}
}

// --- MIDI callback (rtmidi thread) ---

void PanelWindow::midi_callback(double /*ts*/, std::vector<unsigned char> *msg, void *user)
{
	auto *self = static_cast<PanelWindow *>(user);
	std::lock_guard<std::mutex> lg(self->hw_buf_.mutex());
	self->voice_alloc_.process_midi(msg->data(), msg->size());
}

// --- Flush timer (Qt main thread) ---

void PanelWindow::on_flush_timer()
{
	std::lock_guard<std::mutex> lg(hw_buf_.mutex());
	hw_buf_.flush();
}

// --- NRPN sender (Qt main thread) ---

void PanelWindow::send_nrpn_to_midi_ch(uint8_t midi_ch, uint8_t msb, uint8_t lsb, uint8_t value)
{
	if (!running_) return;

	std::lock_guard<std::mutex> lg(hw_buf_.mutex());

	// Global NRPNs (MSB 5) only need to go to one OPL3 channel
	if (msb == 5) {
		direct_mode_.direct_nrpn(0, msb, lsb, value);
		return;
	}

	// Broadcast to all OPL3 channels assigned to this MIDI channel
	const auto &config = voice_alloc_.voice_config(midi_ch);
	if (config.opl3_channels.empty()) {
		// Fallback: if no channels assigned, use midi_ch directly
		direct_mode_.direct_nrpn(midi_ch, msb, lsb, value);
	} else {
		for (uint8_t opl3_ch : config.opl3_channels) {
			direct_mode_.direct_nrpn(opl3_ch, msb, lsb, value);
		}
	}
}

// --- Bank browser ---

void PanelWindow::scan_bank_directory()
{
	QString bankDir = QCoreApplication::applicationDirPath() + "/banks";
	QDir dir(bankDir);
	if (!dir.exists())
		return;

	QStringList filters;
	filters << "*.wopl";
	QFileInfoList files = dir.entryInfoList(filters, QDir::Files, QDir::Name);
	for (const auto &fi : files)
		load_bank_file(fi.absoluteFilePath());

	populate_bank_combos();
}

void PanelWindow::load_bank_file(const QString &path)
{
	QFile f(path);
	if (!f.open(QIODevice::ReadOnly))
		return;

	QByteArray data = f.readAll();
	f.close();

	int error = 0;
	WOPLFile *wopl = WOPL_LoadBankFromMem(data.data(), static_cast<size_t>(data.size()), &error);
	if (!wopl)
		return;

	LoadedBank bank;
	bank.name = QFileInfo(path).completeBaseName();
	bank.path = path;
	bank.wopl = wopl;
	loaded_banks_.push_back(std::move(bank));
}

void PanelWindow::populate_bank_combos()
{
	for (int ch = 0; ch < 16; ++ch) {
		auto *combo = midi_tabs_[ch].bank_combo;
		if (!combo) continue;

		int prev = combo->currentIndex();
		combo->blockSignals(true);
		combo->clear();
		for (const auto &bank : loaded_banks_)
			combo->addItem(bank.name);
		if (prev >= 0 && prev < combo->count())
			combo->setCurrentIndex(prev);
		else if (combo->count() > 0)
			combo->setCurrentIndex(0);
		combo->blockSignals(false);

		// Trigger initial population if we have banks
		if (combo->count() > 0)
			on_bank_selected(ch, combo->currentIndex());
	}
}

void PanelWindow::on_bank_selected(int midi_ch, int bank_index)
{
	auto &tab = midi_tabs_[midi_ch];
	if (!tab.inst_list || bank_index < 0 ||
	    bank_index >= static_cast<int>(loaded_banks_.size()))
		return;

	const auto &bank = loaded_banks_[static_cast<size_t>(bank_index)];
	if (!bank.wopl || bank.wopl->banks_count_melodic == 0)
		return;

	// Store all instruments as items with user data (instrument index)
	tab.inst_list->clear();
	const WOPLBank &melodic = bank.wopl->banks_melodic[0];
	for (int i = 0; i < 128; ++i) {
		const WOPLInstrument &inst = melodic.ins[i];
		bool is_blank = (inst.inst_flags & WOPL_Ins_IsBlank);
		bool is_4op = (inst.inst_flags & WOPL_Ins_4op);
		bool is_p4op = (inst.inst_flags & WOPL_Ins_Pseudo4op);

		QString prefix;
		if (is_4op) prefix = "[4op] ";
		else if (is_p4op) prefix = "[P4] ";

		QString name = QString::fromLatin1(inst.inst_name, static_cast<int>(strnlen(inst.inst_name, 34)));
		bool gm_fallback = name.isEmpty();
		if (gm_fallback)
			name = QString::fromLatin1(kGMNames[i]);
		QString label = QString("%1: %2%3").arg(i, 3, 10, QChar('0')).arg(prefix).arg(name);

		auto *item = new QListWidgetItem(label);
		item->setData(Qt::UserRole, i);
		if (is_blank)
			item->setData(Qt::UserRole + 1, true);  // flag for filtering
		if (gm_fallback) {
			QFont f = item->font();
			f.setItalic(true);
			item->setFont(f);
			item->setForeground(QColor(140, 140, 140));
		}
		tab.inst_list->addItem(item);
	}

	filter_instrument_list(midi_ch);
}

void PanelWindow::filter_instrument_list(int midi_ch)
{
	auto &tab = midi_tabs_[midi_ch];
	if (!tab.inst_list) return;

	bool show_blank = tab.show_blank_cb && tab.show_blank_cb->isChecked();

	for (int i = 0; i < tab.inst_list->count(); ++i) {
		auto *item = tab.inst_list->item(i);
		bool is_blank = item->data(Qt::UserRole + 1).toBool();
		item->setHidden(is_blank && !show_blank);
	}
}

void PanelWindow::on_instrument_apply(int midi_ch)
{
	auto &tab = midi_tabs_[midi_ch];
	if (!tab.inst_list || !tab.bank_combo) return;

	int bank_index = tab.bank_combo->currentIndex();
	if (bank_index < 0 || bank_index >= static_cast<int>(loaded_banks_.size()))
		return;

	auto *item = tab.inst_list->currentItem();
	if (!item) return;

	int inst_index = item->data(Qt::UserRole).toInt();
	const auto &bank = loaded_banks_[static_cast<size_t>(bank_index)];
	if (!bank.wopl || bank.wopl->banks_count_melodic == 0)
		return;

	const WOPLInstrument &inst = bank.wopl->banks_melodic[0].ins[inst_index];
	apply_instrument(midi_ch, inst);
}

void PanelWindow::apply_instrument(int midi_ch, const WOPLInstrument &inst)
{
	auto &tab = midi_tabs_[midi_ch];
	auto ch = static_cast<uint8_t>(midi_ch);

	// Determine 4-op mode
	bool is_4op = (inst.inst_flags & WOPL_Ins_4op) || (inst.inst_flags & WOPL_Ins_Pseudo4op);
	int num_ops = is_4op ? 4 : 2;

	// Set 4-op checkbox (triggers on_four_op_toggled via signal — must happen
	// first so OP3/OP4 groups are visible and algorithm combo has correct items)
	if (tab.four_op_cb && tab.four_op_cb->isChecked() != is_4op)
		tab.four_op_cb->setChecked(is_4op);

	// WOPL operator order:  [0]=Carrier1, [1]=Modulator1, [2]=Carrier2, [3]=Modulator2
	// Panel nrpn_msb order: 0=OP1(Mod),   1=OP2(Car),    2=OP3(Mod2),  3=OP4(Car2)
	static const int wopl_to_panel[] = {1, 0, 3, 2};

	// Block signals on all widgets so we can set values without duplicate NRPNs,
	// then send all NRPNs explicitly afterwards (guarantees every parameter is
	// sent even if the widget value didn't change).
	for (int wopl_idx = 0; wopl_idx < num_ops; ++wopl_idx) {
		int pi = wopl_to_panel[wopl_idx];
		auto &ow = tab.op_widgets[pi];
		if (ow.attack)       ow.attack->blockSignals(true);
		if (ow.decay)        ow.decay->blockSignals(true);
		if (ow.sustain)      ow.sustain->blockSignals(true);
		if (ow.release)      ow.release->blockSignals(true);
		if (ow.waveform)     ow.waveform->blockSignals(true);
		if (ow.freq_mult)    ow.freq_mult->blockSignals(true);
		if (ow.out_level)    ow.out_level->blockSignals(true);
		if (ow.ksl)          ow.ksl->blockSignals(true);
		if (ow.tremolo)      ow.tremolo->blockSignals(true);
		if (ow.vibrato)      ow.vibrato->blockSignals(true);
		if (ow.sustain_mode) ow.sustain_mode->blockSignals(true);
		if (ow.ksr)          ow.ksr->blockSignals(true);
	}
	if (tab.feedback_dial) tab.feedback_dial->blockSignals(true);
	if (tab.pan_left_cb)   tab.pan_left_cb->blockSignals(true);
	if (tab.pan_right_cb)  tab.pan_right_cb->blockSignals(true);
	if (tab.alg_combo)     tab.alg_combo->blockSignals(true);

	// Set all widget values (UI update only, no signals)
	for (int wopl_idx = 0; wopl_idx < num_ops; ++wopl_idx) {
		int pi = wopl_to_panel[wopl_idx];
		auto &ow = tab.op_widgets[pi];
		const WOPLOperator &op = inst.operators[wopl_idx];

		if (ow.attack)       ow.attack->setValue((op.atdec_60 >> 4) & 0x0F);
		if (ow.decay)        ow.decay->setValue(op.atdec_60 & 0x0F);
		if (ow.sustain)      ow.sustain->setValue((op.susrel_80 >> 4) & 0x0F);
		if (ow.release)      ow.release->setValue(op.susrel_80 & 0x0F);
		if (ow.waveform)     ow.waveform->setCurrentIndex(op.waveform_E0 & 0x07);
		if (ow.freq_mult)    ow.freq_mult->setValue(op.avekf_20 & 0x0F);
		if (ow.out_level)    ow.out_level->setValue(op.ksl_l_40 & 0x3F);
		if (ow.ksl)          ow.ksl->setValue((op.ksl_l_40 >> 6) & 0x03);
		if (ow.tremolo)      ow.tremolo->setChecked(op.avekf_20 & 0x80);
		if (ow.vibrato)      ow.vibrato->setChecked(op.avekf_20 & 0x40);
		if (ow.sustain_mode) ow.sustain_mode->setChecked(op.avekf_20 & 0x20);
		if (ow.ksr)          ow.ksr->setChecked(op.avekf_20 & 0x10);
	}

	int feedback = (inst.fb_conn1_C0 >> 1) & 0x07;
	if (tab.feedback_dial)
		tab.feedback_dial->setValue(feedback);

	if (tab.alg_combo) {
		if (is_4op) {
			int conn1 = inst.fb_conn1_C0 & 0x01;
			int conn2 = inst.fb_conn2_C0 & 0x01;
			int alg_index = (conn1) | (conn2 << 1);
			if (alg_index < tab.alg_combo->count())
				tab.alg_combo->setCurrentIndex(alg_index);
		} else {
			int conn1 = inst.fb_conn1_C0 & 0x01;
			if (conn1 < tab.alg_combo->count())
				tab.alg_combo->setCurrentIndex(conn1);
		}
	}

	if (tab.pan_left_cb)  tab.pan_left_cb->setChecked(true);
	if (tab.pan_right_cb) tab.pan_right_cb->setChecked(true);

	// Unblock signals
	for (int wopl_idx = 0; wopl_idx < num_ops; ++wopl_idx) {
		int pi = wopl_to_panel[wopl_idx];
		auto &ow = tab.op_widgets[pi];
		if (ow.attack)       ow.attack->blockSignals(false);
		if (ow.decay)        ow.decay->blockSignals(false);
		if (ow.sustain)      ow.sustain->blockSignals(false);
		if (ow.release)      ow.release->blockSignals(false);
		if (ow.waveform)     ow.waveform->blockSignals(false);
		if (ow.freq_mult)    ow.freq_mult->blockSignals(false);
		if (ow.out_level)    ow.out_level->blockSignals(false);
		if (ow.ksl)          ow.ksl->blockSignals(false);
		if (ow.tremolo)      ow.tremolo->blockSignals(false);
		if (ow.vibrato)      ow.vibrato->blockSignals(false);
		if (ow.sustain_mode) ow.sustain_mode->blockSignals(false);
		if (ow.ksr)          ow.ksr->blockSignals(false);
	}
	if (tab.feedback_dial) tab.feedback_dial->blockSignals(false);
	if (tab.pan_left_cb)   tab.pan_left_cb->blockSignals(false);
	if (tab.pan_right_cb)  tab.pan_right_cb->blockSignals(false);
	if (tab.alg_combo)     tab.alg_combo->blockSignals(false);

	// Explicitly send all NRPNs to hardware (unconditional — every parameter is sent
	// regardless of whether the widget value changed, ensuring a complete patch load)
	for (int wopl_idx = 0; wopl_idx < num_ops; ++wopl_idx) {
		int pi = wopl_to_panel[wopl_idx];
		auto msb = static_cast<uint8_t>(pi);
		const WOPLOperator &op = inst.operators[wopl_idx];

		send_nrpn_to_midi_ch(ch, msb, 0,  static_cast<uint8_t>(((op.atdec_60 >> 4) & 0x0F) << 3));
		send_nrpn_to_midi_ch(ch, msb, 1,  static_cast<uint8_t>((op.atdec_60 & 0x0F) << 3));
		send_nrpn_to_midi_ch(ch, msb, 2,  static_cast<uint8_t>(((op.susrel_80 >> 4) & 0x0F) << 3));
		send_nrpn_to_midi_ch(ch, msb, 3,  static_cast<uint8_t>((op.susrel_80 & 0x0F) << 3));
		send_nrpn_to_midi_ch(ch, msb, 4,  static_cast<uint8_t>((op.waveform_E0 & 0x07) << 4));
		send_nrpn_to_midi_ch(ch, msb, 5,  static_cast<uint8_t>((op.avekf_20 & 0x0F) << 3));
		send_nrpn_to_midi_ch(ch, msb, 6,  static_cast<uint8_t>((op.ksl_l_40 & 0x3F) << 1));
		send_nrpn_to_midi_ch(ch, msb, 7,  static_cast<uint8_t>(((op.ksl_l_40 >> 6) & 0x03) << 5));
		send_nrpn_to_midi_ch(ch, msb, 8,  (op.avekf_20 & 0x80) ? 127 : 0);
		send_nrpn_to_midi_ch(ch, msb, 9,  (op.avekf_20 & 0x40) ? 127 : 0);
		send_nrpn_to_midi_ch(ch, msb, 10, (op.avekf_20 & 0x20) ? 127 : 0);
		send_nrpn_to_midi_ch(ch, msb, 11, (op.avekf_20 & 0x10) ? 127 : 0);
	}

	// Channel parameters
	send_nrpn_to_midi_ch(ch, 4, 0, static_cast<uint8_t>(feedback << 4));

	if (is_4op) {
		uint8_t conn1 = inst.fb_conn1_C0 & 0x01;
		uint8_t conn2 = inst.fb_conn2_C0 & 0x01;
		send_nrpn_to_midi_ch(ch, 4, 1, conn1 ? 127 : 0);
		send_nrpn_to_midi_ch(ch, 4, 5, conn2 ? 127 : 0);
	} else {
		uint8_t conn1 = inst.fb_conn1_C0 & 0x01;
		send_nrpn_to_midi_ch(ch, 4, 1, conn1 ? 127 : 0);
	}

	send_nrpn_to_midi_ch(ch, 4, 2, 127);  // Pan Left on
	send_nrpn_to_midi_ch(ch, 4, 3, 127);  // Pan Right on
}
