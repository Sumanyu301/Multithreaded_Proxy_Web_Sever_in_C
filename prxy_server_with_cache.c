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
#define MAX_ELEMENT_SIZE 1000000000
#define MAX_SIZE 1000000000
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

int sendErrorMessage(int socket, int status_code)
{
    char str[1024];
    char currentTime[50];
    time_t now = time(0);

    struct tm data = *gmtime(&now);
    strftime(currentTime, sizeof(currentTime), "%a, %d %b %Y %H:%M:%S %Z", &data);

    switch (status_code)
    {
    case 400:
        snprintf(str, sizeof(str), "HTTP/1.1 400 Bad Request\r\nContent-Length: 95\r\nConnection: keep-alive\r\nContent-Type: text/html\r\nDate: %s\r\nServer: VaibhavN/14785\r\n\r\n<HTML><HEAD><TITLE>400 Bad Request</TITLE></HEAD>\n<BODY><H1>400 Bad Rqeuest</H1>\n</BODY></HTML>", currentTime);
        printf("400 Bad Request\n");
        send(socket, str, strlen(str), 0);
        break;

    case 403:
        snprintf(str, sizeof(str), "HTTP/1.1 403 Forbidden\r\nContent-Length: 112\r\nContent-Type: text/html\r\nConnection: keep-alive\r\nDate: %s\r\nServer: VaibhavN/14785\r\n\r\n<HTML><HEAD><TITLE>403 Forbidden</TITLE></HEAD>\n<BODY><H1>403 Forbidden</H1><br>Permission Denied\n</BODY></HTML>", currentTime);
        printf("403 Forbidden\n");
        send(socket, str, strlen(str), 0);
        break;

    case 404:
        snprintf(str, sizeof(str), "HTTP/1.1 404 Not Found\r\nContent-Length: 91\r\nContent-Type: text/html\r\nConnection: keep-alive\r\nDate: %s\r\nServer: VaibhavN/14785\r\n\r\n<HTML><HEAD><TITLE>404 Not Found</TITLE></HEAD>\n<BODY><H1>404 Not Found</H1>\n</BODY></HTML>", currentTime);
        printf("404 Not Found\n");
        send(socket, str, strlen(str), 0);
        break;

    case 500:
        snprintf(str, sizeof(str), "HTTP/1.1 500 Internal Server Error\r\nContent-Length: 115\r\nConnection: keep-alive\r\nContent-Type: text/html\r\nDate: %s\r\nServer: VaibhavN/14785\r\n\r\n<HTML><HEAD><TITLE>500 Internal Server Error</TITLE></HEAD>\n<BODY><H1>500 Internal Server Error</H1>\n</BODY></HTML>", currentTime);
        // printf("500 Internal Server Error\n");
        send(socket, str, strlen(str), 0);
        break;

    case 501:
        snprintf(str, sizeof(str), "HTTP/1.1 501 Not Implemented\r\nContent-Length: 103\r\nConnection: keep-alive\r\nContent-Type: text/html\r\nDate: %s\r\nServer: VaibhavN/14785\r\n\r\n<HTML><HEAD><TITLE>404 Not Implemented</TITLE></HEAD>\n<BODY><H1>501 Not Implemented</H1>\n</BODY></HTML>", currentTime);
        printf("501 Not Implemented\n");
        send(socket, str, strlen(str), 0);
        break;

    case 505:
        snprintf(str, sizeof(str), "HTTP/1.1 505 HTTP Version Not Supported\r\nContent-Length: 125\r\nConnection: keep-alive\r\nContent-Type: text/html\r\nDate: %s\r\nServer: VaibhavN/14785\r\n\r\n<HTML><HEAD><TITLE>505 HTTP Version Not Supported</TITLE></HEAD>\n<BODY><H1>505 HTTP Version Not Supported</H1>\n</BODY></HTML>", currentTime);
        printf("505 HTTP Version Not Supported\n");
        send(socket, str, strlen(str), 0);
        break;

    default:
        return -1;
    }
    return 1;
}

