#ifndef PEER_TABLE_H
#define PEER_TABLE_H

#include <pthread.h>
#include <stdint.h>

#include <consts.h>
#include <list.h>
#include <trans_file_table.h>


struct peer_entry {
	int conn;
	uint64_t timestamp;	/* last timestamp of alive */
	struct peer_id peerid;	/* peer ip and port */
	struct list_head l;	/* entry list */
	pthread_t tid;		/* receiver thread id */
};

struct peer_table {
	int peer_num;			/* num of alive peers */
	pthread_mutex_t mutex;
	struct list_head peer_head;	/* linked list of all peers alive */
};


struct peer_entry *peer_entry_alloc();

void peer_table_init(struct peer_table *pt);
int peer_table_add(struct peer_table *pt, struct peer_entry *pe);
int peer_table_delete(struct peer_table *pt, int conn);
struct peer_entry *peer_table_find(struct peer_table *pt, uint32_t ip);
void peer_table_print(struct peer_table *pt);

#endif
