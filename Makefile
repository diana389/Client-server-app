CFLAGS = -Wall -std=c11 -lm

build: server subscriber

subscriber: tcp_client.c
	gcc tcp_client.c -o subscriber $(CFLAGS)

server: server.c
	gcc server.c -o server $(CFLAGS)

clean:
	rm -f server subscriber