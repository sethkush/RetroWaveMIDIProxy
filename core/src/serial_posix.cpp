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

#include <retrowave/serial_posix.h>

#include <cerrno>
#include <cstring>

#include <fcntl.h>
#include <termios.h>
#include <unistd.h>

namespace retrowave {

PosixSerialPort::~PosixSerialPort()
{
	close();
}

bool PosixSerialPort::open(const std::string &port_name)
{
	close();

	fd_ = ::open(port_name.c_str(), O_WRONLY | O_NOCTTY);
	if (fd_ < 0)
		return false;

	struct termios tty{};
	if (tcgetattr(fd_, &tty) != 0) {
		::close(fd_);
		fd_ = -1;
		return false;
	}

	cfsetospeed(&tty, B9600);
	cfsetispeed(&tty, B9600);

	tty.c_cflag = (tty.c_cflag & ~CSIZE) | CS8;
	tty.c_cflag &= ~(PARENB | PARODD | CSTOPB | CRTSCTS);
	tty.c_cflag |= CLOCAL;

	tty.c_iflag &= ~(IXON | IXOFF | IXANY | IGNBRK | BRKINT | PARMRK |
	                  ISTRIP | INLCR | IGNCR | ICRNL);
	tty.c_oflag &= ~OPOST;
	tty.c_lflag &= ~(ECHO | ECHONL | ICANON | ISIG | IEXTEN);

	tty.c_cc[VMIN] = 0;
	tty.c_cc[VTIME] = 0;

	if (tcsetattr(fd_, TCSANOW, &tty) != 0) {
		::close(fd_);
		fd_ = -1;
		return false;
	}

	return true;
}

void PosixSerialPort::close()
{
	if (fd_ >= 0) {
		::close(fd_);
		fd_ = -1;
	}
}

bool PosixSerialPort::is_open() const
{
	return fd_ >= 0;
}

bool PosixSerialPort::write(const uint8_t *data, size_t len)
{
	size_t written = 0;
	while (written < len) {
		ssize_t rc = ::write(fd_, data + written, len - written);
		if (rc < 0) {
			if (errno == EINTR)
				continue;
			return false;
		}
		written += static_cast<size_t>(rc);
	}
	return true;
}

} // namespace retrowave
