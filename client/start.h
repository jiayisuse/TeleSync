#ifndef CLIENT_START_H
#define CLIENT_START_H

#include <stdint.h>

#include <consts.h>
#include <utility/pthread_wait.h>

#define CLIENT_CONF_FILE	"./client/client.conf"
#define CLIENT_TARGET_FILE	"./client/target.conf"

struct client_conf_info {
	char tracker_host[MAX_NAME_LEN];
	char device_name[MAX_NAME_LEN];
	int target_n;
	char *target_dirs[MAX_TARGET_DIR];
};

struct client_thread_arg {
	struct client_conf_info conf;
	struct pthread_wait_t wait;
	int conn;
};

void client_start();
int client_tcp_listen(uint16_t port);

#endif
