#ifndef TRANS_FILE_TABLE_H
#define TRANS_FILE_TABLE_H

#include <stdint.h>

#include <consts.h>

#define trans_table_len(table) (sizeof(int) +	\
		(table)->n * sizeof(struct trans_file_entry))

enum file_type {
	REGULAR,
	DIRECTORY,
	SOFT_LINK,
};

enum operation_type {
	FILE_NONE,
	FILE_ADD,
	FILE_DELETE,
	FILE_MODIFY
};

struct peer_id {
	uint32_t ip;
	uint16_t port;
};

struct trans_file_entry {
	char name[MAX_NAME_LEN];
	uint64_t timestamp;
	uint16_t op_type;
	uint16_t file_type;
	uint16_t owner_n;
	struct peer_id owners[MAX_PEER_ENTRIES];
};

struct trans_file_table {
	int n;
	struct trans_file_entry entries[MAX_FILE_ENTRIES];
};

#endif
