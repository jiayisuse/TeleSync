#ifndef SERVER_PACKET_H
#define SERVER_PACKET_H

#include <packet_def.h>

inline int send_ttop_packet(int conn, struct ttop_packet *pkt);

int recv_ptot_packet(int conn, struct ptot_packet *pkt);

#endif
