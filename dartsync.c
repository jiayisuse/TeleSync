#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>

#include <start.h>
#include <debug.h>
#include <packet_def.h>
#include <file_table.h>
#include <trans_file_table.h>

int debug = 0;

enum start_mode {
	CLIENT,
	SERVER,
	NONE
};

static const char short_options[] = "dm:h";

static const struct option long_options[] = {
	{ "mode", required_argument, NULL, 'm' },
	{ "debug", no_argument, NULL, 'd' },
	{ "help", no_argument, NULL, 'h' },
	{ 0, 0, 0, 0 }
};

static void usage(FILE *fp, char **argv)
{
	fprintf(fp,
		"Usage: %s [-m client|server]\n\n"
		"Options:\n"
		"-m | --mode          client|server\n"
		"-d | --open debug    print debug info\n"
		"-h | --help          Print this message\n\n"
		, argv[0]);
}

int main(int argc, char **argv)
{
	enum start_mode mode = NONE;

	while (1) {
		int index, c;

		c = getopt_long(argc, argv, short_options, long_options, &index);
		if (c == -1)
			break;

		switch (c) {
		case 0:
			break;

		case 'd':
			debug = 1;
			break;

		case 'm':
			if (strcmp(optarg, "server") == 0)
				mode = SERVER;
			else if (strcmp(optarg, "client") == 0)
				mode = CLIENT;
			else {
				usage(stdout, argv);
				exit(EXIT_SUCCESS);
			}
			break;

		case 'h':
			usage(stdout, argv);
			exit(EXIT_SUCCESS);

		default:
			usage(stderr, argv);
			exit(EXIT_FAILURE);
		}
	}

	switch (mode) {
	case CLIENT:
		client_start();
		break;
	case SERVER:
		server_start();
		break;
	case NONE:
		usage(stderr, argv);
		exit(EXIT_FAILURE);
	}

	return 0;
}
