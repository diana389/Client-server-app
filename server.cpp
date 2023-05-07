#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <iostream>
#include <map>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <unistd.h>
#include <unordered_set>
#include <vector>

using namespace std;

#define BUFLEN 1600

typedef struct message {
    char topic[50];
    int type;
    char payload[1500];
    char ip_address[16];
    char port[6];
} message;

typedef struct request {
    int type;
    char payload[65];
} request;

message *build_msg(char *buf) {
    message *m = (message*)malloc(sizeof(message));

    sprintf(m->topic, "%s", buf);

    m->type = 0;
    memcpy(&m->type, buf + 50, sizeof(char));

    if(m->type == 0) {
        uint8_t sign = 0;
        memcpy(&sign, buf + 51, sizeof(uint8_t));

        uint32_t integer = 0;
        memcpy(&integer, buf + 52, sizeof(uint32_t));
        integer = ntohl(integer);
        if(sign == 1) {
            integer *= -1;
        }

        sprintf(m->payload, "%d", integer);
    } else if(m->type == 1) {
        uint16_t sh = 0;
        memcpy(&sh, buf + 51, sizeof(uint16_t));
        sh = ntohs(sh);

        double sh_real = (double)sh / 100;
        if(sh_real == (int)sh_real) {
            sprintf(m->payload, "%d", (int)sh_real);
        } else {
            sprintf(m->payload, "%.2f", sh_real);
        }
    } else if(m->type == 2) {
        uint8_t sign = 0;
        memcpy(&sign, buf + 51, sizeof(uint8_t));

        uint32_t mod = 0;
        memcpy(&mod, buf + 52, sizeof(uint32_t));
        mod = ntohl(mod);

        uint8_t power = 0;
        memcpy(&power, buf + 56, sizeof(uint8_t));

        double rez = 0;
        rez += mod;
        for(u_int8_t i = 0; i < power; i++) {
            rez /= 10;
        }
        if(sign == 1) {
            rez *= -1;
        }
        sprintf(m->payload, "%.*f", power, rez);
    } else if(m->type == 3) {
        memcpy(m->payload, buf + 51, 1500);
    }

    return m;
}

