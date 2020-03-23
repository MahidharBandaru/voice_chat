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
#include <pulse/simple.h>
#include <pulse/error.h>
#include <pulse/gccmacro.h>

#define BUFSIZE 1024
#define MAX_BUFFER 1024
#define _OPEN_SYS_ITOA_EXT

static int socketFd;
pthread_t serverThread, inputThread;

char* name;
extern int DEBUG;
#define BUFSIZE 1024

#define CALL(n, msg)                                              \
    if ((n) < 0) {                                                \
        fprintf(stderr, "%s (%s:%d)\n", msg, __FILE__, __LINE__); \
        exit(EXIT_FAILURE);                                       \
    }
#define LOG(...)                          \
    do {                                  \
        if (DEBUG) {                      \
            fprintf(stderr, __VA_ARGS__); \
            fprintf(stderr, "\n");        \
        }                                 \
    } while (0);

/* The Sample format to use */
static const pa_sample_spec ss = {
    .format = PA_SAMPLE_S16LE, .rate = 40100, .channels = 2};

/* A simple routine calling UNIX write() in a loop */
static ssize_t loop_write(int fd, const void* data, size_t size) {
    ssize_t ret = 0;
    while (size > 0) {
        ssize_t r;
        if ((r = write(fd, data, size)) < 0) return r;
        if (r == 0) break;
        ret += r;
        data = (const char*)data + r;
        size -= (size_t)r;
    }
    return ret;
}

/* A simple routine calling UNIX read() in a loop */
static ssize_t loop_read(int fd, void* data, size_t size) {
    ssize_t ret = 0;
    while (size > 0) {
        ssize_t r;
        if ((r = read(fd, data, size)) < 0) return r;
        if (r == 0) break;
        ret += r;
        data = (char*)data + r;
        size -= (size_t)r;
    }
    return ret;
}

int send_data(int fd, const void* data, size_t size) {
    return loop_write(fd, data, size);
}

int read_data(int fd, void* data, size_t size) {
    return loop_read(fd, data, size);
}

struct Connection {
    int sd;
    char* clientName;
};

// Receiver thread function
void* receive_voice_messages(void* ptr) {

    int sd = socketFd;
    char* clientName = name;

    pa_simple* s = NULL;
    int ret = 1;
    int error;

    /* Create a new playback stream */
    if (!(s = pa_simple_new(NULL, clientName, PA_STREAM_PLAYBACK, NULL,
                            "playback", &ss, NULL, NULL, &error))) {
        fprintf(stderr, __FILE__ ": pa_simple_new() failed: %s\n",
                pa_strerror(error));
        goto finish;
    }
    for (;;) {
        char buf[BUFSIZE];
        ssize_t r;
        // #if 1
        //         pa_usec_t latency;
        //         if ((latency = pa_simple_get_latency(s, &error)) ==
        //         (pa_usec_t) -1) {
        //             fprintf(stderr, __FILE__":
        //             pa_simple_get_playback_latency() failed: %s\n",
        //             pa_strerror(error)); goto finish;
        //         }
        //         fprintf(stderr, "%0.0f usec    \r", (float)latency);
        // #endif
        /* Read some data ... */
        if ((r = read(socketFd, buf, MAX_BUFFER)) <= 0) {
            if (r == 0) /* EOF */
                break;
            fprintf(stderr, __FILE__ ": read() failed: %s\n", strerror(errno));
            goto finish;
        }
        // loop_write(1, buf, sizeof(buf));
        /* ... and play it */
        if (pa_simple_write(s, buf, (size_t)r, &error) < 0) {
            fprintf(stderr, __FILE__ ": pa_simple_write() failed: %s\n",
                    pa_strerror(error));
            goto finish;
        }
    }
    /* Make sure that every single sample was played */
    if (pa_simple_drain(s, &error) < 0) {
        fprintf(stderr, __FILE__ ": pa_simple_drain() failed: %s\n",
                pa_strerror(error));
        goto finish;
    }
    ret = 0;
finish:
    if (s) pa_simple_free(s);

    return NULL;
}

void* send_voice_messages(void* ptr) {

    int sd = socketFd;
    char* clientName = name;

    printf("sd = %d\n", sd);
    printf("clientName = %s\n", clientName);

    pa_simple* s = NULL;
    int ret = 1;
    int error;
    /* Create the recording stream */

    if (!(s = pa_simple_new(NULL, clientName, PA_STREAM_RECORD, NULL, "record",
                            &ss, NULL, NULL, &error))) {
        fprintf(stderr, __FILE__ ": pa_simple_new() failed: %s\n",
                pa_strerror(error));
        goto finish;
    }
    for (;;) {
        char buf[BUFSIZE];
        /* Record some data ... */
        if (pa_simple_read(s, buf, sizeof(buf), &error) < 0) {
            fprintf(stderr, __FILE__ ": pa_simple_read() failed: %s\n",
                    pa_strerror(error));
            goto finish;
        }
        /* And write it to STDOUT */
        if (loop_write(sd, buf, sizeof(buf)) != sizeof(buf)) {
            fprintf(stderr, __FILE__ ": write() failed: %s\n", strerror(errno));
            goto finish;
        }
    }
    ret = 0;
finish:
    if (s) pa_simple_free(s);
    return NULL;
}




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
        printf("%s\n", msgBuffer);
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
            // total += diff;
            // n++;
            // printf("%lld %lld %lld %lld %lld %lld\n", n, a, b, time.s, time.us, diff);
            // printf("%s\n", msgBuffer);

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
        // usleep(40000);
        fgets(chatBuffer, MAX_BUFFER - 1, stdin);
        if (strcmp(chatBuffer, "/exit\n") == 0)
            interruptHandler(-1);
        else {
            // strcpy(chatBuffer, "hi there\n");
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
    signal(SIGINT, interruptHandler);

    fd_set clientFds;
    char chatMsg[MAX_BUFFER];
    char chatBuffer[MAX_BUFFER], msgBuffer[MAX_BUFFER];

    // main chatloop
    void* data;
    pthread_create(&serverThread, NULL, (void*)&send_voice_messages, data);
    pthread_create(&inputThread, NULL, (void*)&receive_voice_messages, data);
    
    pthread_join(serverThread, NULL);
    pthread_join(inputThread, NULL);
     
    return 0;
}
