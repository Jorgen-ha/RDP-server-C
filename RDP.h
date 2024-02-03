#ifndef RDP_H
#define RDP_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <time.h>
#include "send_packet.h"

#define BUFSIZE 1030

// RDP-PACKET STRUCT
struct rdpPacket {
    unsigned char flag;
    unsigned char seqNumb;
    unsigned char ackNumb;
    unsigned char unassigned;
    int senderid;
    int recvID;
    int metadata;
    char *payload;
} __attribute__((packed));

//RDP CONNECTION STRUCT
struct client{
    int ID;
    int sent;
    int lastAdd;
    unsigned char seq;
    unsigned char ack;
    struct sockaddr_in addr;
    struct timeval time;
    struct client *next;
};

int rdp_write(int fd, int srvID, int recvID, int received, int *added, unsigned char *seq, char *tmpFile, struct sockaddr_in cliAddress, 
                       int sizeOfFile, int *transferCnt, struct timeval *sndTime);
int rdp_connect(int fd, struct sockaddr_in srvAddr);
struct client *rdp_accept(struct rdpPacket *pack, struct sockaddr_in cliAddr, struct client *clients, int begun, int maxTrans, int srvID, int srvFd);
void rdp_close(int fd, int sender, int srvID, unsigned char seq, unsigned char ack, struct sockaddr_in cliAddress);
struct rdpPacket *rdp_read(int fd, char *buffer, struct sockaddr_in srvAddr);

void checkError(int res, char *msg);
void sendNcheck(int fd, int size, char *serial, struct sockaddr_in recvAddr);
struct rdpPacket makePacket(unsigned char flag, unsigned char seq, unsigned char ack, int sender, int recv, int data); 
struct rdpPacket *deSerialize(char *serialPack);
char *serialize(struct rdpPacket *pack, unsigned int *size); 

#endif