#ifndef SEGMENT_H
#define SEGMENT_H

int send_segment(int conn, char *buf, int len);
int recv_segment(int conn, char *buf, int len);

#endif
