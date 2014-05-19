#ifndef SERVER_START_H
#define SERVER_START_H

#include <stdint.h>

#include <consts.h>
#include <trans_file_table.h>
#include <utility/pthread_wait.h>

#define SERVER_CONF_FILE	"./server/server.conf"

struct server_thread_arg {
	int conn;
	pthread_t tid;
	struct peer_id peerid;	/* peer ip and peer port */
};

void server_start();

#endif
