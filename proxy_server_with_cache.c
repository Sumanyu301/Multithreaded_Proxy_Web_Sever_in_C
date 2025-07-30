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

#define MAX_BYTES 4096                  // max allowed size of request/response
#define MAX_CLIENTS 400                 // max number of client requests served at a time
#define MAX_SIZE 200 * (1 << 20)        // size of the cache
#define MAX_ELEMENT_SIZE 10 * (1 << 20) // max size of an element in cache

typedef struct cache_element cache_element;

struct cache_element
{
    char *data;            // data stores response
    int len;               // length of data i.e.. sizeof(data)...
    char *url;             // url stores the request
    time_t lru_time_track; // lru_time_track stores the latest time the element is  accesed
    cache_element *next;   // pointer to next element
};

cache_element *find(char *url);
int add_cache_element(char *data, int size, char *url);
void remove_cache_element();
void print_cache_contents();

int port_number = 8080;     // Default Port
int proxy_socketId;         // socket descriptor of proxy server
pthread_t tid[MAX_CLIENTS]; // array to store the thread ids of clients
sem_t seamaphore;           // if client requests exceeds the max_clients this seamaphore puts the
                            // waiting threads to sleep and wakes them when traffic on queue decreases
// sem_t cache_lock;
pthread_mutex_t lock; // lock is used for locking the cache

cache_element *head; // pointer to the cache
int cache_size;      // cache_size denotes the current size of the cache

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

int connectRemoteServer(char *host_addr, int port_num) // request->host is the host address and request->port is the port number
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
    // Input: The function takes a single argument, host_addr, which is a string containing the hostname to be resolved.
    // DNS Resolution: Internally, gethostbyname performs a DNS lookup to resolve the provided hostname into an IP address. This involves querying the DNS system to obtain the corresponding address.
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

    bcopy((char *)host->h_addr, (char *)&server_addr.sin_addr.s_addr, host->h_length);

    // Connect to Remote server ----------------------------------------------------

    if (connect(remoteSocket, (struct sockaddr *)&server_addr, (socklen_t)sizeof(server_addr)) < 0)
    {
        fprintf(stderr, "Error in connecting !\n");
        return -1;
    }
    // free(host_addr);
    return remoteSocket;
}

// The handle_request function processes an incoming HTTP request from a client, constructs a corresponding request to a remote server, sends it, and forwards the response back to the client. Key operations include managing headers, establishing a connection to the remote server, handling data transmission, and caching the response for future requests. This function is crucial for the operation of a proxy server, which acts as an intermediary between clients and remote servers.