int main(int argc, char **argv) {
    setvbuf(stdout, NULL, _IONBF, BUFSIZ);

    if (argc != 2) {
        fprintf(stderr, "Usage: %s <port>\n", argv[0]);
        fprintf(stderr, "Eg: %s 1948\n", argv[0]);
        exit(EXIT_FAILURE);
    }


    //UDP
    int sockfd_udp;
    // Creating udp socket file descriptor
    if ((sockfd_udp = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        fprintf(stderr, "udp socket creation failed\n");
        exit(EXIT_FAILURE);
    }

    int port = atoi(argv[1]);

    struct sockaddr_in servaddr;
    memset((char*)&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET; // IPv4
    servaddr.sin_addr.s_addr = INADDR_ANY;
    servaddr.sin_port = htons(port);

    int rc = bind(sockfd_udp, (const struct sockaddr *)&servaddr, sizeof(servaddr));
    if (rc < 0) {
        fprintf(stderr, "udp bind failed\n");
        exit(EXIT_FAILURE);
    }

    //TCP
    int sockfd_tcp, newsockfd_tcp;
	struct sockaddr_in cli_addr_tcp;
	socklen_t clilen_tcp;

	fd_set read_fds;	// multimea de citire folosita in select()
	fd_set tmp_fds;		// multime folosita temporar
	int fdmax;			// valoare maxima fd din multimea read_fds

    FD_ZERO(&read_fds);
	FD_ZERO(&tmp_fds);

    sockfd_tcp = socket(AF_INET, SOCK_STREAM, 0);
    int aux = 1;
    int deactivate = setsockopt(sockfd_tcp, IPPROTO_TCP, TCP_NODELAY, (char *)&aux, sizeof(int));
    if (sockfd_tcp < 0 || deactivate < 0) {
        fprintf(stderr, "tcp socket creation failed\n");
        exit(EXIT_FAILURE);
    }

    int ret = bind(sockfd_tcp, (struct sockaddr *) &servaddr, sizeof(struct sockaddr));
	if (ret < 0) {
        fprintf(stderr, "tcp bind failed\n");
        exit(EXIT_FAILURE);
    }

    ret = listen(sockfd_tcp, 1);
	if (ret < 0) {
        fprintf(stderr, "listen failed\n");
        exit(EXIT_FAILURE);
    }

    FD_SET(STDIN_FILENO, &read_fds);
    FD_SET(sockfd_tcp, &read_fds);
    FD_SET(sockfd_udp, &read_fds);
	fdmax = sockfd_tcp;

    char buffer[BUFLEN];
    struct sockaddr_in cliaddr_udp;
    socklen_t len_udp = sizeof(cliaddr_udp);
    memset(&cliaddr_udp, 0, sizeof(cliaddr_udp));

    map <string, unordered_set<string> > subscriptions;  
    string client_id;
    string topic_and_sf;
    string topic; 

    map <string, int> sockets_map;

    while(1) {
        tmp_fds = read_fds; 
		
		ret = select(fdmax + 1, &tmp_fds, NULL, NULL, NULL);
		if (ret < 0) {
            fprintf(stderr, "select failed\n");
            exit(EXIT_FAILURE);
        }

        if (FD_ISSET(STDIN_FILENO, &tmp_fds)) {
            // se citeste de la tastatura
            memset(buffer, 0, BUFLEN);
            fgets(buffer, BUFLEN - 1, stdin);

            if (strncmp(buffer, "exit", 4) == 0 && strlen(buffer) == 5) {
                message *m = (message*)malloc(sizeof(message));
                m->type = 4;
                memset(buffer, 0, BUFLEN);
                memcpy(buffer, m, sizeof(message));

                for(int i = 5; i <= fdmax; i++) {
                    send(i, buffer, sizeof(message), 0);
                    close(i);
                }
                break;
            } else {
                fprintf(stderr, "Invalid command!\n");
            }
        }

        for(int i = 1; i <= fdmax; i++) {
            
            if(FD_ISSET(i, &tmp_fds)) {
                if(i == sockfd_tcp) {
                    // a venit o cerere de conexiune pe socketul inactiv (cel cu listen),
					// pe care serverul o accepta
                    clilen_tcp = sizeof(cli_addr_tcp);
                    newsockfd_tcp = accept(sockfd_tcp, (struct sockaddr *) &cli_addr_tcp, &clilen_tcp);
                    if (newsockfd_tcp < 0) {
                        fprintf(stderr, "accept failed\n");
                        exit(EXIT_FAILURE);
                    }

                    // se adauga noul socket intors de accept() la multimea descriptorilor de citire
                    FD_SET(newsockfd_tcp, &read_fds);
					if (newsockfd_tcp > fdmax) { 
						fdmax = newsockfd_tcp;
					}

                
                } else if(i == sockfd_udp) {
                    memset(buffer, 0, BUFLEN);
                    int n = recvfrom(sockfd_udp, buffer, sizeof(buffer), 0,
                                (struct sockaddr *)&cliaddr_udp, &len_udp);
                    if (n < 0) {
                        fprintf(stderr, "recvfrom failed\n");
                        exit(EXIT_FAILURE);
                    } 
                    message *m = build_msg(buffer);

                    strcpy(m->ip_address, inet_ntoa(cliaddr_udp.sin_addr));
                    sprintf(m->port, "%d", ntohs(cliaddr_udp.sin_port));

                    topic.assign(m->topic);

                    for (auto const &pair: subscriptions) {
                        if(pair.second.count(topic) == 1 && sockets_map.count(pair.first) == 1) {
                            int sock = sockets_map.at(pair.first);
                            send(sock, m, sizeof(message), 0);
                        }
                    }
                } else {
                    // s-au primit date pe unul din socketii de client,
					// asa ca serverul trebuie sa le receptioneze
                    memset(buffer, 0, BUFLEN);
					ret = recv(i, buffer, sizeof(request), 0);
					if (ret < 0) {
                        cout << i;
                        fprintf(stderr, "recv failed\n");
                        exit(EXIT_FAILURE);
                    } else if(ret == 0) {
                        close(i);

						FD_CLR(i, &read_fds);
                    } else {
                        request *r = (request*)malloc(sizeof(request));
                        r = (request*)buffer;

                        if(r->type == 0) {
                            if(r->payload[9] == '\0') {
                                client_id.assign(r->payload);
                            } else {
                                client_id.assign(r->payload, 10);
                            }
                            if(sockets_map.count(client_id) > 0) {
                                message *m = (message*)malloc(sizeof(message));
                                m->type = 4;
                                memset(buffer, 0, BUFLEN);
                                memcpy(buffer, m, sizeof(message));
                                send(i, buffer, sizeof(message), 0);
                                FD_CLR(i, &read_fds);
                                cout << "Client ";
                                cout << client_id;
                                cout << " already connected.\n";
                                continue;
                            }

                            printf("New client %s connected from %s:%d.\n", r->payload,
                                inet_ntoa(cli_addr_tcp.sin_addr), ntohs(cli_addr_tcp.sin_port));

                            sockets_map[client_id] = newsockfd_tcp;
                        } else if(r->type == 1) {
                            if(r->payload[9] == '\0') {
                                client_id.assign(r->payload);
                            } else {
                                client_id.assign(r->payload, 10);
                            }
                            sockets_map.erase(client_id);
                            FD_CLR(i, &read_fds);
                            printf("Client %s disconnected.\n", r->payload);
                        } else if(r->type == 2) {
                            if(r->payload[9] == '\0') {
                                client_id.assign(r->payload);
                            } else {
                                client_id.assign(r->payload, 10);
                            }

                            topic_and_sf.assign(r->payload + 10);
                            topic.assign(r->payload + 10, topic_and_sf.size()-2);
                            int sf = atoi(r->payload + 10 + topic_and_sf.size() - 2);

                            if(subscriptions.count(client_id) == 0) {
                                unordered_set<string> v;
                                subscriptions.insert ( std::pair<string,unordered_set<string>>(client_id, v) );
                            }
                            topic.pop_back();
                            subscriptions.at(client_id).insert(topic);
                        } else if(r->type == 3) {
                            if(r->payload[9] == '\0') {
                                client_id.assign(r->payload);
                            } else {
                                client_id.assign(r->payload, 10);
                            }
                            topic.assign(r->payload + 10);

                            topic.pop_back();

                            if(subscriptions.count(client_id) == 0) {
                                continue;
                            }
                            subscriptions.at(client_id).erase(topic);
                        }
                    }
                }
            }
        }
    }
    for(int i = 5; i <= fdmax; i++) {
        close(i);
    }
    close(sockfd_tcp);
    close(sockfd_udp);
}