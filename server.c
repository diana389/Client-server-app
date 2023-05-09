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
#include <netinet/tcp.h>

#define MAXLINE 2000

int PORT;
int sockfd_udp;
char input[100];
struct sockaddr_in servaddr, cliaddr;

int socket_desc, client_sock;
unsigned int client_size;
struct sockaddr_in client_addr;
char server_message[1000];

char buffer[MAXLINE + 60];

struct pollfd pfds[1000];
int nfds = 0;

typedef struct msg
{
    int size;                   // size of content
    struct sockaddr_in cliaddr; // udp client structure
    char topic[51];
    char content[MAXLINE]; // content received from UDP
} msg;

int msg_count = 0;

typedef struct subscriber
{
    int fd;
    int sent;   // number of messages
    int active; // 1 if client is connected
    int sf;     // option store-and-forward
    char id_client[11];
} subscriber;

subscriber tcp_clients[100]; // all connected tcp clients
int tcp_clients_count = 0;   // number pf tcp connectes clients

typedef struct topic
{
    char topic[51];
    msg list[100];               // messages to this topic
    int list_count;              // list size
    subscriber subscribers[100]; // tcp clients subscribed on this topic
    int subscribers_count;       // numbers of subscribers
} topic;

topic topics[100]; // array of topics
int topics_count = 0;

// sends "exit" message to a client
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
// sends "exit" message to all clients and closes sockets
void sent_exit_to_all()
{
    for (int i = 0; i < tcp_clients_count; i++)
        exit_command(tcp_clients[i].fd);

    close(sockfd_udp);
    close(socket_desc);
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

    // Disable Nagle algorithm
    int flag = 1;
    if (setsockopt(socket_desc, IPPROTO_TCP, TCP_NODELAY, (char *)&flag, sizeof(int)) != 0)
        perror("Error TCP_NODELAY\n");

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

    /* Listen for clients */
    if (listen(socket_desc, 35) < 0)
    {
        perror("Error while listening\n");
        exit(EXIT_FAILURE);
    }
}