int connectRemoteServer(char *host_addr, int port_num)
{
    // Creating Socket for remote server ---------------------------

    int remoteSocket = socket(AF_INET, SOCK_STREAM, 0);

    if (remoteSocket < 0)
    {
        printf("Error in Creating Socket.\n");
        return -1;
    }

    // Get host by the name or ip address provided

    struct hostent *host = gethostbyname(host_addr);
    if (host == NULL)
    {
        fprintf(stderr, "No such host exists.\n");
        return -1;
    }

    // inserts ip address and port number of host in struct `server_addr`
    struct sockaddr_in server_addr;

    bzero((char *)&server_addr, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port_num);
    bcopy((char *)&host->h_addr_list[0], (char *)&server_addr.sin_addr.s_addr, host->h_length);

    // Connect to Remote server ----------------------------------------------------

    if (connect(remoteSocket, (struct sockaddr *)&server_addr, (socklen_t)sizeof(server_addr)) < 0)
    {
        fprintf(stderr, "Error in connecting !\n");
        return -1;
    }
    // free(host_addr);
    return remoteSocket;
}

int handle_request(int clientSocket, struct ParsedRequest *request, char *tempReq)
{
    char *buf = (char *)malloc(sizeof(char) * MAX_BYTES);
    strcpy(buf, "GET ");
    strcat(buf, request->path);
    strcat(buf, " ");
    strcat(buf, request->version);
    strcat(buf, "\r\n");

    size_t len = strlen(buf);

    if (ParsedHeader_set(request, "Connection", "close") < 0)
    {
        printf("set header key not work\n");
    }

    if (ParsedHeader_get(request, "Host") == NULL)
    {
        if (ParsedHeader_set(request, "Host", request->host) < 0)
        {
            printf("Set \"Host\" header key not working\n");
        }
    }

    if (ParsedRequest_unparse_headers(request, buf + len, (size_t)MAX_BYTES - len) < 0)
    {
        printf("unparse failed\n");
        // return -1;				// If this happens Still try to send request without header
    }

    int server_port = 80; // Default Remote Server Port
    if (request->port != NULL)
        server_port = atoi(request->port);

    int remoteSocketID = connectRemoteServer(request->host, server_port);

    if (remoteSocketID < 0)
        return -1;

    int bytes_send = send(remoteSocketID, buf, strlen(buf), 0);

    bzero(buf, MAX_BYTES);

    bytes_send = recv(remoteSocketID, buf, MAX_BYTES - 1, 0);
    char *temp_buffer = (char *)malloc(sizeof(char) * MAX_BYTES); // temp buffer
    int temp_buffer_size = MAX_BYTES;
    int temp_buffer_index = 0;

    while (bytes_send > 0) // recieve not sent
    {
        bytes_send = send(clientSocket, buf, bytes_send, 0);

        for (int i = 0; i < bytes_send / sizeof(char); i++)
        {
            temp_buffer[temp_buffer_index] = buf[i];
            // printf("%c",buf[i]); // Response Printing
            temp_buffer_index++;
        }
        temp_buffer_size += MAX_BYTES;
        temp_buffer = (char *)realloc(temp_buffer, temp_buffer_size);

        if (bytes_send < 0)
        {
            perror("Error in sending data to client socket.\n");
            break;
        }
        bzero(buf, MAX_BYTES);

        bytes_send = recv(remoteSocketID, buf, MAX_BYTES - 1, 0);
    }
    temp_buffer[temp_buffer_index] = '\0';
    free(buf);
    add_cache_element(temp_buffer, strlen(temp_buffer), tempReq);
    printf("Done\n");
    free(temp_buffer);

    close(remoteSocketID);
    return 0;
}

int checkHTTPversion(char *msg)
{
    int version = -1;

    if (strncmp(msg, "HTTP/1.1", 8) == 0)
    {
        version = 1;
    }
    else if (strncmp(msg, "HTTP/1.0", 8) == 0)
    {
        version = 1; // Handling this similar to version 1.1
    }
    else
        version = -1;

    return version;
}

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
        if (ParsedRequest_parse(request, buffer, len) < 0) // buffer contains the request and len is the length of the request
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
        ParsedRequest_destroy(request); // this function is used to free the memory allocated to the request already in the library
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

