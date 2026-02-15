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

#include <retrowave/serial_port.h>
#include <QSerialPort>

namespace retrowave {

class QtSerialPort : public SerialPort {
public:
	QtSerialPort() = default;
	~QtSerialPort() override;

	QtSerialPort(const QtSerialPort &) = delete;
	QtSerialPort &operator=(const QtSerialPort &) = delete;

	bool open(const std::string &port_name) override;
	void close() override;
	bool is_open() const override;
	bool write(const uint8_t *data, size_t len) override;

	QSerialPort *qt_port() { return &port_; }

private:
	QSerialPort port_;
};

} // namespace retrowave
