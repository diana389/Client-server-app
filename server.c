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
    int active;
    int sf;
    char id_client[11];
} subscriber;

subscriber tcp_clients[100];
int tcp_clients_count = 0;

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

void exit_command(int fd)
{
    strcpy(buffer, "exit");
    int size_to_send = 5;
    if (send(fd, &size_to_send, sizeof(int), 0) < 0)
    {
        perror("[SERV] Can't send\n");
        exit(EXIT_FAILURE);
    }
    if (send(fd, buffer, size_to_send, 0) < 0)
    {
        perror("[SERV] Can't send exit command\n");
        exit(EXIT_FAILURE);
    }
    close(fd);
}

void sent_exit_to_all()
{
    if (tcp_clients_count > 0)
    {
        for (int i = 3; i < nfds; i++)
        {
            exit_command(pfds[i].fd);
        }
    }

    close(sockfd_udp);
    close(socket_desc);
    close(client_sock);

    printf("EXIT\n");
}

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

    for (int i = 0; i < tcp_clients_count; i++)
        if (strcmp(tcp_clients[i].id_client, id_client) == 0)
        {
            exit_command(client_sock);
            printf("Client %s already connected.\n", id_client);
            return -1;
        }

    printf("New client %s connected from %s:%i.\n", id_client, inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));

    strcpy(tcp_clients[tcp_clients_count].id_client, id_client);
    tcp_clients[tcp_clients_count].fd = client_sock;
    tcp_clients_count++;

    for (int t = 0; t < topics_count; t++)
        for (int s = 0; s < topics[t].subscribers_count; s++)
            if (topics[t].subscribers[s].fd == client_sock)
            {
                topics[t].subscribers[s].active = 1;

                if (topics[t].subscribers[s].sf == 0)
                    topics[t].subscribers[s].sent = topics[t].list_count;
            }

    return client_sock;
}

void add_message_to_topic(msg message)
{
    for (int i = 0; i < topics_count; i++)
        if (strcmp(topics[i].topic, message.topic) == 0)
        {
            topics[i].list[topics[i].list_count++] = message;
            return;
        }

    strcpy(topics[topics_count].topic, message.topic);
    topics[topics_count].list[topics[topics_count].list_count++] = message;
    topics_count++;
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

void send_messages()
{
    for (int t = 0; t < topics_count; t++)
        for (int s = 0; s < topics[t].subscribers_count; s++)
        {
            if (topics[t].subscribers[s].active == 1)
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

                    topics[t].subscribers[s].sent = topics[t].list_count;
                }
            }
        }
}

void subscribe(char *p, int fd)
{
    if (p != NULL)
    {
        p = strtok(NULL, " ");

        if (p != NULL)
        {

            for (int t = 0; t < topics_count; t++)
                if (strcmp(topics[t].topic, p) == 0)
                {
                    for (int s = 0; s < topics[t].subscribers_count; s++)
                        if (topics[t].subscribers[s].fd == fd)
                            return;

                    topics[t].subscribers[topics[t].subscribers_count].fd = fd;
                    topics[t].subscribers[topics[t].subscribers_count].active = 1;

                    p = strtok(NULL, " ");
                    if (p != NULL)
                    {
                        topics[t].subscribers[topics[t].subscribers_count].sf = p[0] - '0';
                    }

                    topics[t].subscribers_count++;
                    return;
                }

            strcpy(topics[topics_count].topic, p);
            topics[topics_count].subscribers[0].fd = fd;
            topics[topics_count].subscribers[0].active = 1;

            p = strtok(NULL, " ");
            if (p != NULL)
            {
                topics[topics_count].subscribers[0].sf = p[0] - '0';
            }

            topics[topics_count].subscribers_count++;
            topics_count++;
        }
    }
}

void unsubscribe(char *p, int fd)
{
    p = strtok(NULL, " ");

    if (p != NULL)
    {
        for (int t = 0; t < topics_count; t++)
            if (strcmp(topics[t].topic, p) == 0)
            {
                for (int s = 0; s < topics[t].subscribers_count; s++)
                    if (topics[t].subscribers[s].fd == fd)
                    {
                        for (int x = s; x < topics[t].subscribers_count - 1; x++)
                            topics[t].subscribers[x] = topics[t].subscribers[x + 1];

                        topics[t].subscribers_count--;
                        break;
                    }

                break;
            }
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
                    sent_exit_to_all();
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

            if (sock != -1)
            { /* add tcp socket */
                pfds[nfds].fd = sock;
                pfds[nfds].events = POLLIN;
                nfds++;
            }
        }
        else
        {
            for (int i = 3; i < nfds; i++)
                if ((pfds[i].revents & POLLIN) != 0)
                {
                    int n = recv(pfds[i].fd, buffer, 100, 0);

                    if (n == 0)
                    {
                        printf("Client disconnected\n");
                        for (int t = 0; t < topics_count; t++)
                            for (int s = 0; s < topics[t].subscribers_count; s++)
                                if (topics[t].subscribers[s].fd == pfds[i].fd)
                                    topics[t].subscribers[s].active = 0;

                        int index = -1;

                        for (int j = 0; j < tcp_clients_count; j++)
                            if (tcp_clients[j].fd == pfds[i].fd)
                            {
                                index = j;
                                break;
                            }

                        if (index != -1)
                        {
                            for (int j = index; j < tcp_clients_count - 1; j++)
                                tcp_clients[j] = tcp_clients[j + 1];

                            tcp_clients_count--;
                        }

                        for (int j = i; j < nfds; j++)
                            pfds[j] = pfds[j + 1];

                        nfds--;
                        break;
                    }

                    printf("%s\n", buffer);
                    char *p = strtok(buffer, " ");

                    if (strcmp(p, "subscribe") == 0)
                    {
                        subscribe(p, pfds[i].fd);
                    }
                    else if (strcmp(p, "unsubscribe") == 0)
                    {
                        unsubscribe(p, pfds[i].fd);
                    }
                }
        }

        if (tcp_clients_count > 0 && msg_count > 0)
        {
            send_messages();
        }
    }
}
