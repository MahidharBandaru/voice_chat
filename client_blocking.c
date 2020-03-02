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
#define BUFSIZE 1024

#define _OPEN_SYS_ITOA_EXT
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include <pulse/error.h>
#include <pulse/gccmacro.h>
#include <pulse/simple.h>

static int socketFd;
pthread_t serverThread, inputThread;

char* name;
pa_simple* s = NULL;

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

void* readFromServer(void* data)
{
    char chatMsg[MAX_BUFFER];
    char chatBuffer[MAX_BUFFER], msgBuffer[MAX_BUFFER];
    while (1) {
        int numBytesRead = read(socketFd, msgBuffer, MAX_BUFFER - 1);
        msgBuffer[numBytesRead] = '\0';
        printf("%s", msgBuffer);
        memset(&msgBuffer, 0, sizeof(msgBuffer));
    }
}

void* writeToServer(void* data)
{
    uint8_t chatMsg[MAX_BUFFER];
    uint8_t buf[BUFSIZE];

    signal(SIGINT, interruptHandler);

    while (1) {

        /* Record some data ... */
        if (pa_simple_read(s, buf, sizeof(buf), &error) < 0) {
            fprintf(stderr, __FILE__ ": pa_simple_read() failed: %s\n", pa_strerror(error));
            interruptHandler(-1);
        } else {
            buildMessage(chatMsg, name, buf);
            if (write(socketFd, chatMsg, MAX_BUFFER - 1) == -1)
                perror("write failed: ");
            memset(&buf, 0, sizeof(buf));
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
    static const pa_sample_spec ss = {
        .format = PA_SAMPLE_S16LE,
        .rate = 44100,
        .channels = 2
    };
    int ret = 1;
    int error;

    /* Create the recording stream */
    if (!(s = pa_simple_new(NULL, argv[0], PA_STREAM_RECORD, NULL, "record", &ss, NULL, NULL, &error))) {
        fprintf(stderr, __FILE__ ": pa_simple_new() failed: %s\n", pa_strerror(error));
        return 0;
    }

    // main chatloop
    void* data;
    pthread_create(&serverThread, NULL, (void*)&readFromServer, data);
    pthread_create(&inputThread, NULL, (void*)&writeToServer, data);
    pthread_join(inputThread, NULL);
    return 0;
}
