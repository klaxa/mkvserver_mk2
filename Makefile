all: server

server: segment.o buffer.o publisher.o server2.c
	gcc -g -Wall -L/usr/local/lib -lavcodec -lavformat -lavutil -lpthread -o server segment.o buffer.o publisher.o server2.c

segment.o: segment.c segment.h
	gcc -g -Wall -L/usr/local/lib -lavcodec -lavformat -lavutil -lpthread -c segment.c

buffer.o: buffer.c buffer.h
	gcc -g -Wall -L/usr/local/lib -lavcodec -lavformat -lavutil -lpthread -c buffer.c

publisher.o: publisher.c publisher.h
	gcc -g -Wall -L/usr/local/lib -lavcodec -lavformat -lavutil -lpthread -c publisher.c


clean:
	rm *.o server
