#include "communication.h"


struct epoll_event events[MAX_EVENTS];
int socket_fd = -1;
int epoll_fd = -1;
int actual_clients;
client_t clients[MAX_CLIENTS];
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_attr_t attr;


void exit_handler() {
    pthread_attr_destroy(&attr);
    if(socket_fd != -1) {
        shutdown(socket_fd,SHUT_RDWR);
        close(socket_fd);
    }
    if(epoll_fd != -1) {
        close(epoll_fd);
    }
}

int start_server() {
    if((socket_fd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        return -1;
    }

    uint16_t inet_port = 36000;
    struct sockaddr_in addr_in;
    memset((char*)&addr_in, 0, sizeof(addr_in));

    in_addr_t bin_addr = INADDR_ANY;
    addr_in.sin_addr.s_addr = bin_addr;
    addr_in.sin_family = AF_INET;
    addr_in.sin_port = htons(inet_port);
    if(bind(socket_fd, (const struct sockaddr *) &addr_in, sizeof(addr_in)) == -1) {
        perror("bind");
        return -1;
    }

    int enable = 1;
    if (setsockopt(socket_fd, SOL_SOCKET, SO_BROADCAST, (const char*)&enable, sizeof(enable)) < 0)
        perror("setsockopt(SO_REUSEADDR) failed");

    printf("IP address: %s\n",inet_ntoa(addr_in.sin_addr));

    return 0;
}

int make_socket_non_blocking(int socket) {
        int flags;
        if ((flags = fcntl(socket, F_GETFL, 0)) == -1) {
            perror ("fcntl");
            return -1;
        }

        flags |= O_NONBLOCK;

        if (fcntl(socket, F_SETFL, flags) == -1) {
            perror ("fcntl");
            return -1;
        }

        return 0;
}

int start_listening() {
    if(listen(socket_fd,SOMAXCONN) == -1) {
        perror("listen");
        return -1;
    }

    if(make_socket_non_blocking(socket_fd) == -1) {
        perror("make_socket_non_blocking");
        return -1;
    }

    if((epoll_fd = epoll_create1(0)) == -1) {
        perror("epoll_create1");
        return -1;
    }

    struct epoll_event ee;
    ee.events = EPOLLIN | EPOLLRDHUP | EPOLLET;
    ee.data.fd = socket_fd;
    if(epoll_ctl(epoll_fd,EPOLL_CTL_ADD,socket_fd,&ee) == -1) {
        perror("epoll_ctl");
        return -1;
    }

    return 0;
}

int is_event_invalid(struct epoll_event event) {
    int fd = event.data.fd;
    if(event.events & EPOLLERR) {
        fprintf(stderr,"Event error\n");
        return -1;
    }
    if(event.events & EPOLLRDHUP || event.events & EPOLLHUP) { // client disconnected
        if(fd!=socket_fd)
            printf("closse_client");
        return -1;
    }
    return 0;
}

int add_client(struct epoll_event event) {
    struct sockaddr new_addr;
    socklen_t new_addr_len = sizeof(new_addr);
    int client_fd = accept(event.data.fd, &new_addr, &new_addr_len);

    if(client_fd == -1) {
        if(errno !=EAGAIN && errno != EWOULDBLOCK) {
            perror("accept");
        }
        return -1;
    }

    if(make_socket_non_blocking(client_fd) == -1) {
        return -1;
    }

    struct epoll_event client_event;
    client_event.data.fd = client_fd;
    client_event.events = EPOLLIN | EPOLLET;
    if(epoll_ctl(epoll_fd,EPOLL_CTL_ADD,client_fd,&client_event) == -1) {
        perror("Error while adding new socket to epoll");
        return -1;
    }
    pthread_mutex_lock(&mutex);
    clients[actual_clients].pings = 0;
    clients[actual_clients].pongs = 0;
    clients[actual_clients++].fd = client_fd;
    pthread_mutex_unlock(&mutex);

    return 0;
}

int broadcast_message(msg_t msg) {
    int res = 0;
    msg_with_type_t respond;
    respond.type = MESSAGE;
    sprintf(respond.timestamp,"%zu",time(NULL));
    strcpy(respond.name,msg.name);
    strcpy(respond.message,msg.message);
    for(int i = 0; i<actual_clients;++i) {
        if(write(clients[i].fd,&respond,sizeof(respond)) == -1) {
            perror("broadcast_message: write");
            ++res;
        }
    }
    return res;
}

void close_client(int fd, int broadcast) {
    pthread_mutex_lock(&mutex);
    client_t client;
    for (int i=0, j=0;i<actual_clients;++i,++j) {
        if (clients[i].fd == fd) {
            if(close(fd) == -1) {
                perror("close_client: close");
            }
            client = clients[i];
            --j;
        } else {
            clients[j] = clients[i];
        }
    }
    --actual_clients;
    if(broadcast) {
        msg_t msg;
        strcpy(msg.name, client.name);
        sprintf(msg.message, " disconnected");
        broadcast_message(msg);
    }
    printf("%d disconnected\n> ",fd);
    fflush(stdout);
    pthread_mutex_unlock(&mutex);
}

int validate_message(struct epoll_event event, int bytes_read) {
    if(bytes_read == -1) {
        if(errno !=EAGAIN && errno != EWOULDBLOCK) {
            perror("Error receiving message from client");
        }
        return -1;
    } else if(bytes_read == 0) { // End of file - client closed connection
        close_client(event.data.fd,1);
        return -1;
    }
    return 0;
}

int read_message(struct epoll_event event) {
    msg_type_t type;
    msg_t msg;
    char *tmp;
    ssize_t bytes_read = read(event.data.fd,&type,sizeof(type));
    if(validate_message(event, (int) bytes_read) != 0) {
        return -1;
    }
    int broad_res;
    switch(type) {
        case MESSAGE:
            if(validate_message(event, (int) read(event.data.fd, &msg, sizeof(msg))) != 0) {
                return -1;
            }
            tmp = malloc(sizeof(char)*(strlen(msg.message)+1));
            strcpy(tmp, msg.message);
            sprintf(msg.message,": %s",tmp);
            free(tmp);

            printf("ID(%s) - Message: %s. Sent from %s\n> ",msg.timestamp, msg.message, msg.name);
            if((broad_res = broadcast_message(msg)) != 0) {
                fprintf(stderr,"failed to send response to %d clients",broad_res);
            } else {
                printf("Broadcasted\n> ");
            }
            break;
        case LOGIN:
            pthread_mutex_lock(&mutex);
            if(validate_message(event, (int) read(event.data.fd, &msg, sizeof(msg))) != 0) {
                return -1;
            }
            for(int i =0; i<actual_clients-1; ++i) {
                if(strcmp(clients[i].name,msg.name)==0) {
                    printf("User with the same username: %s, tried to login\n> ",msg.name);
                    pthread_mutex_unlock(&mutex);
                    type = FAILURE;
                    if(write(event.data.fd,&type,sizeof(type)) == -1) {
                        perror("read_message: write");
                    }
                    close_client(event.data.fd,0);
                    return -1;
                }
            }
            strcpy(clients[actual_clients-1].name,msg.name);
            pthread_mutex_unlock(&mutex);
            type = SUCCESS;
            if(write(event.data.fd,&type,sizeof(type)) == -1) {
                perror("read_message: write");
            }
            sprintf(msg.message," connected");
            broadcast_message(msg);
            printf("%d connected. Username: %s\n> ",event.data.fd,msg.name);
            break;
        case PONG:
            pthread_mutex_lock(&mutex);
            for(int i=0; i<actual_clients; ++i){
                if(clients[i].fd == event.data.fd) {
                    clients[i].pongs++;
                }
            }
            pthread_mutex_unlock(&mutex);
        default:
            break;
    }
    fflush(stdout);

    return 0;
}

void *pinger_handler(void *args) {
    while(1){
        // Send pings
        pthread_mutex_lock(&mutex);
        for(int i=0; i<actual_clients; i++) {
            msg_type_t type = PING;
            if (write(clients[i].fd, &type, sizeof(type)) == -1) {
                perror("pinger: write");
            }
            ++(clients[i].pings);
        }
        // Wait for responses
        pthread_mutex_unlock(&mutex);
        sleep(5);
        pthread_mutex_lock(&mutex);
        // Check responses
        for(int i = 0 ;i<actual_clients;++i) {
            if (clients[i].pings != clients[i].pongs) {
                printf("%d has not responded to PING. Disconnecting...\n> ", clients[i].fd);
                fflush(stdout);
                for (int k=0, j=0;k<actual_clients;++k,++j) {
                    if (clients[k].fd == clients[i].fd) {
                        if(close(clients[i].fd) == -1) {
                            perror("close");
                        }
                        --j;
                    } else {
                        clients[j].fd = clients[k].fd;
                    }
                }
                --actual_clients;
                printf("%d disconnected\n> ",clients[i].fd);
                fflush(stdout);
            }
        }
        pthread_mutex_unlock(&mutex);
    }
    return NULL;
}

void sigint_handler(int signum) {
    exit(0);
}

int main() {
    atexit(exit_handler);

    struct sigaction sa;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sa.sa_handler = sigint_handler;
    sigaction(SIGINT,&sa, NULL);

    if(start_server() == -1) return 1;

    if(start_listening() == -1) return 1;

    pthread_t tid;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr,PTHREAD_CREATE_DETACHED);
    pthread_create(&tid,&attr,pinger_handler,NULL);

    while(1) {
        int n = epoll_wait(epoll_fd,events,MAX_EVENTS,-1);
        if(n == -1) {
            perror("epoll_wait");
        }

        for(int i = 0;i<n;++i) { // handle every event
            struct epoll_event event = events[i];

            if(is_event_invalid(event)) continue;

            if(event.data.fd == socket_fd) {// if the data came from inet socket, we've got new connection
                while(1) {
                    if(actual_clients == MAX_CLIENTS) {
                        fprintf(stderr,"Maximum number of clients reached");
                        break;
                    }
                    if(add_client(event) == -1) break;
                }
            } else { // if the data came from some other socket it must've been some connected user's socket
                while(1) {
                    if(read_message(event) == -1) break;
                }
            }
        }
    }
}