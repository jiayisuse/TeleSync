#ifndef CLIENT_PACKET_H
#define CLIENT_PACKET_H

#include <packet_def.h>

#define p2p_packet_len(ptr) (sizeof(uint16_t) + sizeof(uint16_t) +	\
				(ptr)->data_len)
#define piece_req_len(ptr) (MAX_NAME_LEN * sizeof(char) + sizeof(uint16_t) +	\
				(ptr)->piece_n * sizeof(uint16_t))

enum p2p_packet_type {
	P2P_FILE_LEN_REQ,
	P2P_PORT_REQ,
	P2P_PIECE_REQ,
	P2P_FINISH,
	P2P_FILE_LEN_RET,
	P2P_PORT_RET,
	P2P_PIECE_RET,
};

struct p2p_packet {
	uint16_t type;
	uint16_t data_len;
	char data[MAX_PKT_DATA_LEN];
};

struct p2p_piece_request {
	char file_name[MAX_NAME_LEN];
	uint32_t piece_n;
	uint16_t piece_id[MAX_PIECES];
};

void ptot_packet_init(struct ptot_packet *pkt, enum ptot_packet_type type);
void ptot_packet_fill(struct ptot_packet *pkt, void *buf, int len);
int send_ptot_packet(int conn, struct ptot_packet *pkt);
int recv_ttop_packet(int conn, struct ttop_packet *pkt);

void p2p_packet_init(struct p2p_packet *pkt, enum p2p_packet_type type);
void p2p_packet_fill(struct p2p_packet *pkt, void *buf, int len);
int send_p2p_packet(int conn, struct p2p_packet *pkt);
int recv_p2p_packet(int conn, struct p2p_packet *pkt);

int client_tcp_listen(uint16_t port);

#endif
