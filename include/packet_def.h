#ifndef PACKET_H
#define PACKET_H

#include <stdint.h>
#include "consts.h"

/**
 * definition of packet from peer to tracker
 */

#define ptot_packet_len(pkt) (sizeof(struct ptot_packet_header) +	\
			      (pkt)->hdr.data_len)
#define ttop_packet_len(pkt) (sizeof(struct ttop_packet_header) +	\
			      (pkt)->hdr.data_len)

/* packet types from peer to tracker */
enum ptot_packet_type {
	PEER_REGISTER,
	PEER_KEEP_ALIVE,
	PEER_FILE_UPDATE,
	PEER_SYNC,
};

/* definition of packet header from peer to tracker */
struct ptot_packet_header {
	char protocol_name[MAX_NAME_LEN];	/* protocol name */
	uint16_t type;				/* packet type: REGISTER, KEEP_ALIVE, FILE_UPDATE */
	uint32_t ip;	 			/* the peer ip address sending this segment */
	uint16_t port;				/* p2p listening port of the peer */
	uint16_t data_len;			/* length of data */
};

/* definition of packet from peer to tracker */
struct ptot_packet {
	struct ptot_packet_header hdr;		
	char data[MAX_PKT_DATA_LEN];		/* empty or updated file table */
};


/**
 * definition of packet from tracker to peer 
 */

/* packet types from tracker to peer */
enum ttop_packet_type {
	TRACKER_ACCEPT,
	TRACKER_BROADCAST,
	TRACKER_SYNC,
};

/* definition of packet header from tracker to peer */
struct ttop_packet_header {
	uint16_t type;		/* packet type: ACCPET, BROADCAST, SYNC */
	uint16_t data_len;	/* length of data */
};

/* peer control information which is setup by tracker */
struct ttop_control_info {
	int interval;		/* time interval that the peer should send alive message */
	int piece_len;		/* piece length */
};

/* definition of packet structure from tracker to peer */
struct ttop_packet {
	struct ttop_packet_header hdr;
	char data[MAX_PKT_DATA_LEN];		/* empty or latest tracker file table */
};

#endif
