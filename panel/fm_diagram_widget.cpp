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

#include "fm_diagram_widget.h"
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <cmath>

static const QColor kModulatorColor(100, 149, 237);  // cornflower blue
static const QColor kCarrierColor(255, 165, 0);      // orange
static const QColor kArrowColor(200, 200, 200);
static const QColor kFeedbackColor(180, 180, 80);
static const QColor kOutColor(100, 200, 100);

FMDiagramWidget::FMDiagramWidget(QWidget *parent)
	: QWidget(parent)
{
	setMinimumSize(180, 120);
}

void FMDiagramWidget::setConnection(bool am)
{
	am_ = am;
	// In 2-op mode, conn1_ mirrors the single connection bit
	conn1_ = am ? 1 : 0;
	update();
}

void FMDiagramWidget::setFourOp(bool enabled)
{
	four_op_ = enabled;
	updateGeometry();
	update();
}

void FMDiagramWidget::setFourOpAlgorithm(uint8_t conn1, uint8_t conn2)
{
	conn1_ = conn1;
	conn2_ = conn2;
	update();
}

void FMDiagramWidget::setFeedback(uint8_t fb)
{
	feedback_ = fb;
	update();
}

QSize FMDiagramWidget::sizeHint() const
{
	return QSize(300, 150);
}

QSize FMDiagramWidget::minimumSizeHint() const
{
	return QSize(160, 100);
}

void FMDiagramWidget::paintEvent(QPaintEvent *)
{
	QPainter p(this);
	p.setRenderHint(QPainter::Antialiasing);

	int w = width();
	int h = height();

	// Background
	p.fillRect(rect(), palette().color(QPalette::Window));

	if (four_op_)
		draw4Op(p, w, h);
	else
		draw2Op(p, w, h);
}

void FMDiagramWidget::draw2Op(QPainter &p, int w, int h)
{
	int bw = 50, bh = 30;
	int margin = 16;
	int spacing = 30;

	int total_w = bw * 2 + spacing;
	int start_x = (w - total_w - margin) / 2;
	int cy = h / 2 - bh / 2;

	int x1 = start_x;
	int x2 = start_x + bw + spacing;
	int out_x = x2 + bw + 10;

	if (am_) {
		// AM (additive): both operators go to output
		int y1 = cy - bh / 2 - 4;
		int y2 = cy + bh / 2 + 4;

		drawOperatorBox(p, x1, y1, bw, bh, "OP1", false);
		drawOperatorBox(p, x1, y2, bw, bh, "OP2", true);

		// Plus sign between
		p.setPen(QPen(kArrowColor, 1.5));
		QFont font = p.font();
		font.setBold(true);
		p.setFont(font);
		p.drawText(x1 + bw + 4, y1 + bh / 2 + 4, "+");

		// Arrows to output
		drawArrow(p, x1 + bw, y1 + bh / 2, out_x - 6, cy);
		drawArrow(p, x1 + bw, y2 + bh / 2, out_x - 6, cy);

		// Out label
		p.setPen(kOutColor);
		p.drawText(out_x, cy + 4, "Out");
	} else {
		// FM: OP1 → OP2 → Out
		drawOperatorBox(p, x1, cy, bw, bh, "OP1", false);
		drawOperatorBox(p, x2, cy, bw, bh, "OP2", true);

		drawArrow(p, x1 + bw, cy + bh / 2, x2, cy + bh / 2);
		drawArrow(p, x2 + bw, cy + bh / 2, out_x, cy + bh / 2);

		p.setPen(kOutColor);
		p.drawText(out_x + 4, cy + bh / 2 + 4, "Out");
	}

	// Feedback on OP1
	if (feedback_ > 0) {
		drawFeedbackLoop(p, am_ ? x1 : x1, am_ ? (cy - bh / 2 - 4) : cy, bw, bh);
	}

	// Feedback label
	p.setPen(palette().color(QPalette::Text));
	QFont small = p.font();
	small.setPointSize(small.pointSize() - 1);
	p.setFont(small);
	p.drawText(4, h - 4, "FB: " + QString::number(feedback_));
}

