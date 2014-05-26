#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/file.h>
#include <fcntl.h>

#include <debug.h>
#include "packet.h"
#include "download.h"


extern struct ttop_control_info ctr_info;

static void download_obj_init(struct download_obj *obj,
			      char *logic_name,
			      char *sys_name)
{
	if (obj == NULL || logic_name == NULL)
		return;

	bzero(obj, sizeof(struct download_obj));
	strcpy(obj->logic_name, logic_name);
	strcpy(obj->sys_name, sys_name);
	pthread_mutex_init(&obj->mutex, NULL);
}

static uint32_t get_file_len_from(char *file_name, uint32_t ip, uint16_t port)
{
	struct p2p_packet pkt;
	struct sockaddr_in servaddr;
	int fd = socket(AF_INET, SOCK_STREAM, 0); 
	uint32_t ret_len;

	if (fd < 0) {
		_error("socket create failed\n");
		return -1; 
	}   
	bzero(&servaddr, sizeof(struct sockaddr_in));
	servaddr.sin_family = AF_INET;
	servaddr.sin_addr.s_addr = ip;
	servaddr.sin_port = htons(port);
	if (connect(fd, (struct sockaddr *) &servaddr, sizeof(servaddr)) < 0) {
		perror("connect() error");
		return -1; 
	}

	p2p_packet_init(&pkt, P2P_FILE_LEN_REQ);
	p2p_packet_fill(&pkt, file_name, strlen(file_name) + 1);
	if (send_p2p_packet(fd, &pkt) < 0) {
		_error("p2p packet send failed for '%s'\n", file_name);
		return -1;
	}
	if (recv_p2p_packet(fd, &pkt) < 0) {
		_error("p2p packet recv failed for '%s'\n", file_name);
		return -1;
	}
	if (pkt.type != P2P_FILE_LEN_RET) {
		_error("p2p packet type is not P2P_FILE_LEN_RET\n");
		return -1;
	}

	memcpy(&ret_len, pkt.data, sizeof(uint32_t));
	close(fd);

	return ret_len; 
}

static int connect_to_peer(uint32_t ip, uint16_t port)
{
	struct sockaddr_in servaddr;
	int fd = socket(AF_INET, SOCK_STREAM, 0); 

	if (fd < 0) {
		_error("socket create failed\n");
		return -1; 
	}   
	bzero(&servaddr, sizeof(struct sockaddr_in));
	servaddr.sin_family = AF_INET;
	servaddr.sin_addr.s_addr = ip;
	servaddr.sin_port = htons(port);
	if (connect(fd, (struct sockaddr *) &servaddr, sizeof(servaddr)) < 0) {
		perror("connect() error");
		return -1; 
	}   

	return fd; 
}

static int get_p2p_download_port(int conn, struct download_thread_arg *targ)
{
	struct p2p_packet pkt;
	uint16_t port = -1;

	p2p_packet_init(&pkt, P2P_PORT_REQ);
	p2p_packet_fill(&pkt, targ->obj->logic_name,
			strlen(targ->obj->logic_name) + 1);

	if (send_p2p_packet(conn, &pkt) < 0) {
		_error("p2p packet send error\n");
		goto out;
	}
	if (recv_p2p_packet(conn, &pkt) < 0) {
		_error("p2p packet recv error\n");
		goto out;
	}
	if (pkt.type != P2P_PORT_RET) {
		_error("p2p packet type is not P2P_PORT_RET\n");
		goto out;
	}

	memcpy(&port, pkt.data, sizeof(uint16_t));

out:
	return port;
}

static int get_new_piece(struct download_obj *obj)
{
	int i, ret = -1;;

	if (obj == NULL)
		return ret;

	pthread_mutex_lock(&obj->mutex);
	for (i = 0; i < obj->file_pieces; i++) {
		if (obj->piece_flags[i] == PIECE_AVAILABLE) {
			ret = i;
			obj->piece_flags[i] = PIECE_DOWNLOADNG;
			break;
		}
	}
	pthread_mutex_unlock(&obj->mutex);

	return ret;
}

static inline void mark_piece_finished(struct download_obj *obj, int piece_id)
{
	pthread_mutex_lock(&obj->mutex);
	obj->piece_flags[piece_id] = PIECE_FINISHED;
	pthread_mutex_unlock(&obj->mutex);
}

static inline void mark_piece_failed(struct download_obj *obj, int piece_id)
{
	pthread_mutex_lock(&obj->mutex);
	obj->piece_flags[piece_id] = PIECE_AVAILABLE;
	pthread_mutex_unlock(&obj->mutex);
}

int my_read(int fd, char *buf, int len)
{
	int n = 0, left = len;
	int ret = 0;

	while (n < len) {
		ret = read(fd, buf + n, left);
		if (ret < 0)
			break;
		left -= ret;
		n += ret;
	}

	return n;
}

int my_write(int fd, char *buf, int len)
{
	int n = 0, left = len;
	int ret = 0;

	while (n < len) {
		ret = write(fd, buf + n, left);
		if (ret <= 0)
			continue;
		left -= ret;
		n += ret;
	}

	return n;
}

