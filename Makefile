target = dartsync
common_headers = include/*.h include/utility/*.h
server_objs = server/start.o server/packet.o server/peer_table.o
client_objs = client/start.o client/file_monitor.o client/packet.o client/download.o
utility_objs = file_table.o utility/segment.o utility/list.o utility/pthread_wait.o
objects = dartsync.o $(server_objs) $(client_objs) $(utility_objs)

CFLAGS += -Wall -g
LINKFLAGS += -lpthread
INC = -I./include

$(target) : $(objects)
	cc -o $(target) $(objects) $(LINKFLAGS)

%.o: %.c $(common_headers)
	$(CC) -c -o $@ $< $(INC) $(CFLAGS)

clean:
	rm $(target) $(objects)
