CC=g++
CFLAGS1 = -o0 -g -Wall -Wl, -pthread
CFLAGS2 = -w -g -c

all: binder

client: client.o rpc.o
	$(CC) $(CFLAGS1) client.o librpc.a -o client

server: server.o rpc.o
	$(CC) $(CFLAGS1) server.o server_functions.o server_function_skels.o librpc.a -o server

message: message.cc
	$(CC) $(CFLAGS1) message.cc -o message.o

binder: binder.o rpc.o message.o
	$(CC) $(CFLAGS1) binder.o message.o librpc.a -o binder.o

client.o: client1.c
	$(CC) $(CFLAGS2) client1.c -o client.o

server.o: server.c server_functions.c server_function_skels.c
	$(CC) $(CFLAGS2) server.c server_functions.c server_function_skels.c

binder.o: binder.cc
	$(CC) $(CFLAGS2) binder.cc -o binder.o

rpc.o: rpc.cc
	$(CC) $(CFLAGS2) rpc.cc
	ar crs librpc.a rpc.o

clean:
	rm -f *o client server librpc.a