void FMDiagramWidget::draw4Op(QPainter &p, int w, int h)
{
	int bw = 44, bh = 24;
	int hsp = 20; // horizontal spacing
	int vsp = 10; // vertical spacing

	// 4-op algorithm based on conn1_ and conn2_:
	// 0,0: OP1→OP2→OP3→OP4→Out (serial chain)
	// 1,0: (OP1+OP2)→OP3→OP4→Out
	// 0,1: OP1→OP2 + OP3→OP4 → Out (parallel FM pairs)
	// 1,1: OP1 + OP2 + OP3→OP4 → Out

	uint8_t alg = (conn1_ & 1) | ((conn2_ & 1) << 1);

	int total_h = bh * 2 + vsp;
	int base_y = (h - total_h) / 2;
	int y_top = base_y;
	int y_bot = base_y + bh + vsp;

	int start_x = 8;
	int col0 = start_x;
	int col1 = col0 + bw + hsp;
	int col2 = col1 + bw + hsp;
	int col3 = col2 + bw + hsp;
	int out_x = col3 + bw + 8;

	int cy = (y_top + y_bot + bh) / 2;

	switch (alg) {
	case 0: {
		// Serial: OP1→OP2→OP3→OP4→Out
		int sy = (h - bh) / 2;
		drawOperatorBox(p, col0, sy, bw, bh, "OP1", false);
		drawOperatorBox(p, col1, sy, bw, bh, "OP2", false);
		drawOperatorBox(p, col2, sy, bw, bh, "OP3", false);
		drawOperatorBox(p, col3, sy, bw, bh, "OP4", true);
		drawArrow(p, col0 + bw, sy + bh / 2, col1, sy + bh / 2);
		drawArrow(p, col1 + bw, sy + bh / 2, col2, sy + bh / 2);
		drawArrow(p, col2 + bw, sy + bh / 2, col3, sy + bh / 2);
		drawArrow(p, col3 + bw, sy + bh / 2, out_x, sy + bh / 2);
		p.setPen(kOutColor);
		p.drawText(out_x + 2, sy + bh / 2 + 4, "Out");
		if (feedback_ > 0) drawFeedbackLoop(p, col0, sy, bw, bh);
		break;
	}
	case 1: {
		// (OP1+OP2)→OP3→OP4→Out
		drawOperatorBox(p, col0, y_top, bw, bh, "OP1", false);
		drawOperatorBox(p, col0, y_bot, bw, bh, "OP2", false);
		drawOperatorBox(p, col1 + (bw + hsp) / 2, cy - bh / 2, bw, bh, "OP3", false);
		drawOperatorBox(p, col2 + (bw + hsp) / 2, cy - bh / 2, bw, bh, "OP4", true);

		drawArrow(p, col0 + bw, y_top + bh / 2, col1 + (bw + hsp) / 2, cy);
		drawArrow(p, col0 + bw, y_bot + bh / 2, col1 + (bw + hsp) / 2, cy);
		drawArrow(p, col1 + (bw + hsp) / 2 + bw, cy, col2 + (bw + hsp) / 2, cy);
		drawArrow(p, col2 + (bw + hsp) / 2 + bw, cy, out_x, cy);

		p.setPen(kOutColor);
		p.drawText(out_x + 2, cy + 4, "Out");
		if (feedback_ > 0) drawFeedbackLoop(p, col0, y_top, bw, bh);
		break;
	}
	case 2: {
		// OP1→OP2 + OP3→OP4 → Out (parallel FM pairs)
		drawOperatorBox(p, col0, y_top, bw, bh, "OP1", false);
		drawOperatorBox(p, col1, y_top, bw, bh, "OP2", true);
		drawOperatorBox(p, col0, y_bot, bw, bh, "OP3", false);
		drawOperatorBox(p, col1, y_bot, bw, bh, "OP4", true);

		drawArrow(p, col0 + bw, y_top + bh / 2, col1, y_top + bh / 2);
		drawArrow(p, col0 + bw, y_bot + bh / 2, col1, y_bot + bh / 2);
		drawArrow(p, col1 + bw, y_top + bh / 2, out_x - 6, cy);
		drawArrow(p, col1 + bw, y_bot + bh / 2, out_x - 6, cy);

		p.setPen(kOutColor);
		p.drawText(out_x, cy + 4, "Out");
		if (feedback_ > 0) drawFeedbackLoop(p, col0, y_top, bw, bh);
		break;
	}
	case 3: {
		// OP1 + OP2 + OP3→OP4 → Out
		int y0 = base_y - bh / 2 - 2;
		int y1 = (h - bh) / 2;
		int y2 = base_y + total_h - bh / 2 + 2;

		drawOperatorBox(p, col0, y0, bw, bh, "OP1", true);
		drawOperatorBox(p, col0, y1, bw, bh, "OP2", true);
		drawOperatorBox(p, col0, y2, bw, bh, "OP3", false);
		drawOperatorBox(p, col1, y2, bw, bh, "OP4", true);

		drawArrow(p, col0 + bw, y2 + bh / 2, col1, y2 + bh / 2);
		drawArrow(p, col0 + bw, y0 + bh / 2, out_x - 6, cy);
		drawArrow(p, col0 + bw, y1 + bh / 2, out_x - 6, cy);
		drawArrow(p, col1 + bw, y2 + bh / 2, out_x - 6, cy);

		p.setPen(kOutColor);
		p.drawText(out_x, cy + 4, "Out");
		if (feedback_ > 0) drawFeedbackLoop(p, col0, y0, bw, bh);
		break;
	}
	}

	// Feedback label
	p.setPen(palette().color(QPalette::Text));
	QFont small = p.font();
	small.setPointSize(small.pointSize() - 1);
	p.setFont(small);
	p.drawText(4, h - 4, "FB: " + QString::number(feedback_));
}

