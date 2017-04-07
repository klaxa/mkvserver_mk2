all: server segment-test

server: segment.o buffer.o publisher.o server2.c
	gcc -g -Wall -L/usr/local/lib -lavcodec -lavformat -lavutil -lpthread -o server segment.o buffer.o publisher.o server2.c
	
segment-test: segment.o buffer.o publisher.o segment-test.c
	gcc -g -Wall -L/usr/local/lib -lavcodec -lavformat -lavutil -lpthread -o segment-test segment.o buffer.o publisher.o segment-test.c

segment.o: segment.c
	gcc -g -Wall -L/usr/local/lib -lavcodec -lavformat -lavutil -lpthread -c segment.c

buffer.o: buffer.c
	gcc -g -Wall -L/usr/local/lib -lavcodec -lavformat -lavutil -lpthread -c buffer.c

publisher.o: publisher.c
	gcc -g -Wall -L/usr/local/lib -lavcodec -lavformat -lavutil -lpthread -c publisher.c


clean:
	rm *.o server
