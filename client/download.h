#ifndef CLIENT_DOWNLOAD_H
#define CLIENT_DOWNLOAD_H

#include <stdint.h>
#include <file_table.h>

enum piece_status {
	PIECE_AVAILABLE,
	PIECE_DOWNLOADNG,
	PIECE_FINISHED
};

struct download_obj {
	char logic_name[MAX_NAME_LEN];
	char sys_name[MAX_NAME_LEN];
	int file_len;
	int file_pieces;
	int *piece_flags;
	pthread_t *tids;
	pthread_mutex_t mutex;
};

struct download_thread_arg {
	struct download_obj *obj;
	uint32_t owner_ip;
	uint16_t owner_port;
};

int do_download(struct file_entry *fe, char *sys_name);
int my_read(int fd, char *buf, int len);
int my_write(int fd, char *buf, int len);

#endif
