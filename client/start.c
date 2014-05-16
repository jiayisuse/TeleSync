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
#include "start.h"
#include "file_monitor.h"

uint32_t my_ip;
static pthread_t file_monitor_id;

static void get_my_ip(char *if_name)
{
	int fd;
	struct ifreq ifr;

	fd = socket(AF_INET, SOCK_DGRAM, 0);
	ifr.ifr_addr.sa_family = AF_INET;
	strncpy(ifr.ifr_name, "eth1", IFNAMSIZ - 1);
	ioctl(fd, SIOCGIFADDR, &ifr);
	close(fd);

	memcpy(&my_ip, &(((struct sockaddr_in *)&ifr.ifr_addr)->sin_addr),
			sizeof(my_ip));
}

static int parse_conf_files(struct client_conf_info *conf)
{
	FILE *fp;
	char cmd[MAX_NAME_LEN], arg[MAX_LINE];
	int n = 0;

	fp = fopen(CONF_FILE, "r");
	if (fp == NULL) {
		_error("open '%s' failed\nWe need it!\n", CONF_FILE);
		return -1; 
	}   

	/* get configure info */
	while (fscanf(fp, "%[^:]:%[^\n]\n", cmd, arg) != EOF) {
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
	fp = fopen(TARGET_FILE, "r");
	if (fp == NULL) {
		_error("open '%s' failed\nWe need it!\n", TARGET_FILE);
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

static void client_cleanup()
{
	pthread_cancel(file_monitor_id);
}


void client_start()
{
	struct thread_arg targ;

	if (parse_conf_files(&targ.conf) != 0) {
		_error("parsing '%s' failed\n", CONF_FILE);
		return;
	}
	get_my_ip(targ.conf.device_name);

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
