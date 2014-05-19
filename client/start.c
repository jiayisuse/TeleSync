#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <pthread.h>
#include <signal.h>
#include <unistd.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <net/if.h>

#include <debug.h>
#include <consts.h>
#include <packet_def.h>
#include "start.h"
#include "packet.h"
#include "file_monitor.h"

uint32_t my_ip;
uint32_t serv_ip;

static pthread_t file_monitor_id;


static void get_my_ip(char *if_name)
{
	int fd;
	struct ifreq ifr;

	fd = socket(AF_INET, SOCK_DGRAM, 0);
	ifr.ifr_addr.sa_family = AF_INET;
	strncpy(ifr.ifr_name, if_name, IFNAMSIZ - 1);
	ioctl(fd, SIOCGIFADDR, &ifr);
	close(fd);

	memcpy(&my_ip, &(((struct sockaddr_in *)&ifr.ifr_addr)->sin_addr),
			sizeof(my_ip));
}

static inline void get_server_ip(char *host_name)
{
	struct hostent *host_info;
	host_info = gethostbyname(host_name);
	memcpy(&serv_ip, host_info->h_addr_list[0], host_info->h_length);
	_debug("server: '%s' %s\n", host_name, print_ip(serv_ip));
}

static int parse_conf_files(struct client_conf_info *conf)
{
	FILE *fp;
	char cmd[MAX_NAME_LEN], arg[MAX_LINE];
	int n = 0;

	fp = fopen(CLIENT_CONF_FILE, "r");
	if (fp == NULL) {
		_error("open '%s' failed\nWe need it!\n", CLIENT_CONF_FILE);
		return -1; 
	}   

	/* get configure info */
	while (fscanf(fp, "%[^:]: %[^\n]\n", cmd, arg) != EOF) {
		if (strcmp(cmd, "tracker") == 0)
			strcpy(conf->tracker_host, arg);
		else if (strcmp(cmd, "device") == 0)
			strcpy(conf->device_name, arg);
		else {
			_error("'%s': Bad configure cmd\n", cmd);
			fclose(fp);
			return -1;
		}
	}
	fclose(fp);

	/* get target dirs */
	fp = fopen(CLIENT_TARGET_FILE, "r");
	if (fp == NULL) {
		_error("open '%s' failed\nWe need it!\n", CLIENT_TARGET_FILE);
		return -1; 
	}   
	while (fscanf(fp, "%s", arg) != EOF) {
		if (arg[strlen(arg) - 1] == '/')
			arg[strlen(arg) - 1] = '\0';

		conf->target_dirs[n] = calloc(1, strlen(arg) + 1); 
		if (conf->target_dirs[n] == NULL) {
			_error("calloc failed\n");
			fclose(fp);
			return -1; 
		}   
		strcpy(conf->target_dirs[n], arg);
		n++;
	}
	conf->target_n = n;
	fclose(fp);

	return 0;
}

static int connect_tracker()
{
	struct sockaddr_in servaddr;
	int fd = socket(AF_INET, SOCK_STREAM, 0);

	if (fd < 0) {
		_error("socket create failed\n");
		return -1;
	}
	bzero(&servaddr, sizeof(struct sockaddr_in));
	servaddr.sin_family = AF_INET;
	servaddr.sin_addr.s_addr = serv_ip;
	servaddr.sin_port = htons(TRACKER_RECEIVER_PORT);
	if (connect(fd, (struct sockaddr *) &servaddr, sizeof(servaddr)) < 0) {
		perror("connect() error");
		return -1;
	}

	return fd;
}

static inline int register_to_tracker(int conn)
{
	struct ptot_packet ptot_pkt;
	struct ttop_packet ttop_pkt;
	int ret;

	ptot_packet_init(&ptot_pkt, PEER_REGISTER);
	ret = send_ptot_packet(conn, &ptot_pkt);
	if (ret < 0)
		return ret;
	
	ret = recv_ttop_packet(conn, &ttop_pkt);
	if (ret < 0)
		return ret;
	if (ttop_pkt.hdr.type != TRACKER_ACCEPT)
		return -1;

	return 0;
}

static void client_cleanup()
{
	pthread_cancel(file_monitor_id);
	exit(0);
}

void client_start()
{
	struct client_thread_arg targ;

	bzero(&targ, sizeof(struct client_thread_arg));
	if (parse_conf_files(&targ.conf) != 0) {
		_error("parsing '%s' failed\n", CLIENT_CONF_FILE);
		return;
	}
	get_my_ip(targ.conf.device_name);
	get_server_ip(targ.conf.tracker_host);

	/* connect to tracker */
	targ.conn = connect_tracker();
	if (targ.conn < 0)
		return;

	/* register to tracker */
	if (register_to_tracker(targ.conn) < 0) {
		_error("register to tacker failed\n");
		return;
	}
	_debug("Have registered to tracker\n");

	signal(SIGINT, client_cleanup);

	if (pthread_create(&file_monitor_id, NULL, file_monitor_task, &targ) < 0) {
		_error("Creating file monitor task failed\n");
		return;
	}

	pthread_wait_init(&targ.wait);
	pthread_wait_for(&targ.wait);
	if (targ.wait.status == THREAD_FAILED) {
		_error("file_monitor_task failed\n");
		return;
	}
	_debug("file_monitor_task OK\n");

	pthread_exit(0);
}
