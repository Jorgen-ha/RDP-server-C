// Fil som inneholder felles RDP-kode, brukt av både server og klient

#include "RDP.h"

void checkError(int res, char *msg){                                                                                    //Function to check for error in functions returning -1
    if (res == -1){
        perror(msg);
        exit(EXIT_FAILURE);
    }
}

char *serialize(struct rdpPacket *pack, unsigned int *size){                                                            //Function to serialize the RDP-struct before sending 
    char *toSend;
    int reject = 0;
    int offset = 0;
    if(pack->flag != 0x20){
        toSend = malloc(sizeof(struct rdpPacket)+pack->metadata);
    }else{
        toSend = malloc(sizeof(struct rdpPacket));
        reject = 1;
    }
    if(!toSend){
        fprintf(stderr, "malloc failed, may be out of memory.\n"); 
        exit(EXIT_FAILURE);
    }
    memcpy(toSend+offset, &pack->flag, sizeof(char));
    offset++;
    memcpy(toSend+offset, &pack->seqNumb, sizeof(char));
    offset++;
    memcpy(toSend+offset, &pack->ackNumb,  sizeof(char));
    offset++;
    memcpy(toSend+offset, &pack->unassigned, sizeof(char));
    offset++;
    int tmp =  htonl(pack->senderid);
    memcpy(toSend + offset, &tmp, sizeof(int));
    offset+=sizeof(int);
    tmp = htonl(pack->recvID);
    memcpy(toSend + offset, &tmp, sizeof(int));
    offset += sizeof(int);
    tmp = htonl(pack->metadata);
    memcpy(toSend + offset, &tmp, sizeof(int));
    offset += sizeof(int);
   
    if(!reject){
        memcpy(toSend+offset, pack->payload, pack->metadata);
        *size = sizeof(int)*3 + sizeof(char)*4 + pack->metadata;
    }else{
        *size = sizeof(int)*3 + sizeof(char)*4;
    }

    return toSend;
}

struct rdpPacket *deSerialize(char *serialPack){                                                                        //Function to deserialize incoming RDP-packets
    struct rdpPacket *pack = malloc(sizeof(struct rdpPacket));
    if(!pack){
        fprintf(stderr, "malloc failed, may be out of memory.\n");
        exit(EXIT_FAILURE);
    }
    int offset = 0;
    pack->flag = serialPack[offset];
    offset++;
    pack->seqNumb = serialPack[offset];
    offset++;
    pack->ackNumb = serialPack[offset];
    offset++;
    pack->unassigned = serialPack[offset];
    offset++;
    pack->senderid = ntohl(*((int*)&serialPack[offset]));
    offset+=sizeof(int);
    pack->recvID = ntohl(*((int*)&serialPack[offset]));
    offset+=sizeof(int);
    pack->metadata = ntohl(*((int*)&serialPack[offset]));
    offset+=sizeof(int);

    if(pack->metadata != 1111 && pack->metadata != 2222 && pack->metadata != 0){
        pack->payload = malloc(pack->metadata);
        if(!pack->payload){
            fprintf(stderr, "malloc failed, may be out of memory.\n");
            exit(EXIT_FAILURE);
        }
        memcpy(pack->payload, serialPack+offset, pack->metadata);
    }
    return pack;
}

struct rdpPacket makePacket(unsigned char flag, unsigned char seq, unsigned char ack, int sender, int recv, int data){  //Function to make a new RDP-packet
    struct rdpPacket newPacket = {
        .flag = flag,
        .seqNumb = seq,
        .ackNumb = ack,
        .unassigned = 0,
        .senderid = sender,
        .recvID = recv,
        .metadata = data,
    };
    return newPacket;
}

void sendNcheck(int fd, int size, char *serial, struct sockaddr_in recvAddr){                                           //Function to send rdp-packets and check for error
    int wc = send_packet(fd, serial, size, 0, (struct sockaddr *)&recvAddr, sizeof(struct sockaddr_in));
    checkError(wc, "send_packet");
}

struct rdpPacket *rdp_read(int fd, char *buffer, struct sockaddr_in sndAddr){                                           //Function to receive and deserialize RDP-packets
    char *serialPck = buffer;   
    socklen_t addrLength = sizeof(struct sockaddr_in);

    int rc = recvfrom(fd, buffer, BUFSIZE-1, 0, (struct sockaddr *)&sndAddr, &addrLength);
    checkError(rc, "recvfrom");
    struct rdpPacket *packet = deSerialize(buffer);

    return packet;
}

int rdp_connect(int fd, struct sockaddr_in srvAddr){                                                                    //Function to request a connection to the server
    srand(time(NULL));
    int cliID = random() % 1000;   //Here the random ID is generated (number between 0-1000)
    struct rdpPacket conReq = makePacket(0x1, 0, 0, cliID, 0, 0);
    unsigned int size;
    char *serialPacket = serialize(&conReq, &size);
    sendNcheck(fd, size, serialPacket, srvAddr);
    free(serialPacket);

    return cliID;
}

