#include "ntp.h"
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#define MAX_BUFFER 1024
#define _OPEN_SYS_ITOA_EXT

static int socketFd;
pthread_t serverThread, inputThread;

char* name;

//Concatenates the name with the message and puts it into result
void buildMessage(char* result, char* name, char* msg)
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
void setupAndConnect(struct sockaddr_in* serverAddr, struct hostent* host, int socketFd, long port)
{
    memset(serverAddr, 0, sizeof(serverAddr));
    serverAddr->sin_family = AF_INET;
    serverAddr->sin_addr = *((struct in_addr*)host->h_addr_list[0]);
    serverAddr->sin_port = htons(port);
    if (connect(socketFd, (struct sockaddr*)serverAddr, sizeof(struct sockaddr)) < 0) {
        perror("Couldn't connect to server");
        exit(1);
    }
}

//Notify the server when the client exits by sending "/exit"
void interruptHandler(int sig_unused)
{
    if (write(socketFd, "/exit\n", MAX_BUFFER - 1) == -1)
        perror("write failed: ");
    pthread_exit(&serverThread);

    close(socketFd);
    pthread_exit(&inputThread);
}
long long n = 0, total = 0;
void* readFromServer(void* data)
{
    char chatMsg[MAX_BUFFER];
    char chatBuffer[MAX_BUFFER], msgBuffer[MAX_BUFFER];
    while (1) {

        int numBytesRead = read(socketFd, msgBuffer, MAX_BUFFER - 1);
        struct t_format time = gettime();

        msgBuffer[numBytesRead] = '\0';
        long long a, b;
        char buf[256];
        sscanf(msgBuffer, "%lld %lld", &a, &b);
        int spaces = 0;
        char* temp = msgBuffer;
        while (spaces < 2) {
            if (*temp == ' ')
                spaces++;
            temp++;
        }
        struct t_format msg_gen_time = { a, b };
        long long diff = timediff(msg_gen_time, time);
        if (n < 10) {
            total += diff;
            n++;
            printf("%lld %lld %lld %lld %lld %lld\n", n, a, b, time.s, time.us, diff);

        } else {
            long double avg_delay = (total * 1.0) / n;
            printf("%Lf\n", avg_delay);
            interruptHandler(-1);
        }
        memset(&msgBuffer, 0, sizeof(msgBuffer));
    }
}

void* writeToServer(void* data)
{
    char chatMsg[MAX_BUFFER];
    signal(SIGINT, interruptHandler);

    char chatBuffer[MAX_BUFFER], msgBuffer[MAX_BUFFER];

    while (1) {
        usleep(100000);
        // fgets(chatBuffer, MAX_BUFFER - 1, stdin);
        if (strcmp(chatBuffer, "/exit\n") == 0)
            interruptHandler(-1);
        else {
            strcpy(chatBuffer, "hi there\n");
            buildMessage(chatMsg, name, chatBuffer);
            if (write(socketFd, chatMsg, MAX_BUFFER - 1) == -1)
                perror("write failed: ");
            memset(&chatBuffer, 0, sizeof(chatBuffer));
        }
    }
}

int main(int argc, char* argv[])
{
    ntp_init();
    struct sockaddr_in serverAddr;
    struct hostent* host;
    long port;

    if (argc != 5) {
        fprintf(stderr, "./client [username] [host] [port] [groupId]\n");
        exit(1);
    }
    name = argv[1];
    if ((host = gethostbyname(argv[2])) == NULL) {
        fprintf(stderr, "Couldn't get host name\n");
        exit(1);
    }
    port = strtol(argv[3], NULL, 0);
    if ((socketFd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        fprintf(stderr, "Couldn't create socket\n");
        exit(1);
    }
    char* groupId = argv[4];

    setupAndConnect(&serverAddr, host, socketFd, port);

    write(socketFd, groupId, sizeof(groupId));

    //Set a handler for the interrupt signal
    // signal(SIGINT, interruptHandler);

    fd_set clientFds;
    char chatMsg[MAX_BUFFER];
    char chatBuffer[MAX_BUFFER], msgBuffer[MAX_BUFFER];

    // main chatloop
    void* data;
    // pthread_create(&serverThread, NULL, (void*)&readFromServer, data);
    pthread_create(&inputThread, NULL, (void*)&writeToServer, data);
    pthread_join(inputThread, NULL);
    return 0;
}