static void *piece_download_task(void *arg)
{
	struct download_thread_arg *targ = arg;
	struct p2p_packet pkt;
	char *piece_buf;
	int piece_len = ctr_info.piece_len;
	struct p2p_piece_request req;
	int file_fd;
	int conn, download_conn;
	uint16_t download_port;
	long int ret = 0, ret_len;
	int piece_id;

	_enter();

       	conn = connect_to_peer(targ->owner_ip, targ->owner_port);
	if (conn < 0) {
		ret = -1;
		goto out;
	}

	download_port = get_p2p_download_port(conn, targ);
	if (download_port < 0) {
		_error("get download port failed\n");
		ret = -1;
		goto close_conn;
	}

	_debug("download port = %u\n", download_port);

	download_conn = connect_to_peer(targ->owner_ip, download_port);
	if (download_conn < 0) {
		ret = -1;
		goto out;
	}

	piece_buf = calloc(1, piece_len);
	if (piece_buf == NULL) {
		_error("piece buf alloc failed\n");
		ret = -1;
		goto close_download_conn;
	}

	file_fd = open(targ->obj->sys_name, O_RDWR);
	if (file_fd < 0) {
		_error("file open failed for '%s'\n", targ->obj->sys_name);
		ret = -1;
		goto free_piece_buf;
	}

	p2p_packet_init(&pkt, P2P_PIECE_REQ);
	while ((piece_id = get_new_piece(targ->obj)) >= 0) {
		req.len = piece_len;
		if (piece_id == targ->obj->file_pieces - 1)
			req.len = targ->obj->file_len - piece_len *
				(targ->obj->file_pieces - 1);
		req.piece_id = piece_id;

		_debug("\tdownload piece #%d, len = %d\n", piece_id, req.len);

		p2p_packet_fill(&pkt, &req, sizeof(req));
		if (send_p2p_packet(download_conn, &pkt) < 0) {
			_error("piece req send failed for #%d\n", piece_id);
			mark_piece_failed(targ->obj, piece_id);
			ret = -1;
			break;
		}

		if ((ret_len = my_read(download_conn, piece_buf, req.len)) < 0) {
			_error("download failed for '%s'\n",
					targ->obj->logic_name);
			mark_piece_failed(targ->obj, piece_id);
			ret = -1;
			break;
		}

		flock(file_fd, LOCK_EX);
		lseek(file_fd, piece_id * piece_len, SEEK_SET);
		ret_len = my_write(file_fd, piece_buf, ret_len);
		flock(file_fd, LOCK_UN);

		mark_piece_finished(targ->obj, piece_id);
	}

	close(file_fd);
free_piece_buf:
	free(piece_buf);
close_download_conn:
	close(download_conn);
close_conn:
	close(conn);
out:
	free(targ);
	_leave();
	pthread_exit((void *)ret);
}

int do_download(struct file_entry *fe, char *sys_name)
{
	struct download_obj obj;
	struct peer_id_list *a_owner;
	struct list_head *pos;
	int piece_len = ctr_info.piece_len;
	int fd;
	int i, n, owner_n = 0;
	long int ret = -1;

	_enter("%s", fe->name);

	fd = open(sys_name, O_RDWR | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);
	if (fd < 0) {
		_error("open '%s' failed\n", sys_name);
		goto out;
	}
	close(fd);

	if (list_empty(&fe->owner_head)) {
		_error("No owner for '%s'\n", fe->name);
		goto out;
	}
	pthread_rwlock_rdlock(&fe->rwlock);
	a_owner = list_entry(fe->owner_head.next, struct peer_id_list, l);
	list_for_each(pos, &fe->owner_head)
		owner_n++;
	pthread_rwlock_unlock(&fe->rwlock);
	
	/* init download object */
	download_obj_init(&obj, fe->name, sys_name);
	obj.file_len = get_file_len_from(fe->name, a_owner->ip, a_owner->port);
	obj.file_pieces = (obj.file_len + piece_len - 1) / piece_len;
	obj.piece_flags = calloc(obj.file_pieces, sizeof(int));
	if (obj.piece_flags == NULL) {
		_error("obj piece flags alloc failed\n");
		goto out;
	}
	obj.tids = calloc(owner_n, sizeof(pthread_t));
	if (obj.tids == NULL) {
		_error("obj tids alloc failed\n");
		goto free_piece_flags;
	}

	/* assign download task to different threads */
	n = 0;
	pthread_rwlock_rdlock(&fe->rwlock);
	list_for_each(pos, &fe->owner_head) {
		struct download_thread_arg *targ = calloc(1, sizeof(*targ));
		a_owner = list_entry(pos, struct peer_id_list, l);

		if (targ == NULL) {
			_error("download targ alloc failed\n");
			goto wait_tasks;
		}
		targ->obj = &obj;
		targ->owner_ip = a_owner->ip;
		targ->owner_port = a_owner->port;

		pthread_create(obj.tids + n, NULL, piece_download_task, targ);
		n++;
	}
	pthread_rwlock_unlock(&fe->rwlock);

wait_tasks:
	for (i = 0; i < n; i++) {
		void *status;
		pthread_join(obj.tids[i], &status);
		ret = (int *)status && ret;
	}

	if (ret == 0)
		_debug("{ Download OK! } '%s'\n", fe->name);
	else
		_debug("{ Download ERROR! } '%s'\n", fe->name);


	free(obj.tids);
free_piece_flags:
	free(obj.piece_flags);
out:
	return ret;
}
