#ifndef CONSTS_H
#define CONSTS_H

#define MAX_PEER_ENTRIES	16
#define FILE_HASH_BITS		12
#define MAX_FILE_ENTRIES	4096
#define MAX_LINE		1024
#define MAX_NAME_LEN		128
#define MAX_TARGET_DIR		2048
#define MAX_PKT_DATA_LEN	(2 << 15)
#define MAX_CONNECTIONS		1024
#define CHECK_ALIVE_DIFF	500000	/* check alive different micro sec */
#define MAX_PIECES		1024
#define MAX_P2P_PORT		1000
#define BASE_P2P_PORT		5100

#define TRACKER_RECEIVER_PORT	4092
#define P2P_PORT		4192

#endif
