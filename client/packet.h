#ifndef CLIENT_PACKET_H
#define CLIENT_PACKET_h

#include <packet_def.h>

int send_ptot_packet(int conn, struct ptot_packet *pkt);

int recv_ttop_packet(int conn, struct ttop_packet *pkt);

#endif
