#define _XOPEN_SOURCE 700 // for fileno() to work

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <math.h>
#include <sys/poll.h>

#define MAXLINE 2000

int PORT;
int flags;
int sockfd_udp;
char input[100];
struct sockaddr_in servaddr, cliaddr;

// int sockfd_tcp;
int socket_desc, client_sock;
unsigned int client_size;
struct sockaddr_in client_addr;
char server_message[1000];

char buffer[MAXLINE + 60];

struct pollfd pfds[1000];
int nfds = 0;

typedef struct msg
{
    int size;
    struct sockaddr_in cliaddr;
    char topic[50];
    char content[MAXLINE];
} msg;

int msg_count = 0;
msg messages[100];

typedef struct subscriber
{
    int fd;
    int sent;
} subscriber;

typedef struct topic
{
    char topic[50];
    msg list[100];
    int list_count;
    subscriber subscribers[100];
    int subscribers_count;
} topic;

topic topics[100];
int topics_count = 0;

int tcp_clients_count = 0;

void create_bind_udp_client()
{
    // Creating socket file descriptor
    if ((sockfd_udp = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
    {
        perror("socket creation failed");
        exit(EXIT_FAILURE);
    }

    memset(&servaddr, 0, sizeof(servaddr));
    memset(&cliaddr, 0, sizeof(cliaddr));

    // Filling server information
    servaddr.sin_family = AF_INET;         // IPv4
    servaddr.sin_addr.s_addr = INADDR_ANY; // INADDR_ANY = 0.0.0.0
    servaddr.sin_port = htons(PORT);

    // Bind the socket with the server address
    if (bind(sockfd_udp, (const struct sockaddr *)&servaddr,
             sizeof(servaddr)) < 0)
    {
        perror("bind failed");
        exit(EXIT_FAILURE);
    }
}

void create_bind_listen_tcp_client()
{
    /* Clean buffers and structures*/
    memset(server_message, 0, sizeof(server_message));
    memset(&client_addr, 0, sizeof(client_addr));

    /* Create socket */
    socket_desc = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_desc < 0)
    {
        perror("[SERV] Error while creating socket\n");
        exit(EXIT_FAILURE);
    }
    // printf("[SERV] Socket created successfully\n");

    // Filling server information
    servaddr.sin_family = AF_INET;         // IPv4
    servaddr.sin_addr.s_addr = INADDR_ANY; // INADDR_ANY = 0.0.0.0
    servaddr.sin_port = htons(PORT);

    /* Bind to the set port and IP */
    if (bind(socket_desc, (struct sockaddr *)&servaddr, sizeof(servaddr)) < 0)
    {
        perror("[SERV] Couldn't bind to the port\n");
        exit(EXIT_FAILURE);
    }
    // printf("[SERV] Binding completed successfully\n");

    /* Listen for clients */
    if (listen(socket_desc, 1) < 0)
    {
        perror("Error while listening\n");
        exit(EXIT_FAILURE);
    }
    // printf("[SERV] Start listening for incoming connections.....\n");
}

int tcp_client_accept()
{
    // set tcp socket to non-blocking mode
    flags = fcntl(socket_desc, F_GETFL, 0);
    fcntl(socket_desc, F_SETFL, flags | O_NONBLOCK);

    /*  Accept an incoming connection from one of the clients */
    client_size = sizeof(client_addr);
    client_sock = accept(socket_desc, (struct sockaddr *)&client_addr, &client_size);

    if (client_sock < 0)
        return -1;

    char id_client[11];
    /* Receive message from clients. Note that we use client_sock, not socket_desc */
    int size_id_client = recv(client_sock, id_client, sizeof(id_client), 0);

    if (size_id_client < 0)
    {
        perror("[SERV] Couldn't receive\n");
        exit(EXIT_FAILURE);
    }
    // printf("[SERV] Received id from client: %s\n", id_client);

    id_client[size_id_client] = '\0';
    printf("New client %s connected from %s:%i.\n", id_client, inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));

    return client_sock;
}

void exit_command()
{
    strcpy(buffer, "exit");
    int size_to_send = 5;

    if (tcp_clients_count > 0)
    {
        for (int i = 3; i < nfds; i++)
        {
            if (send(pfds[i].fd, &size_to_send, sizeof(int), 0) < 0)
            {
                perror("[SERV] Can't send\n");
                exit(EXIT_FAILURE);
            }
            if (send(pfds[i].fd, buffer, size_to_send, 0) < 0)
            {
                perror("[SERV] Can't send exit command\n");
                exit(EXIT_FAILURE);
            }
            close(pfds[i].fd);
        }
    }

    close(sockfd_udp);
    close(socket_desc);
    close(client_sock);

    printf("EXIT\n");
}

void add_message_to_topic(msg message)
{
    int is_topic = 0;

    for (int i = 0; i < topics_count; i++)
        if (strcmp(topics[i].topic, message.topic) == 0)
        {
            topics[i].list[topics[i].list_count++] = message;
            is_topic = 1;
            break;
        }

    if (is_topic == 0)
    {
        strcpy(topics[topics_count].topic, message.topic);
        topics[topics_count].list[topics[topics_count].list_count++] = message;
        topics_count++;
    }
}