int handle_request(int clientSocket, struct ParsedRequest *request, char *tempReq) // tempreq is buffer and request is the parsed request socket is the client socket
{
    char *buf = (char *)malloc(sizeof(char) * MAX_BYTES);
    strcpy(buf, "GET ");
    strcat(buf, request->path);
    strcat(buf, " ");
    strcat(buf, request->version);
    strcat(buf, "\r\n");
    // The HTTP method (GET), request path, and version are concatenated to form the complete HTTP request line.
    size_t len = strlen(buf);

    if (ParsedHeader_set(request, "Connection", "close") < 0) // The function sets the Connection header to close, indicating that the server should close the connection after the response is sent.
    {
        printf("set header key not work\n");
    }

    // The function checks if the Host header is present in the parsed request.
    // If not, it sets the Host header to the value specified in the request. This is necessary for HTTP/1.1 requests.
    if (ParsedHeader_get(request, "Host") == NULL)
    {
        if (ParsedHeader_set(request, "Host", request->host) < 0)
        {
            printf("Set \"Host\" header key not working\n");
        }
    }

    if (ParsedRequest_unparse_headers(request, buf + len, (size_t)MAX_BYTES - len) < 0) // The function attempts to unparse any additional headers from the ParsedRequest structure and append them to the buffer.
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

    while (bytes_send > 0)
    {
        bytes_send = send(clientSocket, buf, bytes_send, 0);

        for (int i = 0; i < bytes_send; i++)
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
    print_cache_contents(); // Show what's in cache after adding
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

void *thread_fn(void *socketNew) // its the new socket creted by accept for every client connected.
{
    sem_wait(&seamaphore);
    int p;
    int *t = (int *)(socketNew); // descriptors are integers so typecasting it back to int
    int socket = *t;             // Socket is socket descriptor of the connected Client
    int bytes_send_client, len;  // Bytes Transferred

    char *buffer = (char *)calloc(MAX_BYTES, sizeof(char)); // Creating buffer of 4kb for a client

    bzero(buffer, MAX_BYTES);                               // Making buffer zero
    bytes_send_client = recv(socket, buffer, MAX_BYTES, 0); // recv(socket, buffer, MAX_BYTES, 0): Receives data from the client socket and stores it in buffer. The function reads up to MAX_BYTES bytes.
    // """"This variable is intended to store the number of bytes received from the socket""". The recv function returns the number of bytes actually read into the buffer. If the return value is -1, it indicates an error occurred during the reception of data
    // This is a pointer to a memory area (an array) where the received data will be stored. The buffer should be large enough to hold the incoming data. The data received will be copied into this buffer starting from the first byte.
    // if the data being received is larger than the specified MAX_BYTES in the recv() function, the data will not automatically be received in multiple calls. Instead, the recv() function will only read up to MAX_BYTES bytes in a single call.
    // 0: No special options; the function behaves normally.

    // Here’s a simplified example of an HTTP GET request
    // GET /index.html HTTP/1.1\r\n
    //  Host: www.example.com\r\n
    //  User-Agent: Mozilla/5.0\r\n
    //  Accept: text/html\r\n
    //  \r\n

    while (bytes_send_client > 0) // data chunks mein aayega isliye loop
    {
        len = strlen(buffer); // This line calculates the current length of the data stored in buffer. This is important because it determines where to append new data received from the socket.
        // loop until u find "\r\n\r\n" in the buffer
        if (strstr(buffer, "\r\n\r\n") == NULL) // If "\r\n\r\n" is found in the buffer, the loop breaks, indicating that the complete HTTP headers have been received.
        {
            bytes_send_client = recv(socket, buffer + len, MAX_BYTES - len, 0);
        }
        else
        {
            break;
        }
    }

    // printf("--------------------------------------------\n");
    // printf("%s\n", buffer);
    // printf("----------------------%d----------------------\n", strlen(buffer));

    char *tempReq = (char *)malloc(strlen(buffer) * sizeof(char) + 1);
    // tempReq, buffer both store the http request sent by client
    size_t buffer_len = strlen(buffer);
    for (size_t i = 0; i < buffer_len; i++)
    {
        tempReq[i] = buffer[i];
    }
    tempReq[buffer_len] = '\0'; // Null terminate the string

    // Parse the request to extract essential parts for cache key
    struct ParsedRequest *parsed_request = ParsedRequest_create();
    if (ParsedRequest_parse(parsed_request, buffer, strlen(buffer)) >= 0)
    {
        // Create normalized cache key using only essential parts
        char cache_key[MAX_BYTES];
        snprintf(cache_key, sizeof(cache_key), "GET %s HTTP/1.1\r\nHost: %s\r\n",
                 parsed_request->path ? parsed_request->path : "/",
                 parsed_request->host ? parsed_request->host : "localhost");

        printf("Using normalized cache key: %.200s...\n", cache_key);

        // Call the find function with normalized cache key
        struct cache_element *temp = find(cache_key);

        if (temp != NULL)
        {
            // request found in cache, so sending the response to client from proxy's cache
            int size = temp->len;
            int pos = 0;
            char response[MAX_BYTES];
            while (pos < size)
            {
                bzero(response, MAX_BYTES);
                int bytes_to_send = (size - pos < MAX_BYTES) ? size - pos : MAX_BYTES;
                for (int i = 0; i < bytes_to_send; i++)
                {
                    response[i] = temp->data[pos];
                    pos++;
                }
                send(socket, response, bytes_to_send, 0);
            }
            printf("Data retrieved from the Cache using normalized key\n\n");
            printf("%s\n\n", response);
            print_cache_contents(); // Show what's currently in cache
        }
        else if (bytes_send_client > 0) // Cache miss - process normally
        {
            len = strlen(buffer); // buffer has the request from the client

            if (!strcmp(parsed_request->method, "GET"))
            {
                if (parsed_request->host && parsed_request->path && (checkHTTPversion(parsed_request->version) == 1))
                {
                    bytes_send_client = handle_request(socket, parsed_request, cache_key); // Use normalized key for caching
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

        ParsedRequest_destroy(parsed_request);
    }
    else
    {
        printf("Failed to parse request for caching\n");
        // Fallback to old method if parsing fails
        struct cache_element *temp = find(tempReq);

        if (temp != NULL)
        {
            // request found in cache, so sending the response to client from proxy's cache
            int size = temp->len;
            int pos = 0;
            char response[MAX_BYTES];
            while (pos < size)
            {
                bzero(response, MAX_BYTES);
                int bytes_to_send = (size - pos < MAX_BYTES) ? size - pos : MAX_BYTES;
                for (int i = 0; i < bytes_to_send; i++)
                {
                    response[i] = temp->data[pos];
                    pos++;
                }
                send(socket, response, bytes_to_send, 0);
            }
            printf("Data retrived from the Cache\n\n");
            printf("%s\n\n", response);
            print_cache_contents(); // Show what's currently in cache
        }
        else if (bytes_send_client > 0) // we recv the request from the client into this and it stores the number of bytes received
        {
            len = strlen(buffer); // buffer has the request from the client
            // Parsing the request
            struct ParsedRequest *request = ParsedRequest_create(); // request is a struct that stores the parsed request its defined in proxy_parse.h

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
    }

    if (bytes_send_client < 0)
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
    sem_post(&seamaphore);

    sem_getvalue(&seamaphore, &p);
    printf("Semaphore post value:%d\n", p);
    free(tempReq);
    return NULL;
}

int main(int argc, char *argv[]) // argc has the number of arguments passed to the program and argv is a pointer to the array of character pointers like ./proxy 8080 8080 is the second argv[1]
{

    int client_socketId, client_len;             // client_socketId == to store the client socket id
    struct sockaddr_in server_addr, client_addr; // Address of client and server to be assigned

    //     struct sockaddr_in {
    //     sa_family_t sin_family;  Address family, AF_INET for IPv4
    //     in_port_t sin_port;      Port number in network byte order
    //     struct in_addr sin_addr;  Internet address
    // };

    sem_init(&seamaphore, 0, MAX_CLIENTS); // Initializing seamaphore and lock
    pthread_mutex_init(&lock, NULL);       // Initializing lock for cache

    if (argc == 2) // checking whether two arguments are received or not
    {
        port_number = atoi(argv[1]);
    }
    else
    {
        printf("Too few arguments\n");
        exit(1);
    }

    printf("Setting Proxy Server Port : %d\n", port_number);

    // creating the proxy socket
    proxy_socketId = socket(AF_INET, SOCK_STREAM, 0); // int domain int type int protocol
    // AF_INET is used to represent the IPv4 address of the client to which a connection should be made.
    // Similarly AF_INET6 is used for IPv6 addresses. These sockets are called internet domain sockets.
    // In this case, setting the protocol to 0 means that the default protocol (which is TCP for SOCK_STREAM) will be used.
    if (proxy_socketId < 0) // as in case of error it returns -1
    {
        perror("Failed to create socket.\n");
        exit(1);
    }

    int reuse = 1; // this is to enable the reuse of the address

    // This option allows the socket to bind to an address that is already in use,
    // which is particularly useful for server applications that need to restart
    // and bind to the same port without waiting for the previous socket to fully close.
    if (setsockopt(proxy_socketId, SOL_SOCKET, SO_REUSEADDR, (const char *)&reuse, sizeof(reuse)) < 0) // Casting to const char * is a way to ensure that the pointer is treated as a generic byte pointer, which is suitable for passing data of any type.
        perror("setsockopt(SO_REUSEADDR) failed\n");                                                   // You're right, reuse is an integer variable, so casting it to (const char *) may seem odd at first glance. However, this is a common practice in C programming when working with the setsockopt() function.
    //  The setsockopt() function expects a const void * pointer as the value parameter, which can point to any data type. By casting &reuse to (const char *), you're telling the compiler to treat the memory address of reuse as a pointer to a sequence of bytes (characters).
    // sizeof makes sure that
    bzero((char *)&server_addr, sizeof(server_addr));
    server_addr.sin_family = AF_INET;          // This field specifies the address family, which is typically set to AF_INET for IPv4 addresses.
    server_addr.sin_port = htons(port_number); // Assigning port to the Proxy
    server_addr.sin_addr.s_addr = INADDR_ANY;  // Any available adress assigned

    // Binding the socket
    if (bind(proxy_socketId, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        perror("Port is not free\n");
        exit(1);
    }
    printf("Binding on port: %d\n", port_number);

    // Proxy socket listening to the requests
    int listen_status = listen(proxy_socketId, MAX_CLIENTS); // marks the maximum number of connection requests that can be made to the server by client nodes at a time

    if (listen_status < 0)
    {
        perror("Error while Listening !\n");
        exit(1);
    }

    int i = 0;                           // Iterator for thread_id (tid) and Accepted Client_Socket for each thread
    int Connected_socketId[MAX_CLIENTS]; // This array stores socket descriptors of connected clients

    // Infinite Loop for accepting connections
    while (1)
    {

        bzero((char *)&client_addr, sizeof(client_addr)); // Clears struct client_addr
        client_len = sizeof(client_addr);

        // Accepting the connections
        // The accept() function is a blocking call. This means that when the server reaches this line of code, it will wait (block) until a client attempts to connect to the server.
        // If there are no pending connection requests in the listen queue, the server will remain blocked at this point until a new client connects.
        client_socketId = accept(proxy_socketId, (struct sockaddr *)&client_addr, (socklen_t *)&client_len); // Accepts connection
        if (client_socketId < 0)
        {
            fprintf(stderr, "Error in Accepting connection !\n");
            exit(1);
        }
        else
        {
            Connected_socketId[i] = client_socketId; // Storing accepted client into array
        }

        // Getting IP address and port number of client
        struct sockaddr_in *client_pt = (struct sockaddr_in *)&client_addr; // Since the server is dealing with IPv4 addresses, it casts client_addr to struct sockaddr_in *, which contains fields specific to IPv4 addresses.
        struct in_addr ip_addr = client_pt->sin_addr;                       // this line retrieves the sin_addr field from the client_pt, which contains the client's IP address in binary format (a 32-bit integer).
        char str[INET_ADDRSTRLEN];                                          // INET_ADDRSTRLEN: Default ip address size INET_ADDRSTRLEN is typically defined as 16
        inet_ntop(AF_INET, &ip_addr, str, INET_ADDRSTRLEN);                 // The inet_ntop() function converts the binary representation of the IP address (ip_addr) into a human-readable string format and stores it in str.
        // The first argument AF_INET specifies that the address is an IPv4 address.
        printf("Client is connected with port number: %d and ip address: %s \n", ntohs(client_addr.sin_port), str);
        // This line prints the port number and IP address of the connected client.
        // The port number is retrieved from client_addr.sin_port, which is in network byte order. The ntohs() function converts it to host byte order for correct display.

        pthread_create(&tid[i], NULL, thread_fn, (void *)&Connected_socketId[i]); // Creating a thread for each client accepted
        // After obtaining the client's information, the server typically creates a new thread (or uses a process) to handle the client's requests, allowing it to continue accepting new connections.
        //&tid[i]: This is a pointer to the thread identifier (TID) for the newly created thread. The pthread_create function will store the unique identifier for the newly created thread in this location. The TID is used to manage and identify the thread later.
        // NULL: This argument specifies thread attributes. Passing NULL means that the default thread attributes are used.
        // thread_fn: This is the function that the new thread will execute. It is a pointer to the function that will handle the client request. In this case, it's thread_fn, which processes the client request and performs necessary actions.
        //(void *)&Connected_socketId[i]: This is the argument passed to the thread_fn function. In this case, it's a pointer to the socket descriptor of the connected client. It is cast to void * because pthread_create requires the argument to be of type void *. Inside thread_fn, this argument is cast back to the appropriate type (i.e., int *) to access the socket descriptor.

        i++;
    }
    close(proxy_socketId); // Close socket After the loop (which is actually infinite and only ends if the program is stopped)
    return 0;
}

cache_element *find(char *url) // we are sending the whole request as url
{

    // Checks for url in the cache if found returns pointer to the respective cache element or else returns NULL
    printf("Searching for URL: %s\n", url);
    cache_element *site = NULL;
    // sem_wait(&cache_lock);
    int temp_lock_val = pthread_mutex_lock(&lock);
    printf("Remove Cache Lock Acquired %d\n", temp_lock_val);
    if (head != NULL)
    {
        site = head;
        while (site != NULL)
        {
            if (!strcmp(site->url, url)) // strcmp return 0 if both strings are equal
            {
                printf("URL->site: %s\n", site->url);
                printf("LRU Time Track Before : %ld", site->lru_time_track);
                printf("\nurl found\n");
                // Updating the time_track
                site->lru_time_track = time(NULL);
                printf("LRU Time Track After : %ld", site->lru_time_track);
                break;
            }
            site = site->next;
        }

        // Check if URL was not found after searching through the cache
        if (site == NULL)
        {
            printf("\nurl not found in cache\n");
        }
    }
    else
    {
        printf("\nurl not found - cache is empty\n");
    }
    // sem_post(&cache_lock);
    temp_lock_val = pthread_mutex_unlock(&lock);
    printf("Remove Cache Lock Unlocked %d\n", temp_lock_val);
    return site;
}

void remove_cache_element()
{
    // If cache is not empty searches for the node which has the least lru_time_track and deletes it
    cache_element *p;    // Cache_element Pointer (Prev. Pointer)
    cache_element *q;    // Cache_element Pointer (Next Pointer)
    cache_element *temp; // Cache element to remove
    // sem_wait(&cache_lock);
    int temp_lock_val = pthread_mutex_lock(&lock);
    printf("Remove Cache Lock Acquired %d\n", temp_lock_val);
    if (head != NULL)
    { // Cache != empty
        for (q = head, p = head, temp = head; q->next != NULL;
             q = q->next)
        { // Iterate through entire cache and search for oldest time track
            if (((q->next)->lru_time_track) < (temp->lru_time_track))
            {
                temp = q->next;
                p = q;
            }
        }
        if (temp == head)
        {
            head = head->next; /*Handle the base case*/
        }
        else
        {
            p->next = temp->next;
        }
        cache_size = cache_size - (temp->len) - sizeof(cache_element) -
                     strlen(temp->url) - 1; // updating the cache size
        free(temp->data);
        free(temp->url); // Free the removed element
        free(temp);
    }
    // sem_post(&cache_lock);
    temp_lock_val = pthread_mutex_unlock(&lock);
    printf("Remove Cache Lock Unlocked %d\n", temp_lock_val);
}

int add_cache_element(char *data, int size, char *url)
{
    // Adds element to the cache
    // sem_wait(&cache_lock);
    int temp_lock_val = pthread_mutex_lock(&lock);
    printf("Add Cache Lock Acquired %d\n", temp_lock_val);
    int element_size = size + 1 + strlen(url) + sizeof(cache_element); // Size of the new element which will be added to the cache
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
        cache_element *element = (cache_element *)malloc(sizeof(cache_element)); // Allocating memory for the new cache element
        element->data = (char *)malloc(size + 1);                                // Allocating memory for the response to be stored in the cache element
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

void print_cache_contents()
{
    // Prints all elements currently in the cache
    printf("\n========== CACHE CONTENTS ==========\n");
    int temp_lock_val = pthread_mutex_lock(&lock);
    printf("Cache Lock Acquired for printing %d\n", temp_lock_val);

    if (head == NULL)
    {
        printf("Cache is empty\n");
    }
    else
    {
        cache_element *current = head;
        int count = 1;
        printf("Current cache size: %d bytes\n", cache_size);
        printf("Cache elements:\n");

        while (current != NULL)
        {
            printf("\n--- Element %d ---\n", count);
            printf("URL (first 100 chars): %.100s%s\n",
                   current->url,
                   strlen(current->url) > 100 ? "..." : "");
            printf("Data size: %d bytes\n", current->len);
            printf("LRU timestamp: %ld\n", current->lru_time_track);
            printf("Data preview (first 200 chars): %.200s%s\n",
                   current->data,
                   strlen(current->data) > 200 ? "..." : "");

            current = current->next;
            count++;
        }
        printf("\nTotal elements in cache: %d\n", count - 1);
    }

    temp_lock_val = pthread_mutex_unlock(&lock);
    printf("Cache Lock Unlocked %d\n", temp_lock_val);
    printf("====================================\n\n");
}
