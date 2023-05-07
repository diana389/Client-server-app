#include <algorithm>
#include <arpa/inet.h>
#include <cstring>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sstream>
#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <unistd.h>
#include <iostream>
#define BUFLEN 1600
#define PAYLOADLEN 65

using namespace std;

typedef struct request {
    int type;
    char payload[65];
} request;

typedef struct message {
    char topic[50];
    int type;
    char payload[1500];
    char ip_address[16];
    char port[6];
} message;

request *build_request(int type, char *str, int size) {
    request *r = (request*)malloc(sizeof(request));

    r->type = type;
    if(type == 0 || type == 1) {
        strcpy(r->payload, str);
    }
    else if(type == 2 || type == 3){
        memcpy(r->payload, str, size);
    }

    return r;
}


int main(int argc, char **argv) {
    setvbuf(stdout, NULL, _IONBF, BUFSIZ);

    if (argc != 4) {
        fprintf(stderr, "Usage: %s <ID_CLIENT> <IP_SERVER> <PORT_SERVER>\n", argv[0]);
        fprintf(stderr, "Eg: %s C1 1.2.3.4 1948\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    int sockfd, n, ret;
    char buffer[BUFLEN];
    fd_set read_fds, tmp_fds;

    int port_server = atoi(argv[3]);

    FD_ZERO(&read_fds);
	FD_ZERO(&tmp_fds);

    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    int aux = 1;
    int deactivate = setsockopt(sockfd, IPPROTO_TCP, TCP_NODELAY, (char *)&aux, sizeof(int));
    if (sockfd < 0 || deactivate < 0) {
        fprintf(stderr, "tcp socket creation failed\n");
        exit(EXIT_FAILURE);
    }

    struct sockaddr_in servaddr;
    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(port_server);
    int rc = inet_aton(argv[2], &servaddr.sin_addr);

    if (rc == 0) {
        fprintf(stderr, "ERROR: %s is not a valid IP address.\n", argv[2]);
        exit(EXIT_FAILURE);
    }

    ret = connect(sockfd, (struct sockaddr*) &servaddr, sizeof(servaddr));
	if (ret < 0) {
        fprintf(stderr, "connect failed\n");
        exit(EXIT_FAILURE);
    }

    request *r = build_request(0, argv[1], 0);
    memset(buffer, 0, BUFLEN);
    memcpy(buffer, r, sizeof(request));

    n = send(sockfd, buffer, sizeof(request), 0);

    if (n < 0) {
        fprintf(stderr, "send failed\n");
        exit(EXIT_FAILURE);
    }


    FD_SET(STDIN_FILENO, &read_fds);
	FD_SET(sockfd, &read_fds);

    char payload[PAYLOADLEN];
    string type;
    string command, aux_string;

    while(1) {
        tmp_fds = read_fds; 

        ret = select(sockfd + 1, &tmp_fds, NULL, NULL, NULL);
		if (ret < 0) {
            fprintf(stderr, "select failed\n");
            exit(EXIT_FAILURE);
        }

        if (FD_ISSET(STDIN_FILENO, &tmp_fds)) {
            // se citeste de la tastatura
			memset(buffer, 0, BUFLEN);
            fgets(buffer, BUFLEN - 1, stdin);

            if (strncmp(buffer, "exit", 4) == 0 && strlen(buffer) == 5) {
                request *r = build_request(1, argv[1], 0);
                memset(buffer, 0, BUFLEN);
                memcpy(buffer, r, sizeof(request));

                n = send(sockfd, buffer, sizeof(request), 0);

                if (n < 0) {
                    fprintf(stderr, "send failed\n");
                    exit(EXIT_FAILURE);
                }
                break;
            } else if(strncmp(buffer, "subscribe ", 10) == 0) {
                // verificarea corectitudinii comenzii
                stringstream stream;
                command.assign(buffer);
                if(count(command.begin(), command.end(), ' ') != 2 || command.size() < 14) {
                    fprintf(stderr, "Invalid command!\n");
                    continue;
                }
                if(command.at(command.size()-3) != ' ') {
                    fprintf(stderr, "Invalid command!\n");
                    continue;
                }

                size_t second_space;
                for(second_space = 11; second_space < command.size(); second_space++) {
                    if(command.at(second_space) == ' ') {
                        break;
                    }
                }
                if(second_space > 60) {
                    fprintf(stderr, "Invalid command!\n");
                    continue;
                }
                if(command.at(second_space+1) != '0' && command.at(second_space+1) != '1') {
                    fprintf(stderr, "Invalid command!\n");
                    continue;
                }

                //construirea si trimiterea payload-ului
                memset(payload, 0, PAYLOADLEN);
                memcpy(payload, argv[1], strlen(argv[1]));
                if(strlen(argv[1]) < 10) {
                    payload[strlen(argv[1])] = '\0';
                    payload[9] = '\0';
                }

                memcpy(payload + 10, buffer + 10, strlen(buffer)-10);

                request *r = build_request(2, payload, strlen(buffer));
                memset(buffer, 0, BUFLEN);
                memcpy(buffer, r, sizeof(request));
                n = send(sockfd, buffer, sizeof(request), 0);
                if (n < 0) {
                    fprintf(stderr, "send failed\n");
                    exit(EXIT_FAILURE);
                }
                printf("Subscribed to topic.\n");
            } else if(strncmp(buffer, "unsubscribe ", 12) == 0) {
                // verificarea corectitudinii comenzii
                stringstream stream;
                command.assign(buffer);

                if(count(command.begin(), command.end(), ' ') != 1 || command.size() < 14) {
                    fprintf(stderr, "Invalid command!\n");
                    continue;
                }

                if(command.size() > 64) {
                    fprintf(stderr, "Invalid command!\n");
                    continue;
                }

                // construirea si trimiterea payload-ului
                memset(payload, 0, PAYLOADLEN);
                memcpy(payload, argv[1], strlen(argv[1]));
                if(strlen(argv[1]) < 10) {
                    payload[strlen(argv[1])] = '\0';
                    payload[9] = '\0';
                }

                memcpy(payload + 10, buffer + 12, strlen(buffer)-10);

                request *r = build_request(3, payload, strlen(buffer));
                memset(buffer, 0, BUFLEN);
                memcpy(buffer, r, sizeof(request));
                n = send(sockfd, buffer, sizeof(request), 0);
                if (n < 0) {
                    fprintf(stderr, "send failed\n");
                    exit(EXIT_FAILURE);
                }
                printf("Unsubscribed from topic.\n");
            } else {
                fprintf(stderr, "Invalid command!\n");
                continue;
            }
        }

        if (FD_ISSET(sockfd, &tmp_fds)) {
			memset(buffer, 0, BUFLEN);

            n = recv(sockfd, buffer, sizeof(message), 0);

            if (n < 0) {
                fprintf(stderr, "recv failed\n");
                exit(EXIT_FAILURE);
            }
            
            message *m = (message*)buffer;
            if(m->type == 4) {
                break;
            } else if(m->type >= 0 || m->type <= 3) {
                switch(m->type) {
                    case 0:
                        type = "INT"; break;
                    case 1:
                        type = "SHORT_REAL"; break;
                    case 2:
                        type = "FLOAT"; break;
                    case 3:
                        type = "STRING";
                }
                printf("%s:%s - %s - ", m->ip_address, m->port, m->topic);
                cout << type;
                printf(" - %s\n", m->payload);
            }
		}
    }

    close(sockfd);
}