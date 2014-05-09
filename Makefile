target = dartsync
common_headers = include/*.h
server_objs = server/start.o
client_objs = client/start.o
objects = dartsync.o $(server_objs) $(client_objs)

CFLAGS += -Wall
LINKFLAGS += -lpthread
INC = -I./include

$(target) : $(objects)
	cc -o $(target) $(objects) $(LINKFLAGS)

%.o: %.c $(common_headers)
	$(CC) -c -o $@ $< $(INC) $(CFLAGS)

clean:
	rm *.o $(target) $(server_objs) $(client_objs)
