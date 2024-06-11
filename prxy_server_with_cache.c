#include "proxy_parse.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <sys/wait.h>
#include <errno.h>
#include <pthread.h>
#include <semaphore.h>
#include <time.h>
#define MAX_CLIENTS 10
#define MAX_CACHE_SIZE 1000000
#define MAX_BYTES 4096

struct cacheElement
{
    char *data;
    int len;
    char *url;
    time_t lru_time_track;
    struct cacheElement *next;
};

struct cacheElement *find(char *url);
int add_cacheElement(char *data, int size, char *url);
void remove_cacheElement();

int port_number = 8080;
int proxy_socketId;
pthread_t thread_id[MAX_CLIENTS];
sem_t semaphore;
pthread_mutex_t lock;

struct cacheElement *head;
int cache_size;

void *thread_fn(void *socketNew)
{
    sem_wait(&semaphore);
    int p;
    sem_getvalue(&semaphore, &p);
    printf("semaphore value:%d\n", p);
    int *t = (int *)(socketNew);
    int socket = *t;            // Socket is socket descriptor of the connected Client
    int bytes_send_client, len; // Bytes Transferred

    char *buffer = (char *)calloc(MAX_BYTES, sizeof(char)); // Creating buffer of 4kb for a client

    bzero(buffer, MAX_BYTES);                               // Making buffer zero
    bytes_send_client = recv(socket, buffer, MAX_BYTES, 0); // Receiving the Request of client by proxy server

    while (bytes_send_client > 0)
    {
        len = strlen(buffer);
        // loop until u find "\r\n\r\n" in the buffer
        if (strstr(buffer, "\r\n\r\n") == NULL)
        {
            bytes_send_client = recv(socket, buffer + len, MAX_BYTES - len, 0);
        }
        else
        {
            break;
        }
    }

    // printf("--------------------------------------------\n");
    // printf("%s\n",buffer);
    // printf("----------------------%d----------------------\n",strlen(buffer));

    char *tempReq = (char *)malloc(strlen(buffer) * sizeof(char) + 1);
    // tempReq, buffer both store the http request sent by client
    for (int i = 0; i < strlen(buffer); i++)
    {
        tempReq[i] = buffer[i];
    }

    struct cacheElement *temp = find(tempReq);

    if (temp != NULL)
    {
        // request found in cache, so sending the response to client from proxy's cache
        int size = temp->len / sizeof(char);
        int pos = 0;
        char response[MAX_BYTES];
        while (pos < size)
        {
            bzero(response, MAX_BYTES);
            for (int i = 0; i < MAX_BYTES; i++)
            {
                response[i] = temp->data[pos];
                pos++;
            }
            send(socket, response, MAX_BYTES, 0);
        }
        printf("Data retrived from the Cache\n\n");
        printf("%s\n\n", response);
        // close(socketNew);
        // sem_post(&seamaphore);
        // return NULL;
    }

    else if (bytes_send_client > 0)
    {
        len = strlen(buffer);
        // Parsing the request
        struct ParsedRequest *request = ParsedRequest_create();

        // ParsedRequest_parse returns 0 on success and -1 on failure.On success it stores parsed request in
        //  the request
        if (ParsedRequest_parse(request, buffer, len) < 0)
        {
            printf("Parsing failed\n");
        }
        else
        {
            bzero(buffer, MAX_BYTES);
            if (!strcmp(request->method, "GET"))
            {

                if (request->host && request->path && (checkHTTPversion(request->version) == 1))
                {
                    bytes_send_client = handle_request(socket, request, tempReq); // Handle GET request
                    if (bytes_send_client == -1)
                    {
                        sendErrorMessage(socket, 500);
                    }
                }
                else
                    sendErrorMessage(socket, 500); // 500 Internal Error
            }
            else
            {
                printf("This code doesn't support any method other than GET\n");
            }
        }
        // freeing up the request pointer
        ParsedRequest_destroy(request);
    }

    else if (bytes_send_client < 0)
    {
        perror("Error in receiving from client.\n");
    }
    else if (bytes_send_client == 0)
    {
        printf("Client disconnected!\n");
    }

    shutdown(socket, SHUT_RDWR);
    close(socket);
    free(buffer);
    sem_post(&semaphore);

    sem_getvalue(&semaphore, &p);
    printf("Semaphore post value:%d\n", p);
    free(tempReq);
    return NULL;
}

