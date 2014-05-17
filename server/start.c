#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdint.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>

#include <consts.h>
#include <packet_def.h>
#include <hash.h>
#include "start.h"
#include "debug.h"
#include "file_table.h"

static struct ttop_control_info control;


static int parse_conf_file(struct ttop_control_info *control)
{
	FILE *fp;
	char cmd[MAX_NAME_LEN], arg[MAX_LINE];

	fp = fopen(SERVER_CONF_FILE, "r");
	if (fp == NULL) {
		_error("open '%s' failed\nWe need it!\n", SERVER_CONF_FILE);
		return -1; 
	}   

	/* get configure info */
	while (fscanf(fp, "%[^:]: %[^\n]\n", cmd, arg) != EOF) {
		if (strcmp(cmd, "interval") == 0)
			control->interval = atoi(arg);
		else if (strcmp(cmd, "piece_length") == 0)
			control->piece_len = atoi(arg);
		else {
			_error("'%s': Bad configure cmd\n", cmd);
			fclose(fp);
			return -1;
		}
	}

	fclose(fp);
	return 0;
}

void server_start()
{
	int listenfd, connfd;
	struct sockaddr_in cliaddr;
	socklen_t clilen;
	struct file_table table;

	/* get tracker to peer control information */
	if (parse_conf_file(&control) != 0)
		return;
	_debug("interval = %d, piece_len = %d\n", control.interval,
			control.piece_len);

	/* init file table */
	file_table_init(&table);

	/* accept peer connection */
	listenfd = server_tcp_listen(TRACKER_RECEIVER_PORT);
	if (listenfd < 0)
		return;
	while (1) {
		connfd = accept(listenfd, (struct sockaddr *)&cliaddr, &clilen);
		if (connfd < 0) {
			if (errno == EINTR)
				continue;
			else {
				perror("accept error");
				break;
			}
		}
		//pthread_create()
	}

	return;
}

int server_tcp_listen(uint16_t port)
{
	int listenfd, on = 1;
	struct sockaddr_in servaddr;

	if ((listenfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
		perror("socket() error");
		return -1; 
	}   

	if (setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on))) {
		perror("setsockopt() error");
		return -1; 
	}   

	bzero(&servaddr, sizeof(servaddr));
	servaddr.sin_family = AF_INET;
	servaddr.sin_addr.s_addr = htons(INADDR_ANY);
	servaddr.sin_port = htons(port);

	if (bind(listenfd, (struct sockaddr *)&servaddr, sizeof(servaddr))) {
		perror("bind() error");
		return -1; 
	}   

	listen(listenfd, MAX_CONNECTIONS);

	return listenfd;
}
