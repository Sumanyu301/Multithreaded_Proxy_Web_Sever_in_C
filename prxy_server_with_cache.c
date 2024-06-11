#include<stdio.h>
#include<string.h>
#include "proxy_parse.h"
#include<time.h>

struct cacheElement
{
    char *data;
    int len;
    char *url;
    time_t lru_time_track;
    struct cacheElement *next;
};

struct cacheElement* find(char *url);
int add_cacheElement(char *data, int size,char *url);
void remove_cacheElement();

int port_number=8080;