void FMDiagramWidget::drawOperatorBox(QPainter &p, int x, int y, int bw, int bh,
                                       const QString &label, bool is_carrier)
{
	QColor fill = is_carrier ? kCarrierColor : kModulatorColor;
	p.setPen(QPen(fill.darker(150), 1.5));
	p.setBrush(fill);
	p.drawRoundedRect(x, y, bw, bh, 4, 4);

	p.setPen(Qt::white);
	QFont font = p.font();
	font.setBold(true);
	font.setPointSize(font.pointSize() - 1);
	p.setFont(font);
	p.drawText(QRect(x, y, bw, bh), Qt::AlignCenter, label);
}

void FMDiagramWidget::drawArrow(QPainter &p, int x1, int y1, int x2, int y2)
{
	p.setPen(QPen(kArrowColor, 1.5));
	p.drawLine(x1, y1, x2, y2);

	// Arrowhead
	double angle = std::atan2(y2 - y1, x2 - x1);
	int aw = 6;
	QPointF tip(x2, y2);
	QPointF p1(x2 - aw * std::cos(angle - 0.4), y2 - aw * std::sin(angle - 0.4));
	QPointF p2(x2 - aw * std::cos(angle + 0.4), y2 - aw * std::sin(angle + 0.4));
	p.setBrush(kArrowColor);
	p.drawPolygon(QPolygonF() << tip << p1 << p2);
}

void FMDiagramWidget::drawFeedbackLoop(QPainter &p, int x, int y, int bw, int bh)
{
	p.setPen(QPen(kFeedbackColor, 1.2, Qt::DashLine));
	p.setBrush(Qt::NoBrush);

	// Draw a curved feedback loop above the operator box
	int arc_h = 10;
	QPainterPath path;
	path.moveTo(x + bw / 2 + 8, y);
	path.cubicTo(x + bw / 2 + 8, y - arc_h,
	             x + bw / 2 - 8, y - arc_h,
	             x + bw / 2 - 8, y);
	p.drawPath(path);

	// Small arrowhead on the feedback loop
	p.setBrush(kFeedbackColor);
	QPointF tip(x + bw / 2 - 8, y);
	QPointF a1(x + bw / 2 - 12, y - 4);
	QPointF a2(x + bw / 2 - 4, y - 4);
	p.drawPolygon(QPolygonF() << tip << a1 << a2);
}

