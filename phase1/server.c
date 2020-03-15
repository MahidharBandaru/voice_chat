#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <pthread.h>
#include <pulse/error.h>
#include <pulse/simple.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>



#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

extern int DEBUG;
#define BUFSIZE 1024

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
    struct Connection connection = *(struct Connection*)ptr;

    int sd = connection.sd;
    char* clientName = connection.clientName;

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
        if ((r = read(sd, buf, sizeof(buf))) <= 0) {
            if (r == 0) /* EOF */
                break;
            fprintf(stderr, __FILE__ ": read() failed: %s\n", strerror(errno));
            goto finish;
        }
        /* ... and play it */
        // loop_write(1, buf, sizeof(buf));
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
    struct Connection connection = *(struct Connection*)ptr;

    int sd = connection.sd;
    char* clientName = connection.clientName;

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


int sd;  // socket descriptor
pthread_t receiver_thread;
pthread_t send_thread;

// Function to close resource handles and gracefully shutdown
void killServer();

// Interrupt handler
void handle_my(int sig);

// Server thread function
void *connection_handler(void *nsd);

int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("Usage: %s <server port>", argv[0]);
        exit(EXIT_FAILURE);
    }

    int port = atoi(argv[1]);
    sd = socket(AF_INET, SOCK_STREAM, 0);

    // acquiring port
    socklen_t clientLen;

    struct sockaddr_in server, client;

    server.sin_family = AF_INET;
    server.sin_addr.s_addr = htonl(INADDR_ANY);
    server.sin_port = htons(port);

    int trueV = 1;
    setsockopt(sd, SOL_SOCKET, SO_REUSEADDR, &trueV, sizeof(int));

    if (bind(sd, (struct sockaddr *)&server, sizeof(server)) < 0) {
        perror("bind failed");
        exit(EXIT_FAILURE);
    }

    listen(sd, 1);  // Waiting for connections

    printf("Waiting for the client...\n");

    clientLen = sizeof(client);

    int nsd = accept(sd, (struct sockaddr *)&client, &clientLen);
    struct Connection connection = {
        .sd = nsd,
        .clientName = argv[0],
    };
    printf("Connection Established\n");

    // Start receiver thread to receive call from others
    (pthread_create(&receiver_thread, NULL, receive_voice_messages,
                        (void *)&connection),
         "Receiver thread");

    // Start receiver thread to receive call from others
    (pthread_create(&send_thread, NULL, send_voice_messages,
                        (void *)&connection),
         "Send thread");

    pthread_join(receiver_thread, NULL);
    pthread_join(send_thread, NULL);
}

// Function to close resource handles and gracefully shutdown
void killServer() {
    char response[10];

    printf("Are you sure you want to close the server ? (Y/N) \n");
    scanf("%s", response);

    if (response[0] == 'Y' || response[0] == 'y') {
        pthread_kill(receiver_thread, SIGINT);
        pthread_kill(send_thread, SIGINT);
        close(sd);

        exit(EXIT_SUCCESS);
    }
}

// Interrupt handler
void handle_my(int sig) {
    switch (sig) {
        case SIGINT:
            killServer();
            break;
    }
}