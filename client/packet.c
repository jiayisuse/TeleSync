#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>

#include <debug.h>
#include <utility/segment.h>
#include "packet.h"

/**
 * peer sends a ptot packet to tracker
 * @conn: connection fd
 * @pkt: the ptot_packet which is going to be sent to tracker
 * @return: -1, failed. Otherwise indicates how many bytes have
 *          been sent
 */
inline int send_ptot_packet(int conn, struct ptot_packet *pkt)
{
	return send_segment(conn, (char *)pkt, ptot_packet_len(pkt));
}

/**
 * peer receives a ttop packet from a tracker
 * @conn: connection fd
 * @pkt: the ttop_packet which is going to be filled
 * @return: -1, failed. Otherwise indicates how many bytes have
 *          been received
 */
int recv_ttop_packet(int conn, struct ttop_packet *pkt)
{
	int ret = -1;
	char buf[sizeof(struct ttop_packet) + 2] = { 0 };

	ret = recv_segment(conn, buf, sizeof(buf));
	if (ret < 0)
		return -1;
	memcpy(pkt, buf, sizeof(struct ttop_packet));

	return ret;
}
