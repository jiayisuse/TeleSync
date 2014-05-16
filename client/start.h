#ifndef CLIENT_START_H
#define CLIENT_START_H

#include <consts.h>
#include <utility/pthread_wait.h>

#define CONF_FILE	"./client/client.conf"
#define TARGET_FILE	"./client/target.conf"

struct client_conf_info {
	char tracker_host[MAX_NAME_LEN];
	char device_name[MAX_NAME_LEN];
	int target_n;
	char *target_dirs[MAX_TARGET_DIR];
};

struct thread_arg {
	struct client_conf_info conf;
	struct pthread_wait_t wait;
	int conn;
};

void client_start();

#endif
