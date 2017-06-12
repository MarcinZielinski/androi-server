//
// Created by Mrz355 on 29.05.17.
//

#ifndef CW10_COMMUNICATION_H
#define CW10_COMMUNICATION_H

#define MAX_EVENTS 16
#define MAX_NAME_LEN 108
#define MAX_CLIENTS 32
#define MAX_MESSAGE_LEN 512
#define MAX_TIME_LEN 11

#include <stdio.h>
#include <stdlib.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/un.h>
#include <unistd.h>
#include <pthread.h>
#include <signal.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>

typedef enum msg_type {
    LOGIN = 0, MESSAGE = 1, SUCCESS = 2, FAILURE = 3, PING = 4, PONG = 5
} msg_type_t;

typedef struct client {
    int fd;
    int pings;
    int pongs;
    char name[MAX_NAME_LEN];
} client_t;

typedef struct msg {
    char name[MAX_NAME_LEN];
    char message[MAX_MESSAGE_LEN];
    char timestamp[MAX_TIME_LEN];
} msg_t;

typedef struct {
    msg_type_t type;
    char name[MAX_NAME_LEN];
    char message[MAX_MESSAGE_LEN];
    char timestamp[MAX_TIME_LEN];
} msg_with_type_t;



#endif //CW10_COMMUNICATION_H
