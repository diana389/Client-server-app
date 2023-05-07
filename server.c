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

int tcp_clients_count = 0;

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

void create_bind_udp_client()
{
    // Creating socket file descriptor
    if ((sockfd_udp = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
    {
        perror("socket creation failed");
        exit(EXIT_FAILURE);
    }

    // // set socket to non-blocking mode
    // flags = fcntl(sockfd_udp, F_GETFL, 0);
    // fcntl(sockfd_udp, F_SETFL, flags | O_NONBLOCK);

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

int main(int argc, char const *argv[])
{
    PORT = atoi(argv[1]);

    // int stdin_fd = fileno(stdin);                 // stdin corresponding file descriptor
    // flags = fcntl(stdin_fd, F_GETFL);             // gets the current file status flags
    // fcntl(stdin_fd, F_SETFL, flags | O_NONBLOCK); // sets the fd to non-blocking mode

    setvbuf(stdout, NULL, _IONBF, BUFSIZ);

    /* UDP CLIENT */
    create_bind_udp_client();

    /* TCP CLIENT */
    create_bind_listen_tcp_client();

    // // set udp socket to non-blocking mode
    // flags = fcntl(sockfd_udp, F_GETFL, 0);
    // fcntl(sockfd_udp, F_SETFL, flags | O_NONBLOCK);

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
        char buffer[MAXLINE + 60];
        memset(buffer, 0, sizeof(buffer));

        poll(pfds, nfds, -1);

        if ((pfds[0].revents & POLLIN) != 0)
        {
            if (fgets(input, 100, stdin))
            {
                input[strlen(input)] = '\0';

                if (strcmp(input, "exit\n") == 0)
                {
                    strcpy(buffer, "exit");

                    if (tcp_clients_count > 0 && send(client_sock, buffer, 5, 0) < 0)
                    {
                        perror("[SERV] Can't send exit command\n");
                        exit(EXIT_FAILURE);
                    }

                    close(sockfd_udp);
                    close(socket_desc);
                    close(client_sock);

                    printf("EXIT\n");
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

            if (n != -1)
            {
                buffer[n] = '\n';

                messages[msg_count].size = n;
                messages[msg_count].cliaddr = client_addr;
                memcpy(&messages[msg_count].topic, buffer, 50 * sizeof(char));
                memcpy(&messages[msg_count].content, buffer, n * sizeof(char));

                printf("%d %s\n", msg_count, messages[msg_count].content);

                msg_count++;
            }
        }
        else if ((pfds[2].revents & POLLIN) != 0) // tcp
        {
            int sock = tcp_client_accept();
            tcp_clients_count++;

            for (int i = 0; i < msg_count; i++)
            {
                printf("size = %d\n", messages[i].size);
                // printf("size = %ld\n", sizeof(messages[i]));

                // char *buf = calloc(2000, sizeof(char));

                // memcpy(buf, &messages[i], sizeof(messages[i]));

                // memcpy(buffer, &messages[i].size, sizeof(int));
                // memcpy(buffer + 4, &messages[i].buffer, messages[i].size * sizeof(char));


                if (send(client_sock, &messages[i], sizeof(messages[i]), 0) < 0)
                {
                    perror("[SERV] Can't send\n");
                    exit(EXIT_FAILURE);
                }

                printf("SENT!\n");

                // if (send(client_sock, &messages[i].buffer, messages[i].size, 0) < 0)
                // {
                //     perror("[SERV] Can't send\n");
                //     exit(EXIT_FAILURE);
                // }
            }

            msg_count = 0;
        }
        else
        {
        }

        // // if (tcp_client_accept() == 0)
        // //     printf("Nobody accepted\n");
        // /* read user data from standard input */

        // if (tcp_client_accept() == 1)
        //     tcp_clients_count++;

        // char buffer[MAXLINE + 60];
        // memset(buffer, 0, sizeof(buffer));

        // int input_size = read(stdin_fd, input, 100);
        // if (input_size > 0)
        // {
        //     // printf("\nEnd of input\n");
        //     input[input_size] = '\0';

        //     if (strcmp(input, "exit\n") == 0)
        //     {
        //         strcpy(buffer, "exit");

        //         if (tcp_clients_count > 0 && send(client_sock, buffer, 5, 0) < 0)
        //         {
        //             perror("[SERV] Can't send exit command\n");
        //             exit(EXIT_FAILURE);
        //         }

        //         close(sockfd_udp);
        //         close(socket_desc);
        //         close(client_sock);

        //         // printf("EXIT\n");
        //         return 0;
        //     }
        // }

        // /* UDP CLIENT */

        // socklen_t len = sizeof(cliaddr);

        // int n = recvfrom(sockfd_udp, (char *)buffer, MAXLINE,
        //                  MSG_WAITALL, (struct sockaddr *)&cliaddr,
        //                  &len);

        // if (n != -1)
        // {
        //     buffer[n] = '\n';

        //     messages[msg_count].size = n;
        //     memcpy(&messages[msg_count].buffer, buffer, n * sizeof(char));

        //     printf("%d %s\n", msg_count, messages[msg_count].buffer.topic);

        //     msg_count++;
        // }

        // if (tcp_clients_count > 0)
        // {
        //     for (int i = 0; i < msg_count; i++)
        //     {
        //         printf("size = %d\n", messages[i].size);
        //         printf("size = %ld\n", sizeof(messages[i]));

        //         // char *buf = calloc(2000, sizeof(char));

        //         // memcpy(buf, &messages[i], sizeof(messages[i]));

        //         // memcpy(buffer, &messages[i].size, sizeof(int));
        //         // memcpy(buffer + 4, &messages[i].buffer, messages[i].size * sizeof(char));

        //         if (send(client_sock, &messages[i].size, 4, 0) < 0)
        //         {
        //             perror("[SERV] Can't send\n");
        //             exit(EXIT_FAILURE);
        //         }

        //         if (send(client_sock, &messages[i].buffer, messages[i].size, 0) < 0)
        //         {
        //             perror("[SERV] Can't send\n");
        //             exit(EXIT_FAILURE);
        //         }
        //     }

        //     msg_count = 0;
        // }
    }

    return 0;
}
