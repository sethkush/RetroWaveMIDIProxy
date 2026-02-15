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

#include "serial_qt.h"
#include <QString>

namespace retrowave {

QtSerialPort::~QtSerialPort()
{
	close();
}

bool QtSerialPort::open(const std::string &port_name)
{
	close();
	port_.setPortName(QString::fromStdString(port_name));
	port_.setBaudRate(QSerialPort::Baud9600);
	return port_.open(QSerialPort::WriteOnly);
}

void QtSerialPort::close()
{
	if (port_.isOpen())
		port_.close();
}

bool QtSerialPort::is_open() const
{
	return port_.isOpen();
}

bool QtSerialPort::write(const uint8_t *data, size_t len)
{
	QByteArray ba(reinterpret_cast<const char *>(data), static_cast<int>(len));
	return port_.write(ba) == static_cast<qint64>(len);
}

} // namespace retrowave
