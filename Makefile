CC = cc

LAV_LDFLAGS = `pkg-config --libs libavformat libavcodec libavutil`
LAV_CFLAGS = `pkg-config --cflags libavformat libavcodec libavutil`

.PHONY: all clean

all: server

server: segment.o buffer.o publisher.o server2.c
	$(CC) -g -Wall $(LAV_CFLAGS) $(LAV_LDFLAGS) -pthread -o server segment.o buffer.o publisher.o server2.c

segment.o: segment.c segment.h
	$(CC) -g -Wall $(LAV_CFLAGS) -pthread -c segment.c

buffer.o: buffer.c buffer.h
	$(CC) -g -Wall $(LAV_CFLAGS) -pthread -c buffer.c

publisher.o: publisher.c publisher.h
	$(CC) -g -Wall $(LAV_CFLAGS) -pthread -c publisher.c

clean:
	rm -f *.o server
