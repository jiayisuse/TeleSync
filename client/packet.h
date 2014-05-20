#ifndef CLIENT_PACKET_H
#define CLIENT_PACKET_H

#include <packet_def.h>

void ptot_packet_init(struct ptot_packet *pkt, enum ptot_packet_type type);

void ptot_packet_fill(struct ptot_packet *pkt, void *buf, int len);

int send_ptot_packet(int conn, struct ptot_packet *pkt);

int recv_ttop_packet(int conn, struct ttop_packet *pkt);
int client_tcp_listen(uint16_t port);

#endif