int tcp_client_accept()
{
    /*  Accept an incoming connection from one of the clients */
    client_size = sizeof(client_addr);
    client_sock = accept(socket_desc, (struct sockaddr *)&client_addr, &client_size);

    if (client_sock < 0)
        return -1;

    // Disable Nagle algorithm
    int flag = 1;
    if (setsockopt(client_sock, IPPROTO_TCP, TCP_NODELAY, (char *)&flag, sizeof(int)) != 0)
        perror("Error TCP_NODELAY\n");

    char id_client[11];
    /* Receive message from clients. Note that we use client_sock, not socket_desc */
    int size_id_client = recv(client_sock, id_client, sizeof(id_client), 0);

    if (size_id_client < 0)
    {
        perror("[SERV] Couldn't receive\n");
        exit(EXIT_FAILURE);
    }

    id_client[size_id_client] = '\0';

    // checks if someone is already connected with this ID
    for (int i = 0; i < tcp_clients_count; i++)
        if (strcmp(tcp_clients[i].id_client, id_client) == 0)
        {
            exit_command(client_sock);
            printf("Client %s already connected.\n", id_client);
            return -1;
        }

    printf("New client %s connected from %s:%i.\n", id_client, inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));

    // add client to vector
    strcpy(tcp_clients[tcp_clients_count].id_client, id_client);
    tcp_clients[tcp_clients_count].fd = client_sock;
    tcp_clients_count++;

    // find the topics the client is subscribed to
    for (int t = 0; t < topics_count; t++)
        for (int s = 0; s < topics[t].subscribers_count; s++)
            if (strcmp(topics[t].subscribers[s].id_client, id_client) == 0)
            {
                topics[t].subscribers[s].active = 1;       // client is now active
                topics[t].subscribers[s].fd = client_sock; // update fd

                if (topics[t].subscribers[s].sf == 0)
                    topics[t].subscribers[s].sent = topics[t].list_count; // update index
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

// function to print topics (for debugging)
void print_only_topics()
{
    for (int i = 0; i < topics_count; i++)
    {
        printf("%d %s:", i, topics[i].topic);
        for (int j = 0; j < topics[i].subscribers_count; j++)
            printf("%d", topics[i].subscribers[j].fd);
        printf("\n");
    }
}

// function to print tcp_clients (for debugging)
void print_tcp_clients()
{
    for (int i = 0; i < tcp_clients_count; i++)
        printf("%d ", tcp_clients[i].fd);

    printf("\n");
}

// function to print topics and subscribers (for debugging)
void print_topics()
{
    for (int i = 0; i < topics_count; i++)
    {
        printf("%s :", topics[i].topic);
        for (int j = 0; j < topics[i].subscribers_count; j++)
            printf("(%s %d) ", topics[i].subscribers[j].id_client, topics[i].subscribers[j].fd);
        printf("\n");
    }
}

void send_messages()
{
    for (int t = 0; t < topics_count; t++)
        for (int s = 0; s < topics[t].subscribers_count; s++)
        {
            if (topics[t].subscribers[s].active == 1) // check if client is connect
            {
                if (topics[t].list_count > topics[t].subscribers[s].sent) // check if there are any messages to send
                {
                    for (int mess = topics[t].subscribers[s].sent; mess < topics[t].list_count; mess++)
                    {
                        msg m = topics[t].list[mess];

                        int size_to_send = sizeof(int) + sizeof(struct sockaddr) + 51 + m.size; // compute size of the data

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

                    topics[t].subscribers[s].sent = topics[t].list_count; // update last message sent index
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
            // find client ID
            char id[11];
            for (int i = 0; i < tcp_clients_count; i++)
                if (tcp_clients[i].fd == fd)
                {
                    strcpy(id, tcp_clients[i].id_client);
                    break;
                }

            for (int t = 0; t < topics_count; t++)
                if (strcmp(topics[t].topic, p) == 0) // find topic
                {
                    // check if client is already subscribed
                    for (int s = 0; s < topics[t].subscribers_count; s++)
                        if (strcmp(topics[t].subscribers[s].id_client, id) == 0)
                        {
                            topics[t].subscribers[s].fd = fd; // update fd
                            return;
                        }

                    // add subscriber
                    topics[t].subscribers[topics[t].subscribers_count].fd = fd;
                    topics[t].subscribers[topics[t].subscribers_count].active = 1;

                    strcpy(topics[t].subscribers[topics[t].subscribers_count].id_client, id);

                    // add sf option
                    p = strtok(NULL, " ");
                    if (p != NULL)
                        topics[t].subscribers[topics[t].subscribers_count].sf = p[0] - '0';

                    topics[t].subscribers[topics[t].subscribers_count].sent = topics[t].list_count;

                    topics[t].subscribers_count++;
                    return;
                }

            // topic doesn't exist yet => add topic and subscribe
            strcpy(topics[topics_count].topic, p);
            topics[topics_count].subscribers[0].fd = fd;
            topics[topics_count].subscribers[0].active = 1;
            strcpy(topics[topics_count].subscribers[0].id_client, id);

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
            if (strcmp(topics[t].topic, p) == 0) // find topic
            {
                for (int s = 0; s < topics[t].subscribers_count; s++)
                    if (topics[t].subscribers[s].fd == fd)
                    {
                        // remove subscriber from list
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
    sscanf(argv[1], "%d", &PORT);

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

        if ((pfds[0].revents & POLLIN) != 0) // stdin
        {
            if (fgets(input, 100, stdin))
            {
                if (strcmp(input, "exit\n") == 0)
                {
                    sent_exit_to_all(); // close server
                    return 0;
                }
            }
        }
        else if ((pfds[1].revents & POLLIN) != 0) // udp
        {
            socklen_t len = sizeof(cliaddr);

            int n = recvfrom(sockfd_udp, (char *)buffer, MAXLINE,
                             MSG_WAITALL, (struct sockaddr *)&cliaddr,
                             &len);

            // received a message
            if (n != -1)
            {
                buffer[n] = '\n';

                // declare a new message
                msg message;
                memset(&message, 0, sizeof(message));

                message.size = n;
                message.cliaddr = cliaddr;

                memcpy(&message.topic, buffer, 50 * sizeof(char));
                message.topic[51] = '\0';

                memcpy(&message.content, buffer, (n + 1) * sizeof(char));

                add_message_to_topic(message);
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
            // received input from a tcp client
            for (int i = 3; i < nfds; i++)
                if ((pfds[i].revents & POLLIN) != 0)
                {
                    int n = recv(pfds[i].fd, buffer, 100, 0);

                    // client disconnected
                    if (n == 0)
                    {
                        // set client active = 0 in all topic structures
                        for (int t = 0; t < topics_count; t++)
                            for (int s = 0; s < topics[t].subscribers_count; s++)
                                if (topics[t].subscribers[s].fd == pfds[i].fd)
                                    topics[t].subscribers[s].active = 0;

                        // find client index in tcp_clients
                        int index = -1;

                        for (int j = 0; j < tcp_clients_count; j++)
                            if (tcp_clients[j].fd == pfds[i].fd)
                            {
                                index = j;
                                break;
                            }

                        // remove client from tcp_clients
                        if (index != -1)
                        {
                            printf("Client %s disconnected.\n", tcp_clients[index].id_client);
                            for (int j = index; j < tcp_clients_count - 1; j++)
                                tcp_clients[j] = tcp_clients[j + 1];

                            tcp_clients_count--;
                        }

                        // remove client from poll
                        for (int j = i; j < nfds; j++)
                            pfds[j] = pfds[j + 1];

                        nfds--;
                        break;
                    }

                    // received a command

                    buffer[n] = '\n';
                    char *p = strtok(buffer, " ");

                    if (strcmp(p, "subscribe") == 0)
                        subscribe(p, pfds[i].fd);
                    else if (strcmp(p, "unsubscribe") == 0)
                        unsubscribe(p, pfds[i].fd);
                }
        }

        send_messages();
    }
}