// --- WaveformWidget ---

WaveformWidget::WaveformWidget(QWidget *parent)
	: QWidget(parent)
{
	setFixedSize(64, 32);
}

void WaveformWidget::setWaveform(int waveform)
{
	waveform_ = waveform;
	update();
}

QSize WaveformWidget::sizeHint() const
{
	return QSize(64, 32);
}

QSize WaveformWidget::minimumSizeHint() const
{
	return QSize(48, 24);
}

// Generate OPL3 waveform samples for display.
// The 8 waveforms are:
// 0: Sine
// 1: Half-sine (positive half only, zero for negative)
// 2: Abs-sine (absolute value of sine)
// 3: Pulse-sine (positive quarter, zero, repeat)
// 4: Sine (even periods only — sine for even, zero for odd)
// 5: Abs-sine (even periods only)
// 6: Square wave
// 7: Derived square (exponential sawtooth-like)
static double opl3_waveform_sample(int type, double phase)
{
	// Normalize phase to [0, 2*pi)
	double t = std::fmod(phase, 2.0 * M_PI);
	if (t < 0) t += 2.0 * M_PI;

	double s = std::sin(t);

	switch (type) {
	case 0: // Sine
		return s;
	case 1: // Half-sine
		return s > 0 ? s : 0.0;
	case 2: // Abs-sine
		return std::fabs(s);
	case 3: // Pulse-sine (positive quarter, then zero)
		if (t < M_PI / 2.0) return s;
		if (t < M_PI) return 0.0;
		if (t < 3.0 * M_PI / 2.0) return std::sin(t - M_PI);
		return 0.0;
	case 4: // Sine (even periods) — full sine cycle in first half, silence in second
		if (t < M_PI) return std::sin(2.0 * t);
		return 0.0;
	case 5: // Abs-sine (even periods) — rectified double-speed sine in first half
		if (t < M_PI) return std::fabs(std::sin(2.0 * t));
		return 0.0;
	case 6: // Square
		return s >= 0 ? 1.0 : -1.0;
	case 7: { // Derived square (abs of sawtooth-like)
		// Approximation: rising ramp in first half, falling in second
		double norm = t / (2.0 * M_PI);
		if (norm < 0.25) return norm * 4.0;
		if (norm < 0.5) return (0.5 - norm) * 4.0;
		if (norm < 0.75) return -(norm - 0.5) * 4.0;
		return -(1.0 - norm) * 4.0;
	}
	default:
		return s;
	}
}

void WaveformWidget::paintEvent(QPaintEvent *)
{
	QPainter p(this);
	p.setRenderHint(QPainter::Antialiasing);

	int w = width();
	int h = height();
	int margin = 2;

	// Background
	p.fillRect(rect(), palette().color(QPalette::Window));

	// Draw waveform
	p.setPen(QPen(QColor(100, 200, 255), 1.5));

	int plot_w = w - 2 * margin;
	int plot_h = h - 2 * margin;
	int cy = h / 2;

	QPainterPath path;
	bool first = true;
	for (int x = 0; x <= plot_w; ++x) {
		double phase = (static_cast<double>(x) / plot_w) * 2.0 * M_PI;
		double val = opl3_waveform_sample(waveform_, phase);
		int py = cy - static_cast<int>(val * plot_h / 2.0);
		if (first) {
			path.moveTo(margin + x, py);
			first = false;
		} else {
			path.lineTo(margin + x, py);
		}
	}
	p.drawPath(path);

	// Center line
	p.setPen(QPen(QColor(80, 80, 80), 0.5, Qt::DotLine));
	p.drawLine(margin, cy, w - margin, cy);
}

// --- EnvelopeWidget ---

static const QColor kEnvelopeColor(100, 200, 255);

EnvelopeWidget::EnvelopeWidget(QWidget *parent)
	: QWidget(parent)
{
	setFixedSize(120, 48);
}

