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

#include <QWidget>
#include <cstdint>

class FMDiagramWidget : public QWidget {
	Q_OBJECT

public:
	explicit FMDiagramWidget(QWidget *parent = nullptr);

	// Connection type: false = FM (modulator→carrier), true = AM (additive)
	void setConnection(bool am);

	// Enable 4-operator display
	void setFourOp(bool enabled);

	// Set the 4-op algorithm from the two connection bits (conn1, conn2).
	// 0,0 = serial chain; 1,0 = parallel first pair; 0,1 = parallel pairs; 1,1 = 3 additive
	void setFourOpAlgorithm(uint8_t conn1, uint8_t conn2);

	// Set feedback level (0-7) for annotation
	void setFeedback(uint8_t fb);

	QSize sizeHint() const override;
	QSize minimumSizeHint() const override;

protected:
	void paintEvent(QPaintEvent *event) override;

private:
	void draw2Op(QPainter &p, int w, int h);
	void draw4Op(QPainter &p, int w, int h);
	void drawOperatorBox(QPainter &p, int x, int y, int bw, int bh,
	                      const QString &label, bool is_carrier);
	void drawArrow(QPainter &p, int x1, int y1, int x2, int y2);
	void drawFeedbackLoop(QPainter &p, int x, int y, int bw, int bh);

	bool am_ = false;       // 2-op connection
	bool four_op_ = false;
	uint8_t conn1_ = 0;     // connection bit for first channel pair
	uint8_t conn2_ = 0;     // connection bit for second channel pair
	uint8_t feedback_ = 0;
};

// Small widget displaying an OPL3 waveform shape (0-7).
class WaveformWidget : public QWidget {
	Q_OBJECT

public:
	explicit WaveformWidget(QWidget *parent = nullptr);

	void setWaveform(int waveform);

	QSize sizeHint() const override;
	QSize minimumSizeHint() const override;

protected:
	void paintEvent(QPaintEvent *event) override;

private:
	int waveform_ = 0;
};

// ADSR envelope diagram showing attack/decay/sustain/release shape.
class EnvelopeWidget : public QWidget {
	Q_OBJECT

public:
	explicit EnvelopeWidget(QWidget *parent = nullptr);

	void setAttack(int val);
	void setDecay(int val);
	void setSustain(int val);
	void setRelease(int val);

	QSize sizeHint() const override;
	QSize minimumSizeHint() const override;

protected:
	void paintEvent(QPaintEvent *event) override;

private:
	int attack_ = 15;
	int decay_ = 4;
	int sustain_ = 2;
	int release_ = 4;
};

// Small widget showing KSL attenuation curve (0-3).
class KSLCurveWidget : public QWidget {
	Q_OBJECT

public:
	explicit KSLCurveWidget(QWidget *parent = nullptr);

	void setKSL(int val);

	QSize sizeHint() const override;
	QSize minimumSizeHint() const override;

protected:
	void paintEvent(QPaintEvent *event) override;

private:
	int ksl_ = 0;
};
