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

struct cacheElement* head;
int cache_size;
int main(int argc, char* argv[])
{
    int client_socketId, client_len;
    struct sockaddr_in server_addr, client_addr;
    struct ParsedRequest *request;
    sem_init(&semaphore,0,MAX_CLIENTS);//0 means semaphore is shared between threads and 1 means semaphore is shared between processes
    pthread_mutex_init(&lock,NULL);//null else garbage
    if(argv==2)
    {
        port_number = atoi(argv[1]);
        // when we write in console ./proxy 8080 then  ./proxy 0 and port number 8080 1 as it takes it as a string 
    }
    else
    {
        printf("too few arguments\n");
        exit(1);//exit with error agar exit(0) likhenge that means error nahi chal ke exit hogya
    }

    printf("Proxy server started at port %d\n",port_number);
    proxy_socketId = socket(AF_INET, SOCK_STREAM, 0); //this will be the main socket as whatever request comes it will come here and it will spawn a new thread for that request thus creating a new socket for that request
    //and every time the same request comes it will be handled by the same thread thus the same socket will be used
    //it will create new socket for every new request
    //AF_INET is the address family for IPv4 and SOCK_STREAM is the type of socket we want to create and it ensures that whatever happens happens on tcp and 0 is the protocol
    if(proxy_socketId<0)//if the socket is not created it returns negative
    {
        perror("Error in creating socket\n");
        exit(1);
    }

    int reuse = 1;// to use the main socket again and again
    if(setsockopt(proxy_socketId, SOL_SOCKET, SO_REUSEADDR, (const char*)&reuse, sizeof(reuse))<0)//setsockopt is used to set the options for the socket and sol_socket is the level of the socket and so_reuseaddr is the option we want to set and reuse is the value we want to set it to
    {
        perror("Error in setting socket options\n");
        exit(1);
    }
    bzero((char*)&server_addr, sizeof(server_addr));//bzero is used to set all the values of the server_addr to 0 else it will have garbage values and size is to know when to stop
    server_addr.sin_family = AF_INET;//setting the address family to ipv4
    server_addr.sin_port = htons(port_number);//htons is used to convert the port number to network byte order basically saying that the port number is in the right format
    server_addr.sin_addr.s_addr = INADDR_ANY;//give any address to the server

    if(bind(proxy_socketId, (struct sockaddr*)&server_addr, sizeof(server_addr))<0)//Binding a socket is the process of assigning a port number to a socket. This is necessary for the socket to be able to receive data. When a socket is created, it does not have a port number assigned to it. Binding a socket to a port number allows it to receive data from other sockets on the same network
    {
        perror("Error in binding\n");
        exit(1);
    }
}