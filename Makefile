target = dartsync
common_headers = include/*.h
server_objs = server/start.o server/packet.o
client_objs = client/start.o client/file_monitor.o client/packet.o
utility_objs = utility/segment.o utility/list.o
objects = dartsync.o $(server_objs) $(client_objs) $(utility_objs)

CFLAGS += -Wall
LINKFLAGS += -lpthread
INC = -I./include

$(target) : $(objects)
	cc -o $(target) $(objects) $(LINKFLAGS)

%.o: %.c $(common_headers)
	$(CC) -c -o $@ $< $(INC) $(CFLAGS)

clean:
	rm *.o $(target) $(server_objs) $(client_objs) $(utility_objs)
