#ifndef SERVER_START_H
#define SERVER_START_H

#include <stdint.h>

#include <consts.h>
#include <packet_def.h>
#include <utility/pthread_wait.h>

#define SERVER_CONF_FILE	"./server/server.conf"


void server_start();
int server_tcp_listen(uint16_t port);

#endif