void EnvelopeWidget::setAttack(int val)  { attack_ = val; update(); }
void EnvelopeWidget::setDecay(int val)   { decay_ = val; update(); }
void EnvelopeWidget::setSustain(int val) { sustain_ = val; update(); }
void EnvelopeWidget::setRelease(int val) { release_ = val; update(); }

QSize EnvelopeWidget::sizeHint() const     { return QSize(120, 48); }
QSize EnvelopeWidget::minimumSizeHint() const { return QSize(80, 32); }

void EnvelopeWidget::paintEvent(QPaintEvent *)
{
	QPainter p(this);
	p.setRenderHint(QPainter::Antialiasing);
	p.fillRect(rect(), palette().color(QPalette::Window));

	int w = width();
	int h = height();
	int margin = 4;
	int plot_w = w - 2 * margin;
	int plot_h = h - 2 * margin;
	int bottom = margin + plot_h;
	int top = margin;

	// Widths proportional to (15 - val): slower = wider
	double atk_w = (15 - attack_) + 1.0;
	double dec_w = (15 - decay_) + 1.0;
	double rel_w = (15 - release_) + 1.0;
	double sus_w = 4.0; // fixed sustain hold segment

	double total = atk_w + dec_w + sus_w + rel_w;
	double scale = plot_w / total;

	double x_atk_end = margin + atk_w * scale;
	double x_dec_end = x_atk_end + dec_w * scale;
	double x_sus_end = x_dec_end + sus_w * scale;
	double x_rel_end = x_sus_end + rel_w * scale;

	// Sustain level: 0 = loudest (top), 15 = quietest (bottom)
	double sus_y = top + (sustain_ / 15.0) * plot_h;

	QPainterPath path;
	path.moveTo(margin, bottom);          // start at bottom-left
	path.lineTo(x_atk_end, top);          // attack rise to top
	path.lineTo(x_dec_end, sus_y);        // decay fall to sustain level
	path.lineTo(x_sus_end, sus_y);        // sustain hold
	path.lineTo(x_rel_end, bottom);       // release fall to bottom

	p.setPen(QPen(kEnvelopeColor, 1.5));
	p.drawPath(path);

	// Baseline
	p.setPen(QPen(QColor(80, 80, 80), 0.5, Qt::DotLine));
	p.drawLine(margin, bottom, w - margin, bottom);
}

// --- KSLCurveWidget ---

KSLCurveWidget::KSLCurveWidget(QWidget *parent)
	: QWidget(parent)
{
	setFixedSize(64, 32);
}

void KSLCurveWidget::setKSL(int val) { ksl_ = val; update(); }

QSize KSLCurveWidget::sizeHint() const     { return QSize(64, 32); }
QSize KSLCurveWidget::minimumSizeHint() const { return QSize(48, 24); }

void KSLCurveWidget::paintEvent(QPaintEvent *)
{
	QPainter p(this);
	p.setRenderHint(QPainter::Antialiasing);
	p.fillRect(rect(), palette().color(QPalette::Window));

	int w = width();
	int h = height();
	int margin = 4;
	int plot_w = w - 2 * margin;
	int plot_h = h - 2 * margin;
	int top = margin;
	int bottom = margin + plot_h;

	// Slope: attenuation increases with pitch (left=low, right=high)
	// KSL 0: flat (no rolloff), 1: 1.5 dB/oct, 2: 3.0 dB/oct, 3: 6.0 dB/oct
	// Normalize so KSL 3 reaches full depth
	static const double kSlopes[] = {0.0, 0.25, 0.5, 1.0};
	double slope = kSlopes[ksl_ & 3];

	p.setPen(QPen(kEnvelopeColor, 1.5));
	p.drawLine(margin, top,
	           margin + plot_w, top + static_cast<int>(slope * plot_h));

	// Baseline
	p.setPen(QPen(QColor(80, 80, 80), 0.5, Qt::DotLine));
	p.drawLine(margin, top, w - margin, top);
}