int main(int argc, char *argv[])
{
    int client_socketId, client_len;
    struct sockaddr_in server_addr, client_addr;
    struct ParsedRequest *request;
    sem_init(&semaphore, 0, MAX_CLIENTS); // 0 means semaphore is shared between threads and 1 means semaphore is shared between processes
    pthread_mutex_init(&lock, NULL);      // null else garbage
    if (argv == 2)
    {
        port_number = atoi(argv[1]);
        // when we write in console ./proxy 8080 then  ./proxy 0 and port number 8080 1 as it takes it as a string
    }
    else
    {
        printf("too few arguments\n");
        exit(1); // exit with error agar exit(0) likhenge that means error nahi chal ke exit hogya
    }

    printf("Proxy server started at port %d\n", port_number);
    proxy_socketId = socket(AF_INET, SOCK_STREAM, 0); // this will be the main socket as whatever request comes it will come here and it will spawn a new thread for that request thus creating a new socket for that request
    // and every time the same request comes it will be handled by the same thread thus the same socket will be used
    // it will create new socket for every new request
    // AF_INET is the address family for IPv4 and SOCK_STREAM is the type of socket we want to create and it ensures that whatever happens happens on tcp and 0 is the protocol
    if (proxy_socketId < 0) // if the socket is not created it returns negative
    {
        perror("Error in creating socket\n");
        exit(1);
    }

    int reuse = 1;                                                                                     // to use the main socket again and again
    if (setsockopt(proxy_socketId, SOL_SOCKET, SO_REUSEADDR, (const char *)&reuse, sizeof(reuse)) < 0) // setsockopt is used to set the options for the socket and sol_socket is the level of the socket and so_reuseaddr is the option we want to set and reuse is the value we want to set it to
    {
        perror("Error in setting socket options\n");
        exit(1);
    }
    bzero((char *)&server_addr, sizeof(server_addr)); // bzero is used to set all the values of the server_addr to 0 else it will have garbage values and size is to know when to stop
    server_addr.sin_family = AF_INET;                 // setting the address family to ipv4
    server_addr.sin_port = htons(port_number);        // htons is used to convert the port number to network byte order basically saying that the port number is in the right format
    server_addr.sin_addr.s_addr = INADDR_ANY;         // give any address to the server

    if (bind(proxy_socketId, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) // Binding a socket is the process of assigning a port number to a socket. This is necessary for the socket to be able to receive data. When a socket is created, it does not have a port number assigned to it. Binding a socket to a port number allows it to receive data from other sockets on the same network
    {
        perror("Error in binding\n");
        exit(1);
    }
    printf("binding on port %d\n", port_number);
    int listen_status = listen(proxy_socketId, MAX_CLIENTS);
    if (listen_status < 0)
    {
        perror("Error in listening\n");
        exit(1);
    }
    int i = 0;
    int Connected_SocketId[MAX_CLIENTS];
    while (1)
    {
        bzero((char *)&client_addr, sizeof(client_addr)); // setting all the values of the client_addr to 0 cleaning
        client_len = sizeof(client_addr);
        client_socketId = accept(proxy_socketId, (struct sockaddr *)&client_addr, (socklen_t *)&client_len); // accept is used to accept the incoming request and it returns the socket id of the client
        if (client_socketId < 0)
        {
            perror("Error in accepting\n");
            exit(1);
        }
        Connected_SocketId[i] = client_socketId;

        struct sockaddr_in *client_pt = (struct sockaddr_in *)&client_addr; // creating a pointer to the client_addr
        struct in_addr ipAddr = client_pt->sin_addr;                        // getting the ip address of the client
        char str[INET_ADDRSTRLEN];                                          // creating a string to store the ip address
        inet_ntop(AF_INET, &ipAddr, str, INET_ADDRSTRLEN);                  // converting the ip address to string
        printf("client connected with ip address %s\n and port number %d\n", str, ntohs(client_addr.sin_port));

        pthread_create(&thread_id[i], NULL, thread_fn, (void *)&Connected_SocketId[i]); // creating a new thread for the request
        i++;
    }
    close(proxy_socketId);
    return 0;
}