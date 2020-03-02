#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <netdb.h>
#include <sys/select.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <signal.h>
#include "ntp.h"

#define MAX_BUFFER 1024
#define _OPEN_SYS_ITOA_EXT

static int socketFd;



//Concatenates the name with the message and puts it into result
void buildMessage(char *result, char *name, char *msg)
{
    struct t_format time = gettime();
    memset(result, 0, MAX_BUFFER);
    char buffer[256];
    sprintf(buffer, "%lld", time.s);
    strcpy(result, buffer);
    strcat(result, " ");

    sprintf(buffer, "%lld", time.us);
    strcat(result, buffer);
    strcat(result, " ");

    strcat(result, name);
    strcat(result, ": ");
    strcat(result, msg);
}

//Sets up the socket and connects
void setupAndConnect(struct sockaddr_in *serverAddr, struct hostent *host, int socketFd, long port)
{
    memset(serverAddr, 0, sizeof(serverAddr));
    serverAddr->sin_family = AF_INET;
    serverAddr->sin_addr = *((struct in_addr *)host->h_addr_list[0]);
    serverAddr->sin_port = htons(port);
    if(connect(socketFd, (struct sockaddr *) serverAddr, sizeof(struct sockaddr)) < 0)
    {
        perror("Couldn't connect to server");
        exit(1);
    }
}

//Sets the fd to nonblocking
// used non blocking io here
// resource: https://github.com/angrave/SystemProgramming/wiki/Networking,-Part-7:-Nonblocking-I-O,-select(),-and-epoll
void setupNonBlocking(int fd)
{
    int flags = fcntl(fd, F_GETFL);
    if(flags < 0)
        perror("fcntl failed");

    flags |= O_NONBLOCK;
    fcntl(fd, F_SETFL, flags);
}

//Notify the server when the client exits by sending "/exit"
void interruptHandler(int sig_unused)
{
    if(write(socketFd, "/exit\n", MAX_BUFFER - 1) == -1)
        perror("write failed: ");

    close(socketFd);
    exit(1);
}

int main(int argc, char *argv[])
{
    ntp_init();
    char *name;
    struct sockaddr_in serverAddr;
    struct hostent *host;
    long port;

    if(argc != 5)
    {
        fprintf(stderr, "./client [username] [host] [port] [groupId]\n");
        exit(1);
    }
    name = argv[1];
    if((host = gethostbyname(argv[2])) == NULL)
    {
        fprintf(stderr, "Couldn't get host name\n");
        exit(1);
    }
    port = strtol(argv[3], NULL, 0);
    if((socketFd = socket(AF_INET, SOCK_STREAM, 0))== -1)
    {
        fprintf(stderr, "Couldn't create socket\n");
        exit(1);
    }
    char *groupId = argv[4];

    setupAndConnect(&serverAddr, host, socketFd, port);
    setupNonBlocking(socketFd);
    setupNonBlocking(0);

    write(socketFd, groupId, sizeof(groupId));

    //Set a handler for the interrupt signal
    signal(SIGINT, interruptHandler);

    fd_set clientFds;
    char chatMsg[MAX_BUFFER];
    char chatBuffer[MAX_BUFFER], msgBuffer[MAX_BUFFER];

    // main chatloop
    while(1)
    {
        // used non blocking io here
        // resource: https://github.com/angrave/SystemProgramming/wiki/Networking,-Part-7:-Nonblocking-I-O,-select(),-and-epoll
        FD_ZERO(&clientFds);
        FD_SET(socketFd, &clientFds);
        FD_SET(0, &clientFds);
        if(select(FD_SETSIZE, &clientFds, NULL, NULL, NULL) != -1) //wait for an available fd
        {
            for(int fd = 0; fd < FD_SETSIZE; fd++)
            {
                if(FD_ISSET(fd, &clientFds))
                {
                    //to process data from server
                    if(fd == socketFd) 
                    {
                        int numBytesRead = read(socketFd, msgBuffer, MAX_BUFFER - 1);
                        msgBuffer[numBytesRead] = '\0';
                        printf("%s", msgBuffer);
                        memset(&msgBuffer, 0, sizeof(msgBuffer));
                    }
                    // for keyboard io
                    else if(fd == 0) 
                    {
                        fgets(chatBuffer, MAX_BUFFER - 1, stdin);
                        if(strcmp(chatBuffer, "/exit\n") == 0)
                            interruptHandler(-1); 
                        else
                        {
                            buildMessage(chatMsg, name, chatBuffer);
                            if(write(socketFd, chatMsg, MAX_BUFFER - 1) == -1) perror("write failed: ");
                            memset(&chatBuffer, 0, sizeof(chatBuffer));
                        }
                    }
                }
            }
        }
    }
}
