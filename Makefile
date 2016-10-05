all: server.o
	gcc -Wall -L/usr/local/lib -lavcodec -lavformat -lavutil -lpthread -o server server.o main.c

server.o:	server.c
	gcc -Wall -L/usr/local/lib -lavcodec -lavformat -lavutil -lpthread -c server.c

clean:
	rm *.o server
