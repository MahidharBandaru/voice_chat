#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <signal.h>

#define MAX_BUFFER 1024
#define MAX_CHATBOXES 3

typedef struct {
    uint8_t* buffer[MAX_BUFFER];
    int head, tail;
    int full, empty;
    pthread_mutex_t* mutex;
    pthread_cond_t *notFull, *notEmpty;
} queue;

typedef struct temp chatData;
typedef struct {
    fd_set serverReadFds;
    int socketFd;
    int clientSockets[MAX_BUFFER];
    int numClients;
    pthread_mutex_t* clientListMutex;
    queue* queue;
    chatData* data;
} chatDataVars;

typedef struct {
    chatDataVars* data;
    int clientSocketFd;
} clientHandlerVars;

typedef struct temp {
    chatDataVars* chatBox[MAX_CHATBOXES];
    int socketFd;
    char* chatBoxName[MAX_CHATBOXES];
    int tally;
    pthread_mutex_t* chatBoxListMutex;
} chatData;

void buildMessage(char* result, char* name, char* msg);
void removeClient(chatDataVars* data, int clientSocketFd);

void* newClientHandler(void* data);
void* clientHandler(void* chv);
void* messageHandler(void* data);

//Initializes queue
queue* queueInit(void)
{
    queue* q = (queue*)malloc(sizeof(queue));

    q->empty = 1;
    q->full = q->head = q->tail = 0;
    q->mutex = (pthread_mutex_t*)malloc(sizeof(pthread_mutex_t));
    pthread_mutex_init(q->mutex, NULL);

    q->notFull = (pthread_cond_t*)malloc(sizeof(pthread_cond_t));
    pthread_cond_init(q->notFull, NULL);

    q->notEmpty = (pthread_cond_t*)malloc(sizeof(pthread_cond_t));
    pthread_cond_init(q->notEmpty, NULL);

    return q;
}

//Frees a queue
void queueDestroy(queue* q)
{
    pthread_mutex_destroy(q->mutex);
    pthread_cond_destroy(q->notFull);
    pthread_cond_destroy(q->notEmpty);
    free(q->mutex);
    free(q->notFull);
    free(q->notEmpty);
    free(q);
}

//Push to end of queue
void queuePush(queue* q, char* msg)
{
    q->buffer[q->tail] = msg;
    q->tail++;
    if (q->tail == MAX_BUFFER)
        q->tail = 0;
    if (q->tail == q->head)
        q->full = 1;
    q->empty = 0;
}

//Pop front of queue
char* queuePop(queue* q)
{
    char* msg = q->buffer[q->head];
    q->head++;
    if (q->head == MAX_BUFFER)
        q->head = 0;
    if (q->head == q->tail)
        q->empty = 1;
    q->full = 0;

    return msg;
}

