#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>

#include <debug.h>
#include <utility/segment.h>
#include "packet.h"

/**
 * tracker sends a ttop packet to peer
 * @conn: connection fd
 * @pkt: the ttop_packet which is going to be sent to peer
 * @return: -1, failed. otherwise indicates how many bytes have
 *          been sent
 */
inline int send_ttop_packet(int conn, struct ttop_packet *pkt)
{
	return send_segment(conn, (char *)pkt, ttop_packet_len(pkt));
}

/**
 * tracker receives a ptot packet from a peer
 * @conn: connection fd
 * @pkt: the ptot_packet which is going to be filled
 * @return: -1, failed. otherwise indicates how many bytes have
 *          been received
 */
int recv_ptot_packet(int conn, struct ptot_packet *pkt)
{
	int ret = -1;
	char buf[sizeof(struct ptot_packet) + 2] = { 0 };

	ret = recv_segment(conn, buf, sizeof(buf));
	if (ret < 0)
		return -1;
	memcpy(pkt, buf, sizeof(struct ptot_packet));

	return ret;
}

int server_tcp_listen(uint16_t port)
{
	int listenfd, on = 1;
	struct sockaddr_in servaddr;

	if ((listenfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
		perror("socket() error");
		return -1; 
	}   

	if (setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on))) {
		perror("setsockopt() error");
		return -1; 
	}   

	bzero(&servaddr, sizeof(servaddr));
	servaddr.sin_family = AF_INET;
	servaddr.sin_addr.s_addr = htons(INADDR_ANY);
	servaddr.sin_port = htons(port);

	if (bind(listenfd, (struct sockaddr *)&servaddr, sizeof(servaddr))) {
		perror("bind() error");
		return -1; 
	}   

	listen(listenfd, MAX_CONNECTIONS);

	return listenfd;
}
