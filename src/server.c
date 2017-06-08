#include "communication.h"


struct epoll_event events[MAX_EVENTS];
int socket_fd;
int epoll_fd;
int actual_clients;
client_t clients[MAX_CLIENTS];
//pthread_mutex_t mutex; maybe in future some threads will be used

int start_server() {
    if((socket_fd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        return -1;
    }

    uint16_t inet_port = 36000;
    struct sockaddr_in addr_in;
    memset((char*)&addr_in, 0, sizeof(addr_in));

    char * addr = "10.205.11.113";

    in_addr_t bin_addr = inet_addr(addr);
    addr_in.sin_addr.s_addr = bin_addr;
    addr_in.sin_family = AF_INET;
    addr_in.sin_port = htons(inet_port);
    if(bind(socket_fd, (const struct sockaddr *) &addr_in, sizeof(addr_in)) == -1) {
        perror("bind");
        return -1;
    }

//    int enable = 1;
//    if (setsockopt(socket_fd, SOL_SOCKET, SO_BROADCAST, (const char*)&enable, sizeof(enable)) < 0)
//        perror("setsockopt(SO_REUSEADDR) failed");

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
    //pthread_mutex_lock(&mutex);
    clients[actual_clients++].fd = client_fd;
    //pthread_mutex_unlock(&mutex);

    return 0;
}

void close_client(int fd) {
    //pthread_mutex_lock(&mutex);
    for (int i=0, j=0;i<actual_clients;++i,++j) {
        if (clients[i].fd == fd) {
            if(close(fd) == -1) {
                perror("close_client: close");
            }
            --j;
        } else {
            clients[j] = clients[i];
        }
    }
    --actual_clients;
    printf("%d disconnected\n> ",fd);
    fflush(stdout);
    //pthread_mutex_unlock(&mutex);
}

int broadcast_message(msg_t msg) {
    int res = 0;
    for(int i = 0; i<actual_clients;++i) {
        if(write(clients[i].fd,&msg,sizeof(msg)) == -1) {
            perror("broadcast_message: write");
            ++res;
        }
    }
    return res;
}

int read_message(struct epoll_event event) {
    msg_t msg;
    ssize_t bytes_read = read(event.data.fd,&msg,sizeof(msg));
    if(bytes_read == -1) {
        if(errno !=EAGAIN && errno != EWOULDBLOCK) {
            perror("Error receiving message from client");
        }
        return -1;
    } else if(bytes_read == 0) { // End of file - client closed connection
        close_client(event.data.fd);
        return -1;
    }
    else {
        //msg_t response;
        //strcpy(response.name,msg.name);
        //response.timestamp = msg.timestamp;
        int broad_res;
        switch(msg.type) {
            case MESSAGE:
                printf("ID(%d) - Message: %s. Sent from %s\n> ",msg.timestamp, msg.message, msg.name);
                if((broad_res = broadcast_message(msg)) != 0) {
                    fprintf(stderr,"failed to send response to %d clients",broad_res);
                } else {
                    printf("Broadcasted\n> ");
                }
                break;
            case LOGIN:
                //pthread_mutex_lock(&mutex);

                for(int i =0; i<actual_clients-1; ++i) {
                    if(strcmp(clients[i].name,msg.name)==0) {
                        printf("User with the same username: %s, tried to login\n> ",msg.name);
                        //pthread_mutex_unlock(&mutex);
                        msg.type = FAILURE;
                        if(write(event.data.fd,&msg,sizeof(msg)) == -1) {
                            perror("read_message: write");
                        }
                        close_client(event.data.fd);
                        return -1;
                    }
                }
                strcpy(clients[actual_clients-1].name,msg.name);
                //pthread_mutex_unlock(&mutex);
                msg.type = SUCCESS;
                if(write(event.data.fd,&msg,sizeof(msg)) == -1) {
                    perror("read_message: write");
                }
                printf("%d connected. Username: %s\n> ",event.data.fd,msg.name);
                break;
            default:
                break;
        }
        fflush(stdout);
    }
    return 0;
}

int main() {
    if(start_server() == -1) return 1;

    if(start_listening() == -1) return 1;

    int s = 1;
    while(s) {
        int n = epoll_wait(epoll_fd,events,MAX_EVENTS,-1);
        if(n == -1) {
            perror("epoll_wait");
        }

        for(int i = 0;i<n;++i) { // handle every event
            struct epoll_event event = events[i];

            if(is_event_invalid(event)) continue;

            if(event.data.fd == socket_fd) {// if the data came from inet or unix socket, we've got new connection
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