int main(int argc, char* argv[])
{
    // signal(SIGPIPE, SIG_IGN);
    struct sockaddr_in serverAddr;
    long port = 9999;
    int socketFd;

    if (argc == 2)
        port = strtol(argv[1], NULL, 0);

    if ((socketFd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        perror("Socket creation failed");
        exit(1);
    }

    // binding socket
    memset(&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = htonl(INADDR_ANY);
    serverAddr.sin_port = htons(port);

    if (bind(socketFd, (struct sockaddr*)(&serverAddr), sizeof(struct sockaddr_in)) == -1) {
        perror("Socket bind failed: ");
        exit(1);
    }

    // listening on the socket
    if (listen(socketFd, 100) == -1) {
        perror("listen failed: ");
        exit(1);
    }

    chatData data;
    data.tally = 0;
    pthread_mutex_init(data.chatBoxListMutex, NULL);
    data.socketFd = socketFd;

    //Start thread to handle new client connections
    pthread_t connectionThread;
    if ((pthread_create(&connectionThread, NULL, (void*)&newClientHandler, (void*)&data)) == 0) {
        fprintf(stderr, "Connection handler started\n");
    }

    pthread_join(connectionThread, NULL);
    // newClientHandler(&data);

    close(socketFd);
}

//Removes the socket from the list of active client sockets and closes it threada
void removeClient(chatDataVars* data, int clientSocketFd)
{
    for (int i = 0; i < MAX_BUFFER; i++) {
    pthread_mutex_lock(data->clientListMutex);

        if (data->clientSockets[i] == clientSocketFd) {
            data->clientSockets[i] = 0;
            close(clientSocketFd);
            data->numClients--;
            i = MAX_BUFFER;
        }
    pthread_mutex_unlock(data->clientListMutex);

    }
}

int registerClient(chatData* info, char* msgBuffer, int clientSocketFd)
{
    int found = 0;
    pthread_mutex_lock(info->chatBoxListMutex);
    int numberOfGroups = info->tally;

    for (int i = 0; i < numberOfGroups; i++) {
        fprintf(stderr, "%s %s \n", msgBuffer, info->chatBoxName[i]);

        if (strcmp(msgBuffer, info->chatBoxName[i]) == 0) {
            //Obtain lock on clients list and add new client in

            chatDataVars* vars = ((info->chatBox)[i]);

            pthread_mutex_lock(vars->clientListMutex);
            if (vars->numClients < MAX_BUFFER) {
                //Add new client to list
                for (int i = 0; i < MAX_BUFFER; i++) {
                    if (!FD_ISSET(vars->clientSockets[i], &(vars->serverReadFds))) {
                        vars->clientSockets[i] = clientSocketFd;
                        i = MAX_BUFFER;
                    }
                }

                FD_SET(clientSocketFd, &(vars->serverReadFds));

                //Spawn new thread to handle client's messages
                clientHandlerVars* chv = (clientHandlerVars*)malloc(sizeof(clientHandlerVars));
                chv->clientSocketFd = clientSocketFd;
                chv->data = vars;

                pthread_t clientThread;
                if ((pthread_create(&clientThread, NULL, (void*)&clientHandler, (void*)chv)) == 0) {
                    vars->numClients++;
                    fprintf(stderr, "Client has joined chat. Socket: %d\n", clientSocketFd);
                } else
                    close(clientSocketFd);
            }
            pthread_mutex_unlock(vars->clientListMutex);
            found = 1;
        }
    }

    pthread_mutex_unlock(info->chatBoxListMutex);
    return found;
}

// thread to handle creation of new room
void* roomHandler(void* data)
{
    chatDataVars* info = (chatDataVars*)data;

    pthread_t messagesThread;

    if ((pthread_create(&messagesThread, NULL, (void*)&messageHandler, (void*)&(*info))) == 0) {
        fprintf(stderr, "Message handler started\n");
    }

    pthread_join(messagesThread, NULL);
    // messageHandler(vars);

    queueDestroy(info->queue);
    pthread_mutex_destroy(info->clientListMutex);
    free(info->clientListMutex);
    info->data->tally--;
}

//Thread to handle new connections. Adds client's fd to list of client fds and spawns a new clientHandler thread for it
void* newClientHandler(void* data)
{
    // chatDataVars *chatData = (chatDataVars *) data;
    chatData* info = (chatData*)data;
    char msgBuffer[MAX_BUFFER];
    int a = 1;
    setsockopt(info->socketFd, SOL_SOCKET, SO_REUSEADDR, &a, sizeof(int));
    while (1) {

        int clientSocketFd = accept(info->socketFd, NULL, NULL);
        if (clientSocketFd > 0) {

            fprintf(stderr, "Server accepted new client. Socket: %d\n", clientSocketFd);
            int numBytesRead = read(clientSocketFd, msgBuffer, MAX_BUFFER - 1);
            msgBuffer[numBytesRead] = '\0';
            fprintf(stderr, "Requested group is %s\n", msgBuffer);

            int found = registerClient(info, msgBuffer, clientSocketFd);

            if (found == 0) {
                // pthread_mutex_lock(info->chatBoxListMutex);
                int numberOfGroups = info->tally;
                if (numberOfGroups == MAX_CHATBOXES) {
                    char* msg = "N";
                    write(clientSocketFd, msg, sizeof(msg));
                    fprintf(stderr, "Cant create new group\n");
                    close(clientSocketFd);
                } else {
                    pthread_t roomThread;
                    chatDataVars* vars = (chatDataVars*)malloc(sizeof(chatDataVars));

                    vars->numClients = 0;
                    vars->socketFd = info->socketFd;
                    vars->queue = queueInit();
                    vars->data = info;
                    FD_ZERO(&(vars->serverReadFds));
                    FD_SET(vars->socketFd, &(vars->serverReadFds));
                    vars->clientListMutex = (pthread_mutex_t*)malloc(sizeof(pthread_mutex_t));
                    pthread_mutex_init(vars->clientListMutex, NULL);
                    info->chatBoxName[info->tally] = (char*)malloc(strlen(msgBuffer));
                    strcpy(info->chatBoxName[info->tally], msgBuffer);
                    (info->chatBox)[info->tally] = vars;
                    info->tally++;
                    if ((pthread_create(&roomThread, NULL, (void*)&roomHandler, (void*)&(*vars))) == 0) {
                        // roomHandler(info);

                        fprintf(stderr, "New Group Created %s\n", msgBuffer);

                        registerClient(info, msgBuffer, clientSocketFd);
                    } else {
                        fprintf(stderr, "Failed\n");
                    }
                }
                // pthread_mutex_unlock(info->chatBoxListMutex);
            }
            // write found non zero
        }
    }
}

void* clientHandler(void* chv)
{

    clientHandlerVars* vars = (clientHandlerVars*)chv;
    chatDataVars* data = (chatDataVars*)vars->data;

    queue* q = data->queue;
    int clientSocketFd = vars->clientSocketFd;

    char msgBuffer[MAX_BUFFER];

    while (1) {
        int numBytesRead = read(clientSocketFd, msgBuffer, MAX_BUFFER - 1);
        msgBuffer[numBytesRead] = '\0';

        //If the client sent /exit\n, remove them from the client list and close their socket
        if (strcmp(msgBuffer, "/exit\n") == 0) {
            fprintf(stderr, "Client on socket %d has disconnected.\n", clientSocketFd);
            removeClient(data, clientSocketFd);
            return NULL;
        } else {
            while (q->full) {
                pthread_cond_wait(q->notFull, q->mutex);
            }

            pthread_mutex_lock(q->mutex);
            fprintf(stderr, "Pushing message to queue: %s\n", msgBuffer);
            queuePush(q, msgBuffer);
            pthread_mutex_unlock(q->mutex);
            pthread_cond_signal(q->notEmpty);
        }
    }
}

void* messageHandler(void* data)
{
    chatDataVars* chatData = (chatDataVars*)data;
    queue* q = chatData->queue;
    int* clientSockets = chatData->clientSockets;

    while (1) {
        //Obtain lock and pop message from queue when not empty
        pthread_mutex_lock(q->mutex);
        while (q->empty) {
            pthread_cond_wait(q->notEmpty, q->mutex);
        }
        char* msg = queuePop(q);
        pthread_mutex_unlock(q->mutex);
        pthread_cond_signal(q->notFull);

        //Broadcast message to all connected clients
        fprintf(stderr, "Broadcasting message: %s\n", msg);
        for (int i = 0; i < MAX_BUFFER; i++) {
            pthread_mutex_lock(chatData->clientListMutex);
            int socket = clientSockets[i];
            if (socket != 0 && write(socket, msg, MAX_BUFFER - 1) == -1)
                perror("Socket write failed: ");
            else if(socket != 0)
                fprintf(stderr, "Broadcasting to %d\n", clientSockets[i]);
            pthread_mutex_unlock(chatData->clientListMutex);
        }
    }
}