#include <sys/types.h>
#include <sys/socket.h>

#include <utility/segment.h>
#include <debug.h>

int send_segment(int conn, char *buf, int len)
{
	char start_buf[] = { '!', '&' };
	char end_buf[] = { '!', '#' };

	if (send(conn, start_buf, sizeof(start_buf), 0) < 0)
		return -1;
	if (send(conn, buf, len, 0) < 0)
		return -1;
	if (send(conn, end_buf, sizeof(end_buf), 0) < 0)
		return -1;

	return len + sizeof(start_buf) + sizeof(end_buf);
}

int recv_segment(int conn, char *buf, int len)
{
	char c;
	int idx = 0;
	// state can be 0,1,2,3; 
	// 0 starting point 
	// 1 '!' received
	// 2 '&' received, start receiving segment
	// 3 '!' received,
	// 4 '#' received, finish receiving segment 
	int state = 0;  
	while (recv(conn, &c, 1, 0) > 0) {
		if (state == 0) {
			if (c == '!')
				state = 1;
		} else if (state == 1)
			state = c == '&' ? 2 : 0;
		else if (state == 2) {
			if (c == '!') {
				buf[idx] = c;
				idx++;
				state = 3;
			} else {
				buf[idx] = c;
				idx++;
			}   
		} else if (state == 3) {
			if (c == '#') {
				buf[idx]=c;
				idx++;
				return idx;
			} else if (c == '!') {
				buf[idx] = c;
				idx++;
			} else {
				buf[idx] = c;
				idx++;
				state = 2;
			}
		}
	}

	return -1;
}
