//Server

#include "RDP.h"

int sizeOfFile = 0, transferCnt = 0, srvID = 1000;
struct client *clients = NULL;

struct client *findClient(int ID){                      //Function to find a client given its ID
    struct client *tmp = clients;
    while (tmp->ID != ID){
        tmp = tmp->next;
    }
    return tmp;
}

void addClient(struct client *newClient){               //Function to add a new client to the global clients-list
    newClient->next = clients;
    clients = newClient;
}

void removeClient(int clientID){                        //Function to remove a disconnecting client
    int i = 0, j = 0;
    struct client *tmp = clients, *tmp2 = clients;
    while (tmp->ID != clientID){ 
        tmp = tmp->next;
        i++;
    }
    if (i == 0){
        clients = tmp->next;
        free(tmp);
    }else{
        while (j < i - 1){
            tmp2 = tmp2->next;
            j++;
        }
        tmp2->next = tmp->next;
        free(tmp);
    }
}

char *readFile(char *fileName){                         //Function to read the file to the server memory
    int numBytes;
    int rc;

    if (!access(fileName, F_OK) == 0){ //Checks if the file exists
        perror("access");
        exit(EXIT_FAILURE);
    }

    FILE *srcFile = fopen(fileName, "rb");

    if (srcFile == NULL){ //Checks if opening the file went alright
        perror("fopen");
        exit(EXIT_FAILURE);
    }

    fseek(srcFile, 0L, SEEK_END); //Finds the end of the file
    numBytes = ftell(srcFile);    //Stores the number of bytes found in this file
    sizeOfFile = numBytes;
    char *fileBuffer = malloc(numBytes);

    fseek(srcFile, 0L, SEEK_SET); //Reset the file position indicator

    rc = fread(fileBuffer, sizeof(char), numBytes, srcFile); //Reads the entire file to fileBuffer
    fclose(srcFile);
    return fileBuffer;
}

int checkAck(struct client *cli){                       //Function to a client's last ACK

    if (cli->ack != cli->seq){
        cli->sent -= cli->lastAdd;
        return 1;
    }else{
        if (cli->seq == 0){
            cli->seq++;
            cli->ack = 0;
        }else{
            cli->seq = 0;
            cli->ack++;
        }
        return 0;
    }
}

int createNbind(struct sockaddr_in myAddr, int port){   //Function to create and bind a new socket
    int newSock = socket(AF_INET, SOCK_DGRAM, 0);
    checkError(newSock, "socket");
    myAddr.sin_family = AF_INET;
    myAddr.sin_port = htons(port);
    myAddr.sin_addr.s_addr = INADDR_ANY;

    int rc = bind(newSock, (struct sockaddr *)&myAddr, sizeof(struct sockaddr_in));
    checkError(rc, "bind");
    return newSock;
}
 
void checkTimeouts(int srvFd, char *fileCopy){          //Function to look for potential timeouts (clients who haven't ACK'ed)
    struct timeval now, timeout;
    timeout.tv_sec = 0;
    timeout.tv_usec = 100000;
    struct client *tmp = clients;
    while (tmp){
        gettimeofday(&now, NULL);
        if ((now.tv_usec + now.tv_sec * 1000000) - (tmp->time.tv_usec + tmp->time.tv_sec * 1000000) > timeout.tv_usec){
            if (checkAck(tmp)){
                tmp->sent = rdp_write(srvFd, srvID, tmp->ID, tmp->sent, &tmp->lastAdd, &tmp->seq, fileCopy, tmp->addr, sizeOfFile, &transferCnt, &tmp->time);
            }
        }
        tmp = tmp->next;
    }
}

int main(int argc, char *argv[]){
    int srvFd, port, maxTrans, curSend = 0, rc, alrCon, begun = 0;
    float lossProb;
    char buffer[BUFSIZE];
    struct sockaddr_in myAddress, cliAddress;
    struct timeval timeout, now;
    timeout.tv_sec = 3;
    timeout.tv_usec = 0;
    fd_set fds;
    socklen_t addressLength = sizeof(struct sockaddr_in);
    char *fileCopy, *fileName;

    
    if (argc != 5){         //Checks if the right amount of command line arguments are given
        fprintf(stderr, "Forventet bruk; '%s <portnummer> <filnavn> <antall filer> <tapssannsynlighet>'\n", argv[0]);
        return EXIT_SUCCESS;
    }
    port = atoi(argv[1]);
    fileName = argv[2];
    maxTrans = atoi(argv[3]);
    lossProb = atof(argv[4]);

    set_loss_probability(lossProb);                                     //Set the given loss probability

    srvFd = createNbind(myAddress, port);                               //Create the server socket and bind it

    fileCopy = readFile(fileName);                                      //Read the file to memory

    while (transferCnt < maxTrans){ 

        FD_ZERO(&fds);
        FD_SET(srvFd, &fds);
        timeout.tv_sec = 0;
        timeout.tv_usec = 100000;

        rc = select(FD_SETSIZE, &fds, NULL, NULL, &timeout);
        checkError(rc, "select");

        if (!rc){                                                       //rc = 0 indicates no communication
            checkTimeouts(srvFd, fileCopy);                             //Checks if there's any clients who haven't ACK'ed the last packet
        }

        if (FD_ISSET(srvFd, &fds)){
            rc = recvfrom(srvFd, buffer, BUFSIZE - 1, 0, (struct sockaddr *)&cliAddress, &addressLength);
            checkError(rc, "recvfrom");
            struct rdpPacket *pack = deSerialize(buffer);               

            struct client *newClient = rdp_accept(pack, cliAddress, clients, begun, maxTrans, srvID, srvFd);
            if (newClient){
                addClient(newClient);
                newClient->sent = rdp_write(srvFd, srvID, newClient->ID, newClient->sent, &newClient->lastAdd, &newClient->seq, 
                                            fileCopy, newClient->addr, sizeOfFile, &transferCnt, &newClient->time);             //Sends the first packet after new client is connected
                begun++;
            }
            if (pack->flag == 0x2){                                                 // Close connection between client and server
                printf("DISCONNECTED %d %d\n", pack->senderid, srvID);
                struct client *disc = findClient(pack->senderid);
                if (disc->sent != sizeOfFile){                                      //Checks if the connection closed before the file was done transferring.
                    removeClient(pack->senderid);
                    begun -= 1;
                }else{
                    removeClient(pack->senderid);
                    transferCnt += 1;
                }
                free(pack);
            }else if (pack->flag == 0x8){                               
                struct client *tmp = findClient(pack->senderid);
                tmp->ack = pack->ackNumb;
                if (!checkAck(tmp) && tmp->lastAdd){                //Checks if the ACK corresponds with the last sent packet, sends next packet if it does
                    tmp->sent = rdp_write(srvFd, srvID, tmp->ID, tmp->sent, &tmp->lastAdd, &tmp->seq, fileCopy, tmp->addr, sizeOfFile, &transferCnt, &tmp->time);
                }
                free(pack);
            }else{
                free(pack);
            }
        }
    }

    printf("All files are sent, closing down server..\n");
    sleep(1);

    close(srvFd);       //Closing socket
    free(fileCopy);     //Free the memory allocated for the file copy

    return EXIT_SUCCESS;
}
