#define _BSD_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdint.h>
#include <unistd.h>
#include <errno.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>

#include <consts.h>
#include <packet_def.h>
#include <hash.h>
#include <debug.h>
#include "start.h"
#include "file_table.h"
#include "peer_table.h"
#include "packet.h"

static struct file_table ft;
static struct peer_table pt;
static struct ttop_control_info ctr_info;


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

static void broadcast_update(struct peer_table *pt, struct trans_file_table *tft)
{
	struct list_head *pos;
	struct ttop_packet pkt;

	_enter();

	if (pt == NULL || tft == NULL)
		return;

	pkt.hdr.type = TRACKER_BROADCAST;
	pkt.hdr.data_len = trans_table_len(tft);
	memcpy(pkt.data, &ctr_info, pkt.hdr.data_len);

	/* send ACCEPT back to peer */
	list_for_each(pos, &pt->peer_head) {
		struct peer_entry *pe = list_entry(pos, struct peer_entry, l);
		if (send_ttop_packet(pe->conn, &pkt) < 0)
			_error("fail to send BROADCAST to peer(%u)\n",
					pe->peerid.ip);
	}

	_leave();
}

/* checks every INTERVAL time to see if a peer is still alive,
 * if the peer is dead, terminates the receiver_handler task for this peer and
 * terminates peer_check_alive_task thread itself
 * @arg: a server_thread_arg structure
 */
void *peer_check_alive_task(void *arg)
{
	/* extracs arguments */
	struct server_thread_arg *targ = (struct server_thread_arg*)arg;
	uint32_t ip = targ->peerid.ip;
	uint64_t last_timestamp = 0;
	pthread_t receiver_tid = targ->tid;

	free(targ);

	/* keeps running until this peer is considered to be dead */
	while (1) {
		struct peer_entry *pe;
		struct timeval current_time;
		/* sleep for INTERVAL time */
		usleep(ctr_info.interval + CHECK_ALIVE_DIFF);

		pe = peer_table_find(&pt, ip);
		if (pe == NULL) {
			_error("peer entry (ip:%u) doesn't exist\n", ip);
			pthread_cancel(receiver_tid);
			pthread_exit(NULL);
		}

		/* check if peer is alive */
		gettimeofday(&current_time, NULL);
		pthread_mutex_lock(&pt.mutex);
		if (pe->timestamp == last_timestamp) {
			_debug("DIED! peer (ip:%s)\n", ip_string(pe->peerid.ip));
			pthread_mutex_unlock(&pt.mutex);
			pthread_cancel(receiver_tid);
			pthread_exit((void *)0);
		}
		last_timestamp = pe->timestamp;
		pthread_mutex_unlock(&pt.mutex);
	}

	pthread_exit((void *)0);
}

/**
 * peer_register_handler_task: called when receivs PEER_REGISTER,
 * @  a structure that contains
 * 1. create an ACCEPT packet and send it back to peer
 * 2. create a new peer_table_entry and add it to peer_table
 * 3. create a peer_check_alive_timer for this client (send the receiver_handler's
      tid and peer ip as argument to this thread)
 */
static void *peer_register_handler_task(void *arg)
{
	struct server_thread_arg *targ = (struct server_thread_arg *)arg;
	int conn = targ->conn;
	uint32_t ip = targ->peerid.ip;
	uint16_t port = targ->peerid.port;
	pthread_t receiver_tid = targ->tid;
	pthread_t check_alive_tid;
	struct peer_entry *pe;
	struct ttop_packet accept_pkt;
	long int ret = 0;

	_enter();

	/* create an ACCEPT packet */
	accept_pkt.hdr.type = TRACKER_ACCEPT;
	accept_pkt.hdr.data_len = sizeof(struct ttop_control_info);
	memcpy(accept_pkt.data, &ctr_info, accept_pkt.hdr.data_len);

	/* send ACCEPT back to peer */
	if (send_ttop_packet(conn, &accept_pkt) < 0) {
		_error("fail to send ACCEPT to peer\n");
		free(targ);
		ret = -1;
		goto out;
	}

	/* create a new peer_table_entry and add it to peer_table */
	pe = peer_entry_alloc();
	if (pe == NULL) {
		_error("peer entry alloc failed\n");
		free(targ);
		ret = -1;
		goto out;
	}
	pe->conn = conn;
	pe->peerid.ip = ip;
	pe->peerid.port = port;
	pe->tid = receiver_tid;
	peer_table_add(&pt, pe);

	/* create a peer_check_alive_task for this peer and
	   send the targ got from receiver_handler_task to
	   peer_check_alive_task */
	pthread_create(&check_alive_tid, NULL, peer_check_alive_task, targ);

out:
	_leave();
	pthread_exit((void *)ret);
}

/**
 * called when receivs PEER_KEEP_ALIVE, looks for the peer in peer table and
 * udpates its timestamp
 */
static void *peer_life_update_task(void *arg)
{
	uint32_t ip = (long int)arg;
	struct peer_entry *pe = peer_table_find(&pt, ip);
	long int ret = -1;

	if (pe == NULL) {
		_error("peer entry (ip:%u) doesn't exist\n", ip);
		goto out;
	}

	/* update peer timestamp */
	pthread_mutex_lock(&pt.mutex);
	pe->timestamp += ctr_info.interval;
	pthread_mutex_unlock(&pt.mutex);

out:
	pthread_exit((void *)ret);
}

