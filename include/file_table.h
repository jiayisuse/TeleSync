#ifndef FILE_TABLE_H
#define FILE_TABLE_H

#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>

#include <consts.h>
#include <hash.h>
#include <list.h>
#include <trans_file_table.h>

struct peer_id_list {
	uint32_t ip;
	uint16_t port;
	struct list_head l;
};

struct file_entry {
	char name[MAX_NAME_LEN];
	uint64_t timestamp;
	enum file_type type;
	struct list_head owner_head;
	struct hlist_node hlist;
	pthread_rwlock_t rwlock;
};

struct file_table {
	int n;
	pthread_mutex_t mutex;
	DECLARE_HASHTABLE(file_htable, FILE_HASH_BITS);
};

void peer_id_list_replace(struct file_entry *fe, struct trans_file_entry *te);
bool has_same_owners(struct file_entry *fe, struct trans_file_entry *te);

struct file_entry *file_entry_alloc();
void file_entry_add(struct file_table *table, struct file_entry *fe);
int file_entry_update(struct file_entry *fe, struct trans_file_entry *te);
void file_entry_update_timestamp(struct file_entry *fe, uint64_t timestamp);
void file_entry_fill_from(struct file_entry *fe, struct trans_file_entry *te);
void trans_entry_fill_from(struct trans_file_entry *te, struct file_entry *fe);

void file_table_init(struct file_table *table);
struct file_entry *file_table_add(struct file_table *table,
				  struct trans_file_entry *te);
struct file_entry *file_table_find(struct file_table *table,
				   struct trans_file_entry *te);
int file_table_update(struct file_table *table, struct trans_file_entry *te);
int file_table_delete(struct file_table *table, struct trans_file_entry *te);
void file_table_delete_owner(struct file_table *table, uint32_t ip);
void file_table_destroy(struct file_table *table);
void file_table_print(struct file_table *table);

void trans_table_fill_from(struct trans_file_table *tft, struct file_table *ft);

#endif