int acceptCon(int fd, int srvID, int reqID, struct sockaddr_in cliAddress){                                             //Function to send a confirmating packet allowing the requested connection
    struct rdpPacket conAccept = makePacket(0x10, 0, 0, srvID, reqID, 0);
    unsigned int size;
    char *acceptMsg = serialize(&conAccept, &size);
    sendNcheck(fd, size, acceptMsg, cliAddress);
    free(acceptMsg);
    return fd;
}

void rejectCon(int fd, int srvID, int reqID, int connected, struct sockaddr_in cliAddress){                             //Function to reject an incoming connection request
    int metDat;
    if(connected){
        metDat = 1111;       //ClientID already connected
    }else{
        metDat = 2222;       //Last filetransfer begun
    }
    struct rdpPacket conReject = makePacket(0x20, 0, 0, srvID, reqID, metDat);
    unsigned int size;
    char *reject = serialize(&conReject, &size);
    sendNcheck(fd, size, reject, cliAddress);
    free(reject);
}

void rdp_close(int fd, int sender, int srvID, unsigned char seq, unsigned char ack, struct sockaddr_in cliAddress){     //Function to close the connection between client and server
    struct rdpPacket conClose = makePacket(0x2, seq, ack, sender, srvID, 0);
    unsigned int size;
    char *close = serialize(&conClose, &size);
    sendNcheck(fd, size, close, cliAddress);
    free(close);
}

int rdp_write(int fd, int srvID, int recvID, int received, int *added, unsigned char *seq, char *tmpFile, struct sockaddr_in cliAddress, 
              int sizeOfFile, int *transferCnt, struct timeval *sndTime){                                               //Function to send the next packet of the file being copied from server
    unsigned int size;
    struct rdpPacket dataPacket;
    struct timeval curTime; 
    char *data;
    char *fileBuffer = tmpFile;
    unsigned char flag = 0x4, ack = 0;
    int remaining;
    int counter = received;

    if(counter == sizeOfFile){
        dataPacket = makePacket(flag, *seq, ack, srvID, recvID, 0); //Send the terminating 0-packet
        data = serialize(&dataPacket, &size);
        
        gettimeofday(sndTime, NULL);
        sendNcheck(fd, size, data, cliAddress);
        free(data);
        *added = 0;
        return counter;
    }

    if(counter < sizeOfFile-999){               //Sends the next 999 bytes as
        char pLoad[999];
        memcpy(pLoad, &fileBuffer[counter], 999);
        dataPacket = makePacket(flag, *seq, ack, srvID, recvID, 999);
        dataPacket.payload = pLoad;
        data = serialize(&dataPacket, &size);

        gettimeofday(sndTime, NULL);
        sendNcheck(fd, size, data, cliAddress);
        free(data);
        counter+=999;
        *added = 999;
        return counter;

    }else if(counter > sizeOfFile-999 ){
        remaining = sizeOfFile - counter;   //Find out how much is left to send.
        char pLoad[remaining];
        memcpy(pLoad, &fileBuffer[counter], remaining);
        dataPacket = makePacket(flag, *seq, ack, srvID, recvID, remaining);
        dataPacket.payload = pLoad;
        data = serialize(&dataPacket, &size);

        gettimeofday(sndTime, NULL);
        sendNcheck(fd, size, data, cliAddress);     //Send last bit of data
        free(data);
        counter+=remaining;
        *added = remaining;
        return counter;
    }    
}

struct client *makeClient(int ID, struct sockaddr_in cliAddr){                                                          //Function to make a new client struct
    struct client *newClient = malloc(sizeof(struct client));
    if (newClient == NULL){
        fprintf(stderr, "malloc failed, may be out of memory.\n");
        exit(EXIT_FAILURE);
    }
    newClient->ID = ID;
    newClient->sent = 0;
    newClient->seq = 0;
    newClient->ack = 1;
    newClient->addr = cliAddr;
    return newClient;
}

struct client *rdp_accept(struct rdpPacket *pack, struct sockaddr_in cliAddr, struct client *clients, int begun, int maxTrans,
                          int srvID, int srvFd){                                                                        //Function to accept an incoming connection request
                                                                                                                                
    int alrCon = 0;
    if(pack->flag == 0x1){
        if(begun == maxTrans){
            printf("NOT CONNECTED %d %d\n", pack->senderid, srvID);
            rejectCon(srvFd, srvID, pack->senderid, alrCon, cliAddr);
            return NULL;
        }
        struct client *tmp = clients;
        while (tmp){ //Loop to see if the clientID is already found among connected clients
            if (tmp->ID == pack->senderid || tmp->ID == srvID){
                alrCon = 1;
                printf("NOT CONNECTED %d %d\n", pack->senderid, srvID);
                rejectCon(srvFd, srvID, pack->senderid, alrCon, cliAddr);
                return NULL;
            }else{
                tmp = tmp->next;
            }
        }
        if (!alrCon){
            acceptCon(srvFd, srvID, pack->senderid, cliAddr);
            printf("CONNECTED %d %d\n", pack->senderid, srvID);
            struct client *newClient = makeClient(pack->senderid, cliAddr);
            return newClient;
        }
    }else{
        return NULL;
    }
}