void print_topics()
{
    for (int i = 0; i < topics_count; i++)
    {
        printf("%d %s:", i, topics[i].topic);
        for (int j = 0; j < topics[i].subscribers_count; j++)
            printf("%d", topics[i].subscribers[j].fd);
        printf("\n");
    }
}

int main(int argc, char const *argv[])
{
    PORT = atoi(argv[1]);
    setvbuf(stdout, NULL, _IONBF, BUFSIZ);

    /* UDP CLIENT */
    create_bind_udp_client();

    /* TCP CLIENT */
    create_bind_listen_tcp_client();

    /* stdin */
    pfds[nfds].fd = STDIN_FILENO;
    pfds[nfds].events = POLLIN;
    nfds++;

    /* add udp socket */
    pfds[nfds].fd = sockfd_udp;
    pfds[nfds].events = POLLIN;
    nfds++;

    /* add listener socket */
    pfds[nfds].fd = socket_desc;
    pfds[nfds].events = POLLIN;
    nfds++;

    while (1)
    {
        memset(buffer, 0, sizeof(buffer));

        poll(pfds, nfds, -1);

        if ((pfds[0].revents & POLLIN) != 0)
        {
            if (fgets(input, 100, stdin))
            {
                if (strcmp(input, "exit\n") == 0)
                {
                    exit_command();
                    return 0;
                }

                if (strcmp(input, "print\n") == 0)
                    print_topics();
            }
        }
        else if ((pfds[1].revents & POLLIN) != 0) // udp
        {
            socklen_t len = sizeof(cliaddr);

            int n = recvfrom(sockfd_udp, (char *)buffer, MAXLINE,
                             MSG_WAITALL, (struct sockaddr *)&cliaddr,
                             &len);

            if (n != -1)
            {
                buffer[n] = '\n';

                messages[msg_count].size = n;
                messages[msg_count].cliaddr = cliaddr;
                printf("Sent from %s:%i.\n", inet_ntoa(cliaddr.sin_addr), ntohs(cliaddr.sin_port));

                memcpy(&messages[msg_count].topic, buffer, 50 * sizeof(char));
                memcpy(&messages[msg_count].content, buffer, n * sizeof(char));

                // printf("%d %s\n", msg_count, messages[msg_count].content);

                add_message_to_topic(messages[msg_count]);
                msg_count++;
            }
        }
        else if ((pfds[2].revents & POLLIN) != 0) // tcp
        {
            int sock = tcp_client_accept();
            tcp_clients_count++;

            /* add tcp socket */
            pfds[nfds].fd = sock;
            pfds[nfds].events = POLLIN;
            nfds++;
        }
        else
        {
            for (int i = 3; i < nfds; i++)
                if ((pfds[i].revents & POLLIN) != 0)
                {
                    recv(pfds[i].fd, buffer, 100, 0);
                    printf("%s\n", buffer);

                    char *p = strtok(buffer, " ");

                    if (strcmp(p, "subscribe") == 0)
                    {
                        p = strtok(NULL, " ");
                        for (int j = 0; j < topics_count; j++)
                            if (strcmp(topics[j].topic, p) == 0)
                            {
                                topics[j].subscribers[topics[j].subscribers_count++].fd = pfds[i].fd;
                                break;
                            }
                    }
                    else if (strcmp(p, "unsubscribe") == 0)
                    {
                        p = strtok(NULL, " ");
                    }
                }
        }

        if (tcp_clients_count > 0 && msg_count > 0)
        {
            // for (int i = 3; i < nfds; i++)
            // {
            //     for (int j = 0; j < msg_count; j++)
            //     {
            //         // printf("size = %d\n", messages[j].size);

            //         int size_to_send = sizeof(int) + sizeof(struct sockaddr) + 50 + messages[j].size;

            //         if (send(pfds[i].fd, &size_to_send, sizeof(int), 0) < 0)
            //         {
            //             perror("[SERV] Can't send\n");
            //             exit(EXIT_FAILURE);
            //         }

            //         if (send(pfds[i].fd, &messages[j], size_to_send, 0) < 0)
            //         {
            //             perror("[SERV] Can't send\n");
            //             exit(EXIT_FAILURE);
            //         }

            //         // printf("SENT!\n");
            //     }
            // }

            // msg_count = 0;

            for (int t = 0; t < topics_count; t++)
                for (int s = 0; s < topics[t].subscribers_count; s++)
                {
                    if (topics[t].list_count > topics[t].subscribers[s].sent)
                    {
                        for (int mess = topics[t].subscribers[s].sent; mess < topics[t].list_count; mess++)
                        {
                            msg m = topics[t].list[mess];

                            int size_to_send = sizeof(int) + sizeof(struct sockaddr) + 50 + m.size;

                            if (send(topics[t].subscribers[s].fd, &size_to_send, sizeof(int), 0) < 0)
                            {
                                perror("[SERV] Can't send\n");
                                exit(EXIT_FAILURE);
                            }

                            if (send(topics[t].subscribers[s].fd, &m, size_to_send, 0) < 0)
                            {
                                perror("[SERV] Can't send\n");
                                exit(EXIT_FAILURE);
                            }
                        }
                    }

                    topics[t].subscribers[s].sent = topics[t].list_count;
                }
        }
    }

    return 0;
}