/**
 * called when receivs PEER_FILE_UPDATE
 */
void *peer_file_update_task(void *arg)
{
	/* extracts the trans_file_table sent by receiver_handler_task thread */
	struct trans_file_table *tft = (struct trans_file_table *)arg;
	struct trans_file_table new_tft;
	enum operation_type op_type;
	int entry_n = tft->n;
	int i;

	_enter();

	bzero(&new_tft, sizeof(struct trans_file_table));

	/* update tracker's file table. if updated, broadcast the
	   entire file tabel to all peers alive */
	for (i = 0; i < entry_n; i++) {
		struct trans_file_entry *te = tft->entries + i;
		struct file_entry *fe;

		switch (te->op_type) {
		case FILE_ADD:
			_debug("{ FILE_ADD } '%s'\n", te->name);
			/* create a new file_entry, set this peer as the
			   only owenr, and add it to global filetable */
			fe = file_table_add(&ft, te);
			if (fe != NULL) {
				trans_entry_fill_from(new_tft.entries + new_tft.n,
						fe);
				new_tft.entries[new_tft.n].op_type = FILE_ADD;
				new_tft.n++;
			}
			break;

		case FILE_DELETE:
			_debug("{ FILE_DELETE } '%s'\n", te->name);
			/* delete this entry from file table */
			if (file_table_delete(&ft, te) == 0) {
				memcpy(new_tft.entries + new_tft.n, te,
						sizeof(struct trans_file_entry));
				new_tft.n++;
			}
			break;

		case FILE_MODIFY:
			_debug("{ FILE_MODIFY } '%s'\n", te->name);

			fe = file_table_find(&ft, te);
			if (fe == NULL) {
				_debug("\t'%s' not exists, conflict!\n",
						te->name);
				file_table_add(&ft, te);
				op_type = FILE_ADD;
			} else {
				file_entry_update(fe, te);
				op_type = FILE_MODIFY;
			}

			trans_entry_fill_from(new_tft.entries + new_tft.n, fe);
			new_tft.entries[new_tft.n].op_type = op_type;
			new_tft.n++;

			break;

		case FILE_NONE:
			_debug("{ FILE_NONE } '%s'\n", te->name);
			break;

		default:
			break;
		}
	}

	/* after scanning the entire trans_file_table, check if need
	   to broadcast the entire file table to all peers alive */
	if (new_tft.n > 0)
		broadcast_update(&pt, &new_tft);

	_leave();
	free(tft);
	pthread_exit((void *)0);
}

static void *receiver_task(void *arg)
{
	long int conn = (long int)arg;
	pthread_t my_tid = pthread_self();
	struct ptot_packet pkt;

	_enter();

	while (recv_ptot_packet(conn, &pkt) > 0) {
		pthread_t new_tid;
		enum ptot_packet_type type = pkt.hdr.type;
		uint32_t ip = pkt.hdr.ip;
		uint16_t port = pkt.hdr.port;
		uint16_t data_len = pkt.hdr.data_len;
		struct server_thread_arg *targ;
		struct trans_file_table *tft;

		switch (type) {
		case PEER_REGISTER:
			_debug("[ PEER_REGISTER from '%s']\n", ip_string(ip));
			/* create a structure of arguments that needs to pass
			   to peer_register_handler_task thread */
			targ = calloc(1, sizeof(*targ));
			if (targ == NULL) {
				_error("targ alloc failed\n");
				pthread_exit((void *)-1);
			}
			targ->conn = conn;
			targ->peerid.ip = ip;
			targ->peerid.port = port;
			targ->tid = my_tid;
			/* creates peer_register_handler_task thread */
			pthread_create(&new_tid, NULL,
					peer_register_handler_task,
					(void *)targ);
			break;
		case PEER_KEEP_ALIVE:
			_debug("[ PEER_KEEP_ALIVE from '%s']\n", ip_string(ip));
			/* creates peer_life_update_task thread */
			pthread_create(&new_tid, NULL,
					peer_life_update_task,
					(void *)(long int)ip);
			break;
		case PEER_SYNC:
			_debug("[ PEER_SYNC from '%s']\n", ip_string(ip));
			break;
		case PEER_FILE_UPDATE:
			_debug("[ PEER_FILE_UPDATE from '%s']\n", ip_string(ip));
			/* extracts trans_file_table from received packet's
			   data field */
			tft = calloc(1, sizeof(*tft));
			if (tft == NULL) {
				_error("trans file table alloc failed\n");
				pthread_exit((void *)-1);
			}
			memcpy(tft, pkt.data, data_len);
			/* creates peer_file_update thread
			   passes the trans_file_table structure it needs to
			   handle as argument */
			pthread_create(&new_tid, NULL,
					peer_file_update_task, tft);
			break;
		default:
			break;
		}
	}

	_leave();

	pthread_exit((void *)0);
}

void server_start()
{
	long int listenfd, connfd;
	struct sockaddr_in cliaddr;
	socklen_t clilen;
	pthread_t receiver_tid;

	/* get tracker to peer control information */
	if (parse_conf_file(&ctr_info) != 0)
		return;
	_debug("interval = %d, piece_len = %d\n", ctr_info.interval,
			ctr_info.piece_len);

	/* init file table and peer table */
	file_table_init(&ft);
	peer_table_init(&pt);

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
		pthread_create(&receiver_tid, NULL,
				receiver_task, (void *)connfd);
	}

	return;
}
