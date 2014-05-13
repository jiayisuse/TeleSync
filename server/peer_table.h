#ifndef PEER_TABLE_H
#define PEER_TABLE_H

#include <pthread.h>
#include "consts.h"

struct peer_entry {
	int connfd;
	long int alive_timestamp;
};

struct peer_table {
	unsigned int n;
	pthread_mutex_t table_mutex;
	struct peer_entry *entries[MAX_PEER_ENTRIES];
};

#endif
