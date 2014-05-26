#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <unistd.h>
#include <netdb.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/file.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <net/if.h>

#include <debug.h>
#include <consts.h>
#include <packet_def.h>
#include <file_table.h>
#include "start.h"
#include "packet.h"
#include "file_monitor.h"
#include "download.h"

uint32_t my_ip;
uint32_t serv_ip;
struct ttop_control_info ctr_info;
struct file_table ft;

static pthread_t file_monitor_tid;
static pthread_t keep_alive_tid;
static pthread_t ttop_receiver_tid;
static pthread_t ptop_listening_tid;
static struct client_thread_arg targ;

static uint16_t p2p_ports[MAX_P2P_PORT] = { 0 };
static pthread_mutex_t p2p_port_mutex = PTHREAD_MUTEX_INITIALIZER;


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
	_debug("server: '%s' %s\n", host_name, ip_string(serv_ip));
}

static inline uint64_t get_current_timestamp()
{
	struct timeval tv;
	gettimeofday(&tv, NULL);
	return GET_TIMESTAMP(&tv);
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

static int16_t get_free_p2p_port()
{
	int i = 0;
	int16_t port = -1;

	pthread_mutex_lock(&p2p_port_mutex);
	for (i = 0; i < MAX_P2P_PORT; i++) {
		if (p2p_ports[i] == 0) {
			p2p_ports[i] = 1;
			port = i + BASE_P2P_PORT;
			break;
		}
	}
	pthread_mutex_unlock(&p2p_port_mutex);

	return port;
}

static void put_p2p_port(uint16_t port)
{
	pthread_mutex_lock(&p2p_port_mutex);
	p2p_ports[port - BASE_P2P_PORT] = 0;
	pthread_mutex_unlock(&p2p_port_mutex);
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

static int register_to_tracker(int conn)
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

	memcpy(&ctr_info, ttop_pkt.data, ttop_pkt.hdr.data_len);

	return 0;
}

/**
 * peer delete a file from file system
 * @logic_name: the logic name of the file
 */
static void file_delete(struct trans_file_entry *te)
{
	char *file_sys_name;
	struct monitor_target *target;

	if (te == NULL)
		return;
	
	target = file_monitor_block(te->name);
	if (target == NULL) {
		_error("Could NOT find the target with '%s'\n", te->name);
		return;
	}
	file_sys_name = monitor_get_sys_name(te->name, target);
	if (file_sys_name == NULL) {
		_error("Could NOT find sys name for '%s'\n", te->name);
		goto out;
	}

	if (te->file_type == DIRECTORY) {
		/* TODO: rm directory */
	} else
		unlink(file_sys_name);

	free(file_sys_name);
out:
	file_monitor_unblock(target);
}

static void notify_tracker_add_me(struct file_entry *fe)
{
	struct ptot_packet *pkt;
	struct trans_file_table *tft;
	struct trans_file_entry *te;

	pkt = calloc(1, sizeof(*pkt));
	if (pkt == NULL) {
		_error("pkt alloc failed\n");
		goto out;
	}
	tft = calloc(1, sizeof(*tft));
	if (tft == NULL) {
		_error("tft alloc failed\n");
		goto free_pkt;
	}

	te = tft->entries + tft->n;
	pthread_rwlock_rdlock(&fe->rwlock);
	strcpy(te->name, fe->name);
	te->timestamp = fe->timestamp;
	pthread_rwlock_unlock(&fe->rwlock);
	te->file_type = fe->type;
	te->op_type = FILE_MODIFY;
	te->owners[te->owner_n].ip = my_ip;
	te->owners[te->owner_n].port = P2P_PORT;
	te->owner_n++;
	tft->n++;

	ptot_packet_init(pkt, PEER_FILE_UPDATE);
	ptot_packet_fill(pkt, tft, trans_table_len(tft));
	if (send_ptot_packet(targ.conn, pkt) < 0)
		_error("send ptot packet failed\n");

free_pkt:
	free(pkt);
out:
	return;
}

/**
 * peer download a file from some owners
 * @arg: file entry related to the file to be downloaded
 * @return: return 1 if succeeds, -1 if fails
 */
static void *ptop_download_task(void *arg)
{
	struct file_entry *fe = arg;
	char *logic_name = fe->name;
	char *sys_name;
	struct monitor_target *target;
	long int ret = 0;

	/* block file monitor */
	target = file_monitor_block(logic_name);
	if (target == NULL) {
		_error("Could NOT find the target with '%s'\n", logic_name);
		ret = -1;
		goto out;
	}

	/* get file's sys name */
	sys_name = monitor_get_sys_name(logic_name, target);
	if (sys_name == NULL) {
		_error("Could NOT find sys name for '%s'\n", logic_name);
		ret = -1;
		goto unblock_file_monitor;
	}

	/* TODO: to uncomment the unlink(), please make sure your
	   download function works well */
	/* unlink(sys_name); */
	if (fe->type == DIRECTORY) {
		ret = file_monitor_mkdir(sys_name, logic_name);
		if (ret == 0)
			file_change_modtime(sys_name, fe->timestamp);
	} else {
		ret = do_download(fe, sys_name);
		if (ret == 0)
			file_change_modtime(sys_name, fe->timestamp);
	}
	
	if (ret == 0)
		notify_tracker_add_me(fe);

	struct stat st;
	stat(sys_name, &st);
	_debug("????????? %lu\n", (long unsigned int)st.st_mtime);

	free(sys_name);
unblock_file_monitor:
	file_monitor_unblock(target);
out:
	pthread_exit((void *)ret);
}

static void do_upload(int listenfd, const char *sys_name)
{
	struct sockaddr_in cliaddr;
	socklen_t clilen;
	int file_fd, conn;
	struct p2p_packet *pkt;
	char *piece_buf;
	int piece_id, ret_len;

	file_fd = open(sys_name, O_RDWR);
	if (file_fd < 0) {
		_error("'%s' open failed\n", sys_name);
		return;
	}

	pkt = calloc(1, sizeof(struct p2p_packet));
	if (pkt == NULL) {
		_error("pkt alloc failed\n");
		goto close_fd;
	}
	piece_buf = calloc(1, ctr_info.piece_len);
	if (piece_buf == NULL) {
		_error("piece buf alloc failed\n");
		goto free_pkt;
	}

do_agin:
	conn = accept(listenfd, (struct sockaddr*)&cliaddr, &clilen);
	if (conn < 0) {
		if (errno == EINTR)
			goto do_agin;
		else {
			perror("accept error");
			goto free_piece_buf;
		}   
	}   

	
	while (recv_p2p_packet(conn, pkt) > 0) {
		if (pkt->type != P2P_PIECE_REQ) {
			_error("is not P2P_PIECE_REQ\n");
			goto out;
		}

		memcpy(&piece_id, pkt->data, pkt->data_len);
		_debug("piece_id = %d\n", piece_id);

		flock(file_fd, LOCK_EX);
		lseek(file_fd, piece_id * ctr_info.piece_len, SEEK_SET);
		ret_len = read(file_fd, piece_buf, ctr_info.piece_len);
		flock(file_fd, LOCK_UN);
		if (ret_len < 0) {
			_error("'%s' read failed\n", sys_name);
			goto out;
		}
		write(conn, piece_buf, ret_len);
	}

out:
	close(conn);
free_piece_buf:
	free(piece_buf);
free_pkt:
	free(pkt);
close_fd:
	close(file_fd);
	return;
}

/**
 * peer upload a file to another peer
 * @arg: socket description for communicating with the particular peer
 * @return: return 1 if succeeds, -1 if fails
 */
static void *ptop_upload_task(void *arg)
{
	long int p2p_conn = (long int)arg;
	struct p2p_packet pkt;
	char logic_name[MAX_NAME_LEN];
	char *sys_name;
	struct stat st;
	uint16_t new_port;
	int listenfd;

	/* TODO: start your upload work here */
	while (recv_p2p_packet(p2p_conn, &pkt) > 0) {

		if (pkt.type == P2P_FILE_LEN_REQ || pkt.type == P2P_PORT_REQ) {
			memcpy(logic_name, pkt.data, pkt.data_len);
			sys_name = get_sys_name(logic_name);
			if (sys_name == NULL) {
				_error("can't get sys name for '%s'\n",
						logic_name);
				goto close_out;
			}
		}

		switch (pkt.type) {
		case P2P_FILE_LEN_REQ:
			_debug("{ P2P_FILE_LEN_REQ }\n");

			if (stat(sys_name, &st) < 0) {
				_error("stat error for '%s'\n", logic_name);
				goto free_sys_name;
			}
			p2p_packet_init(&pkt, P2P_FILE_LEN_RET);
			p2p_packet_fill(&pkt, &st.st_size, sizeof(st.st_size));
			if (send_p2p_packet(p2p_conn, &pkt) < 0) {
				_error("P2P_FILE_LEN_RET send failed\n");
				goto free_sys_name;
			}

			_debug("\t'%s(%u bytes)'\n",
					sys_name, (unsigned int)st.st_size);
			break;

		case P2P_PORT_REQ:
			_debug("{ P2P_PORT_REQ }\n");

			new_port = get_free_p2p_port();
			p2p_packet_init(&pkt, P2P_PORT_RET);
			p2p_packet_fill(&pkt, &new_port, sizeof(new_port));
			if (send_p2p_packet(p2p_conn, &pkt) < 0) {
				_error("P2P_FILE_PORT_RET send failed\n");
				goto free_sys_name;
			}

			_debug("\t new port = %u\n", new_port);

			listenfd = client_tcp_listen(new_port);
			if (listenfd < 0) {
				_error("listen fd create failed\n");
				goto free_sys_name;
			}

			do_upload(listenfd, sys_name);
			close(listenfd);
			put_p2p_port(new_port);

			break;

		default:
			break;
		}
	}

free_sys_name:
	free(sys_name);
close_out:
	close(p2p_conn);
	_leave();
	pthread_exit((void *)0);
}


static void file_table_sync(struct file_table *ft, struct trans_file_table *tft)
{
	struct file_entry *fe;
	pthread_t new_tid;
	int i;

	for (i = 0; i < tft->n; i++) {
		struct trans_file_entry *te = tft->entries + i;
		fe = file_table_find(ft, te);
		if (fe == NULL) {
			fe = file_table_add(ft, te);
			pthread_create(&new_tid, NULL, ptop_download_task, fe);
		} else {
			peer_id_list_replace(fe, te);
			if (te->timestamp > fe->timestamp)
				pthread_create(&new_tid, NULL,
						ptop_download_task, fe);
		}
	}
}

static int sync_files(int conn, char **target, int n)
{
	struct trans_file_table tft;
	struct ptot_packet ptot_pkt;
	struct ttop_packet ttop_pkt;
	int ret;

	/* get local file table */
	file_table_init(&ft);
	ret = get_file_table(&ft, target, n);
	if (ret != 0) {
		_error("getting file table failed\n");
		return ret;
	}
	file_table_print(&ft);

	/* trans local file table to trans file table */
	trans_table_fill_from(&tft, &ft);

	/* send sync request to tracker */
	ptot_packet_init(&ptot_pkt, PEER_SYNC);
	ptot_packet_fill(&ptot_pkt, &tft, trans_table_len(&tft));
	ret = send_ptot_packet(conn, &ptot_pkt);
	if (ret < 0) {
		_error("send packet failed\n");
		return ret;
	}

	/* get file_table which contains the items that we need to update */
	ret = recv_ttop_packet(conn, &ttop_pkt);
	if (ret < 0) {
		_error("recv ttop packet failed\n");
		return ret;
	}
	if (ttop_pkt.hdr.type != TRACKER_SYNC) {
		_error("packet type is not correct\n");
		return -1;
	}

	_debug("NEED TO SYNC FILE LOCALLY!!!\n");
	memcpy(&tft, ttop_pkt.data, ttop_pkt.hdr.data_len);
	file_table_sync(&ft, &tft);

	return 0;
}

/**
 * peer handles a single broadcast entry received from tracker
 * @arg: trans file entry related to the file to be downloaded
 * @return: return 1 if succeeds, -1 if fails
 */
void *broadcast_entry_handler_task(void *arg)
{
	struct trans_file_entry *te = arg;
	struct file_entry *fe = NULL;
	pthread_t new_tid;

	_enter();

	if (te == NULL) {
		_error("trans_file_entry is NULL\n");
		pthread_exit((void *)-1);
	}

	switch (te->op_type) {
	case FILE_ADD:
		_debug("{ FILE_ADD } '%s'\n", te->name);
		/* for now, assume there's no confliction */
		fe = file_table_find(&ft, te);
		if (fe != NULL) {
			_debug("\tOLD File\n");
			peer_id_list_replace(fe, te);
			if (te->timestamp > fe->timestamp) {
				/* create a file add task to download the file */
				pthread_create(&new_tid, NULL,
						ptop_download_task, fe);
			}
		} else {
			_debug("\tNEW File\n");
			fe = file_table_add(&ft, te);
			pthread_create(&new_tid, NULL, ptop_download_task, fe);
		}

		break;

	case FILE_DELETE:
		_debug("{ FILE_DELETE } '%s'\n", te->name);

		if (file_table_delete(&ft, te) < 0)
			_error("\tfail to delete file entry\n");

		/* delete the file */
		file_delete(te);

		break;

	case FILE_MODIFY:
		_debug("{ FILE_MODIFY } '%s'\n", te->name);
		fe = file_table_find(&ft, te);
		if (fe == NULL) {
			_debug("\t'%s' not exists, conflict!\n", te->name);
			fe = file_table_add(&ft, te);
			/* create a file add task to download the file */
			pthread_create(&new_tid, NULL, ptop_download_task, fe);
		} else {
			peer_id_list_replace(fe, te);
			if (te->timestamp > fe->timestamp)
				/* create a file add task to download the file */
				pthread_create(&new_tid, NULL,
						ptop_download_task, fe);
		}
		/*
		file_table_print(&ft);
		*/

		break;

	case FILE_NONE:
		_debug("{ FILE_NONE }\n");
		break;

	default:
		break;
	}

	_leave();
	free(te);
	pthread_exit(0);
}

static void client_cleanup()
{
	pthread_cancel(file_monitor_tid);
	pthread_cancel(keep_alive_tid);
	pthread_cancel(ttop_receiver_tid);
	file_table_destroy(&ft);
	exit(0);
}

static void *keep_alive_task(void *arg)
{
	struct client_thread_arg *targ = arg;
	int conn = targ->conn;
	uint64_t timestamp;
	struct ptot_packet pkt;

	ptot_packet_init(&pkt, PEER_KEEP_ALIVE);
	timestamp = get_current_timestamp();
	ptot_packet_fill(&pkt, &timestamp, sizeof(timestamp));

	while (1) {
		usleep(ctr_info.interval);
		if (send_ptot_packet(conn, &pkt) < 0) {
			_debug("Server has been down\n");
			break;
		}
	}

	pthread_exit((void *)0);
}

static void *ttop_receiver_task(void *arg)
{
	pthread_t broadcast_entry_handler_tid;
	struct client_thread_arg *targ = arg;
	int conn = targ->conn;
	struct ttop_packet pkt;
	struct trans_file_table tft;
	int i;

	pthread_wait_notify(&targ->wait, THREAD_RUNNING);
	while (recv_ttop_packet(conn, &pkt) > 0) {
		switch (pkt.hdr.type) {
		case TRACKER_BROADCAST:
			_debug("[ TRACKER_BROADCAST ] from tracker\n");
			memcpy(&tft, pkt.data, pkt.hdr.data_len);

			_debug("\tbroadcast entry number: %d\n", tft.n);
			for (i = 0; i < tft.n; i++) {
				struct trans_file_entry *te = calloc(1,
								sizeof(*te));
				if (te == NULL) {
					_error("te alloc failed\n");
					break;
				}
				*te = tft.entries[i];
				pthread_create(&broadcast_entry_handler_tid,
						NULL, broadcast_entry_handler_task,
						te);
			}
			break;

		case TRACKER_SYNC:
			_debug("[ TRACKER_SYNC ] from tracker\n");
			break;

		case TRACKER_CLOSE:
			_debug("[ TRACKER_CLOSE ] from tracker\n");
			break;
		}
	}

	pthread_exit((void *)0);
}

/**
 * open and listen on port P2P_PORT for other peer to connect
 * @interval: number of micro-seconds that the function waits for
 * @return: no return value
 */
static void *ptop_listening_task(void *arg)
{
	struct sockaddr_in cliaddr;
	socklen_t cliaddr_len;
	pthread_t ptop_upload_tid;
	long int listenfd, ptop_conn;

	_enter();

	listenfd = client_tcp_listen(P2P_PORT);
	if (listenfd < 0)
		return 0;

	while(1) {
		ptop_conn = accept(listenfd, (struct sockaddr*)&cliaddr,
				   &cliaddr_len);
		if (ptop_conn < 0) {
			_error("fail to accept a p2p connection request\n");
			continue;
		}

		/* create a ptop_upload_task to handle this connection */
		pthread_create(&ptop_upload_tid, NULL, ptop_upload_task,
				(void *)ptop_conn);
	}

	pthread_exit((void *)0);
}

void client_start()
{
	bzero(&targ, sizeof(struct client_thread_arg));
	if (parse_conf_files(&targ.conf) != 0) {
		_error("parsing '%s' failed\n", CLIENT_CONF_FILE);
		return;
	}
	get_my_ip(targ.conf.device_name);
	get_server_ip(targ.conf.tracker_host);

	signal(SIGINT, client_cleanup);
	signal(SIGPIPE, client_cleanup);

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

	/* create keep alive thread */
	if (pthread_create(&keep_alive_tid, NULL, keep_alive_task, &targ) < 0) {
		_error("Creating file monitor task failed\n");
		return;
	}

	/* create file monitor thread */
	if (pthread_create(&file_monitor_tid, NULL, file_monitor_task, &targ) < 0) {
		pthread_cancel(keep_alive_tid);
		_error("Creating file monitor task failed\n");
		return;
	}
	/* wait until the file monitor thread init successfully */
	pthread_wait_init(&targ.wait);
	pthread_wait_for(&targ.wait);
	if (targ.wait.status == THREAD_FAILED) {
		_error("file_monitor_task failed\n");
		return;
	}
	_debug("file_monitor_task OK\n");

	/* sync with tracker */
	if (sync_files(targ.conn, targ.conf.target_dirs, targ.conf.target_n) < 0) {
		_error("sync with tracker failed\n");
		return;
	}
	
	/* create receiver thread to avoid destroy ctr+c handler */
	if (pthread_create(&ttop_receiver_tid, NULL, ttop_receiver_task, &targ) < 0) {
		pthread_cancel(keep_alive_tid);
		pthread_cancel(file_monitor_tid);
		_error("Creating receive ttop task failed\n");
		return;
	}
    
	/* create receive p2p request thread */
	if (pthread_create(&ptop_listening_tid, NULL,
				ptop_listening_task, (void *)0) < 0) {
		pthread_cancel(keep_alive_tid);
		pthread_cancel(file_monitor_tid);
		pthread_cancel(ttop_receiver_tid);
		_error("Creating receive ptop task failed\n");
		return;
	}
	pthread_wait_init(&targ.wait);
	pthread_wait_for(&targ.wait);

	pthread_exit(0);
}