struct cacheElement *find(char *url)
{
    // Checks for url in the cache. If found, returns pointer to the respective cache element or else returns NULL
    struct cacheElement *site = NULL;

    // Acquire lock for thread safety
    int temp_lock_val = pthread_mutex_lock(&lock);
    printf("Find Cache Lock Acquired %d\n", temp_lock_val);

    if (head != NULL)
    {
        site = head;
        while (site != NULL)
        {
            if (!strcmp(site->url, url))
            {
                printf("LRU Time Track Before : %ld\n", site->lru_time_track);
                printf("URL found\n");

                // Updating the lru_time_track
                site->lru_time_track = time(NULL);
                printf("LRU Time Track After : %ld\n", site->lru_time_track);
                break;
            }
            site = site->next;
        }
    }
    else
    {
        printf("URL not found\n");
    }

    // Release lock
    temp_lock_val = pthread_mutex_unlock(&lock);
    printf("Find Cache Lock Unlocked %d\n", temp_lock_val);

    return site;
}

void remove_cache_element()
{
    // If cache is not empty, searches for the node which has the least lru_time_track and deletes it
    struct cacheElement *p;    // Previous pointer
    struct cacheElement *q;    // Current pointer
    struct cacheElement *temp; // Cache element to remove

    // Acquire lock for thread safety
    int temp_lock_val = pthread_mutex_lock(&lock);
    printf("Remove Cache Lock Acquired %d\n", temp_lock_val);

    if (head != NULL)
    { // Cache is not empty
        p = head;
        q = head;
        temp = head;

        // Iterate through the entire cache and search for the oldest time track
        while (q->next != NULL)
        {
            if ((q->next->lru_time_track) < (temp->lru_time_track))
            {
                temp = q->next;
                p = q;
            }
            q = q->next;
        }

        // Handle the base case if the element to be removed is the head
        if (temp == head)
        {
            head = head->next;
        }
        else
        {
            p->next = temp->next;
        }

        // Update the cache size
        cache_size -= (temp->len + sizeof(struct cacheElement) + strlen(temp->url) + 1);

        // Free the removed element
        free(temp->data);
        free(temp->url);
        free(temp);
    }

    // Release lock
    temp_lock_val = pthread_mutex_unlock(&lock);
    printf("Remove Cache Lock Unlocked %d\n", temp_lock_val);
}

int add_cache_element(char *data, int size, char *url)
{
    // Adds element to the cache
    // sem_wait(&cache_lock);
    int temp_lock_val = pthread_mutex_lock(&lock);
    printf("Add Cache Lock Acquired %d\n", temp_lock_val);
    int element_size = size + 1 + strlen(url) + sizeof(struct cacheElement); // Size of the new element which will be added to the cache
    if (element_size > MAX_ELEMENT_SIZE)
    {
        // sem_post(&cache_lock);
        //  If element size is greater than MAX_ELEMENT_SIZE we don't add the element to the cache
        temp_lock_val = pthread_mutex_unlock(&lock);
        printf("Add Cache Lock Unlocked %d\n", temp_lock_val);
        // free(data);
        // printf("--\n");
        // free(url);
        return 0;
    }
    else
    {
        while (cache_size + element_size > MAX_SIZE)
        {
            // We keep removing elements from cache until we get enough space to add the element
            remove_cache_element();
        }
        struct cacheElement *element = (struct cacheElement *)malloc(sizeof(struct cacheElement)); // Allocating memory for the new cache element
        element->data = (char *)malloc(size + 1);                                                  // Allocating memory for the response to be stored in the cache element
        strcpy(element->data, data);
        element->url = (char *)malloc(1 + (strlen(url) * sizeof(char))); // Allocating memory for the request to be stored in the cache element (as a key)
        strcpy(element->url, url);
        element->lru_time_track = time(NULL); // Updating the time_track
        element->next = head;
        element->len = size;
        head = element;
        cache_size += element_size;
        temp_lock_val = pthread_mutex_unlock(&lock);
        printf("Add Cache Lock Unlocked %d\n", temp_lock_val);
        // sem_post(&cache_lock);
        //  free(data);
        //  printf("--\n");
        //  free(url);
        return 1;
    }
    return 0;
}