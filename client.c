//Client
#include "RDP.h"


int makeFile(char fileName[]){          //Function to  make a new file with random name
    char *kf = "kernel-file-";
    srand(time(NULL));
    int fileNumb = random() % 1000;
    sprintf(fileName, "%s%d", kf, fileNumb);
    if (access(fileName, F_OK) == 0){   //Checks if the file already exists
        printf("access: File with filename %s already exists.\n", fileName);
        return 0;
    }else{
        return 1;
    }
}

void confPck(unsigned char ack, int srvID, int cliID, int fd, struct sockaddr_in srvAddr){      //Function to send ACK-packet back to server
    struct rdpPacket ackPacket = makePacket(0x8, 0, ack, cliID, srvID, 0);
    unsigned int size;
    char *serialAck = serialize(&ackPacket, &size);
    sendNcheck(fd, size, serialAck, srvAddr);
    free(serialAck);
}

int main(int argc, char *argv[]){
    int fd, wc, rc, clientID, srvID, port, curPload = 1;
    float lossProb;
    unsigned char seq, ack;
    char buffer[BUFSIZE], fileName[16]; 
    char *IP;
    struct sockaddr_in srvAddr;
    struct in_addr ipAddr;
    socklen_t addrLength = sizeof(struct sockaddr_in);
    FILE *fileCopy;
    struct timeval timeout;
    fd_set fds;

    if(argc != 4){
        fprintf(stderr, "Forventet bruk; '%s <IP> <portnummer> <tapssannsynlighet>'\n", argv[0]);
        return EXIT_FAILURE;
    }

    IP = argv[1];
    port = atoi(argv[2]);
    lossProb = atof(argv[3]);

    set_loss_probability(lossProb);

    wc = inet_pton(AF_INET, IP, &ipAddr);
    checkError(wc, "inet_pton");
    if(!wc){
        fprintf(stderr, "Invalid IP address: %s\n", IP);
        return EXIT_FAILURE;
    }

    fd = socket(AF_INET, SOCK_DGRAM, 0);
    checkError(fd, "socket");
    
    srvAddr.sin_family = AF_INET;
    srvAddr.sin_port =  htons(port);
    srvAddr.sin_addr = ipAddr;

//Send request to server 
    clientID = rdp_connect(fd, srvAddr);           //Requests to connect to the server. The randomly  chosen ID is returned


    FD_ZERO(&fds);
    FD_SET(fd,&fds);
    timeout.tv_sec = 1;
    timeout.tv_usec = 0;
// Receive from server
    rc = select(FD_SETSIZE, &fds, NULL, NULL, &timeout);
    if(!rc){                                            //If server didn't respond in 1 sec, close connection
        printf("Couldn't reach server.\n");
        close(fd);
        return EXIT_FAILURE;
    }

    if(FD_ISSET(fd, &fds)){
        struct rdpPacket *reply = rdp_read(fd, buffer, srvAddr);
        unsigned char curFlag = reply->flag;
        int metDat = reply->metadata;
        srvID = reply->senderid;
        free(reply);
        seq = 0;
        ack = 0;

        if(curFlag == 0x20){
            if (metDat == 1111){
                printf("Message from server: CLIENT ID %d NOT AVAILABLE\n", clientID);
            }
            else if (metDat == 2222){
                printf("Message from server: LAST FILE TRANSFER HAS ALREADY BEGUN\n");
            }
        }
        
        if(curFlag == 0x10){
            printf("Server ID: %d\t Client ID: %d\n", srvID, clientID);
            if(!makeFile(fileName)){
                rdp_close(fd, clientID, srvID, seq, ack, srvAddr);
                exit(EXIT_FAILURE);
            }

            fileCopy = fopen(fileName, "wb"); //Åpner en ny fil, som det skal kopieres til

            if (fileCopy == NULL) { //Sjekker om åpning av den nye filen gikk bra
                perror("fopen");
                rdp_close(fd, clientID, srvID, seq, ack, srvAddr);
                exit(EXIT_FAILURE);
            }
        }

        while(curFlag != 0x20 && curPload){
            struct rdpPacket *filePor = rdp_read(fd, buffer, srvAddr);            
            curPload = filePor->metadata;
            if(curPload){
                if(filePor->seqNumb == seq){
                    wc = fwrite(filePor->payload, sizeof(char), filePor->metadata, fileCopy);
                    ack = filePor->seqNumb;
                    if(seq == 0){
                        seq++;
                    }else{
                        seq = 0;
                    }
                    confPck(ack, srvID, clientID, fd, srvAddr);         //Send ACK
                    curFlag = filePor->flag;
                    free(filePor->payload);
                    free(filePor);
                }else{                                                  //Server resent a packet, last ACK was probably lost
                    confPck(ack, srvID, clientID, fd, srvAddr);         //Send a new ACK
                    curFlag = filePor->flag;
                    free(filePor->payload);
                    free(filePor);
                }
            }else{
                ack = filePor->seqNumb;
                if (seq == 0){
                    seq++;
                }else{
                    seq = 0;
                }
                confPck(ack, srvID, clientID, fd, srvAddr);              //Send ACK
                rdp_close(fd, clientID, srvID, seq, ack, srvAddr);       //Send disconnect-packet
                fclose(fileCopy);
                free(filePor);
                printf("%s\n",fileName);
            }
        }
    }
    close(fd);
    return EXIT_SUCCESS;
}
