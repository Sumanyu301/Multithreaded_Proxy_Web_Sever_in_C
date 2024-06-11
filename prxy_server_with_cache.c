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
#define MAX_CLIENTS 1000
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