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

#include <QApplication>
#include <QPalette>

int main(int argc, char *argv[])
{
	QApplication app(argc, argv);
	app.setApplicationName("RetroWave OPL3 Panel");

	// Dark theme
	QPalette p;
	p.setColor(QPalette::Window, QColor(53, 53, 53));
	p.setColor(QPalette::WindowText, Qt::white);
	p.setColor(QPalette::Base, QColor(35, 35, 35));
	p.setColor(QPalette::AlternateBase, QColor(53, 53, 53));
	p.setColor(QPalette::ToolTipBase, QColor(50, 50, 50));
	p.setColor(QPalette::ToolTipText, Qt::white);
	p.setColor(QPalette::Text, Qt::white);
	p.setColor(QPalette::Button, QColor(53, 53, 53));
	p.setColor(QPalette::ButtonText, Qt::white);
	p.setColor(QPalette::BrightText, Qt::red);
	p.setColor(QPalette::Link, QColor(42, 130, 218));
	p.setColor(QPalette::Highlight, QColor(42, 130, 218));
	p.setColor(QPalette::HighlightedText, Qt::black);
	app.setPalette(p);

	PanelWindow w;
	w.show();
	return app.exec();
}
