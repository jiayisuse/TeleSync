#ifndef FILE_TABLE_H
#define FILE_TABLE_H

#include <stdint.h>
#include <pthread.h>
#include "consts.h"

struct peer_id_list {
	uint32_t ip;
	uint16_t port;
	struct peer_id_list *next;
};

struct file_entry {
	struct file_entry *next;
	char *name;
	long int timestamp;
	struct peer_id_list *owners_head;
};

struct file_table {
	int n;
	pthread_mutex_t table_mutex;
	struct file_entry *file_hash_table[FILE_HASH_SIZE];
};

#endif
