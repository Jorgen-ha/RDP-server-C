BIN = client server client.o server.o common.o send_packet.o

all: $(BIN)

client: client.o common.o send_packet.o send_packet.h RDP.h
	gcc -g client.o common.o send_packet.o -o client

server: server.o common.o send_packet.o send_packet.h RDP.h
	gcc -g server.o common.o send_packet.o -o server

client.o: client.c
	gcc -g -c client.c

server.o: server.c
	gcc -g -c server.c

common.o: common.c
	gcc -g -c common.c

send_packet.o: send_packet.c
	gcc -g -c send_packet.c

clean: 
	rm -f $(BIN)