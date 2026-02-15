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

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <csignal>
#include <getopt.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>

#include <retrowave/midi_router.h>

static Daemon *g_daemon = nullptr;

static void signal_handler(int sig)
{
	if (g_daemon)
		g_daemon->request_stop();
}

static void daemonize(const char *pid_file)
{
	pid_t pid = fork();
	if (pid < 0) {
		perror("fork");
		exit(1);
	}
	if (pid > 0)
		exit(0); // Parent exits

	if (setsid() < 0) {
		perror("setsid");
		exit(1);
	}

	pid = fork();
	if (pid < 0) {
		perror("fork");
		exit(1);
	}
	if (pid > 0)
		exit(0);

	// Redirect stdio
	int devnull = open("/dev/null", O_RDWR);
	if (devnull >= 0) {
		dup2(devnull, STDIN_FILENO);
		dup2(devnull, STDOUT_FILENO);
		dup2(devnull, STDERR_FILENO);
		if (devnull > 2)
			close(devnull);
	}

	// Write PID file
	if (pid_file) {
		FILE *f = fopen(pid_file, "w");
		if (f) {
			fprintf(f, "%d\n", getpid());
			fclose(f);
		}
	}
}

static void print_usage(const char *argv0)
{
	fprintf(stderr,
		"Usage: %s [options]\n"
		"\n"
		"Options:\n"
		"  -s, --serial PORT     Serial port device (e.g. /dev/ttyUSB0)\n"
		"  -m, --midi PORT       MIDI input port number, or 'virtual' (default: virtual)\n"
		"  -M, --mode MODE       Mode: 'bank' or 'direct' (default: bank)\n"
		"  -b, --bank ID         Bank number (default: 58)\n"
		"  -B, --bank-file PATH  Bank file path (WOPL format)\n"
		"  -v, --volume-model N  Volume model (0-11, default: 0/AUTO)\n"
		"  -D, --daemon          Run as daemon (background)\n"
		"  -P, --pid-file PATH   PID file path (with --daemon)\n"
		"      --list-midi       List available MIDI ports\n"
		"      --list-serial     List available serial ports\n"
		"      --list-banks      List available banks\n"
		"  -h, --help            Show this help\n"
		"\n"
		"Examples:\n"
		"  %s -s /dev/ttyUSB0 -m virtual -M bank -b 58\n"
		"  %s -s /dev/ttyUSB0 -m 1 -M direct\n"
		"  %s -s /dev/ttyUSB0 --daemon -P /run/retrowave-midi.pid\n",
		argv0, argv0, argv0, argv0);
}

enum LongOpts {
	OPT_LIST_MIDI = 256,
	OPT_LIST_SERIAL,
	OPT_LIST_BANKS,
};

int main(int argc, char *argv[])
{
	static const struct option long_options[] = {
		{"serial",       required_argument, nullptr, 's'},
		{"midi",         required_argument, nullptr, 'm'},
		{"mode",         required_argument, nullptr, 'M'},
		{"bank",         required_argument, nullptr, 'b'},
		{"bank-file",    required_argument, nullptr, 'B'},
		{"volume-model", required_argument, nullptr, 'v'},
		{"daemon",       no_argument,       nullptr, 'D'},
		{"pid-file",     required_argument, nullptr, 'P'},
		{"list-midi",    no_argument,       nullptr, OPT_LIST_MIDI},
		{"list-serial",  no_argument,       nullptr, OPT_LIST_SERIAL},
		{"list-banks",   no_argument,       nullptr, OPT_LIST_BANKS},
		{"help",         no_argument,       nullptr, 'h'},
		{nullptr,        0,                 nullptr, 0},
	};

	Daemon daemon;
	bool do_daemon = false;
	const char *pid_file = nullptr;

	int opt;
	while ((opt = getopt_long(argc, argv, "s:m:M:b:B:v:DP:h", long_options, nullptr)) != -1) {
		switch (opt) {
		case 's':
			daemon.set_serial_port(optarg);
			break;
		case 'm':
			if (strcmp(optarg, "virtual") == 0) {
				daemon.set_midi_virtual(true);
			} else {
				daemon.set_midi_port(atoi(optarg));
				daemon.set_midi_virtual(false);
			}
			break;
		case 'M':
			if (strcmp(optarg, "direct") == 0)
				daemon.set_mode(retrowave::RoutingMode::Direct);
			else
				daemon.set_mode(retrowave::RoutingMode::Bank);
			break;
		case 'b':
			daemon.set_bank_id(atoi(optarg));
			break;
		case 'B':
			daemon.set_bank_path(optarg);
			break;
		case 'v':
			daemon.set_volume_model(atoi(optarg));
			break;
		case 'D':
			do_daemon = true;
			break;
		case 'P':
			pid_file = optarg;
			break;
		case OPT_LIST_MIDI:
			Daemon::list_midi_ports();
			return 0;
		case OPT_LIST_SERIAL:
			Daemon::list_serial_ports();
			return 0;
		case OPT_LIST_BANKS:
			Daemon::list_banks();
			return 0;
		case 'h':
		default:
			print_usage(argv[0]);
			return (opt == 'h') ? 0 : 1;
		}
	}

	if (do_daemon)
		daemonize(pid_file);

	g_daemon = &daemon;
	signal(SIGINT, signal_handler);
	signal(SIGTERM, signal_handler);

	return daemon.run();
}
