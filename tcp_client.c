#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <math.h>
#include <poll.h>
#include <netinet/tcp.h>

#define MAXLINE 2000

struct sockaddr_in cliaddr;
int n, size_to_be_received = 0;

struct pollfd pfds[MAXLINE];
int nfds = 0;

typedef struct msg
{
    int size;                   // size of content
    struct sockaddr_in cliaddr; // udp client structure
    char topic[51];
    char content[MAXLINE]; // content received from UDP
} msg;

void complete_message(msg *message)
{
    char *buffer = message->content; // content received from UDP
    unsigned int type = 0;
    memcpy(&type, buffer + 50, sizeof(char)); // message type

    // print sender info
    printf("%s:%i - ", inet_ntoa(message->cliaddr.sin_addr), ntohs(message->cliaddr.sin_port));
    // print topic
    printf("%s - ", message->topic);

    if (type == 0) // int
    {
        int payload_int;
        uint8_t sign = 0;
        memcpy(&sign, buffer + 51, sizeof(char));
        memcpy(&payload_int, buffer + 52, sizeof(int));

        payload_int = ntohl(payload_int);

        if (sign) // negativ
            payload_int = -payload_int;

        printf("INT - %d\n", payload_int);
    }

    if (type == 1) // short real
    {
        float payload_short_real;
        uint16_t num = 0;
        memcpy(&num, buffer + 51, sizeof(uint16_t));

        payload_short_real = ntohs(num) / 100.00;
        printf("SHORT_REAL - %.4f\n", payload_short_real);
    }

    if (type == 2) // float
    {
        float payload_float;
        uint8_t sign = 0, power = 0;
        uint32_t num = 0;
        memcpy(&sign, buffer + 51, sizeof(char));
        memcpy(&num, buffer + 52, sizeof(int));
        memcpy(&power, buffer + 56, sizeof(char));

        payload_float = ntohl(num);
        payload_float = payload_float / pow(10.0, power);

        if (sign) // negativ
            payload_float = -payload_float;

        printf("FLOAT - %.4f\n", payload_float);
    }

    if (type == 3) // string
    {
        char payload_string[MAXLINE];
        memcpy(payload_string, buffer + 51, (message->size - 50) * sizeof(char));
        printf("STRING - %s\n", payload_string);
    }
}

int main(int argc, char const *argv[])
{
    char id_client[50], ip_server[20];
    int PORT;
    sscanf(argv[3], "%d", &PORT);

    strcpy(id_client, argv[1]);
    strcpy(ip_server, argv[2]);

    setvbuf(stdout, NULL, _IONBF, BUFSIZ);

    int socket_desc;
    struct sockaddr_in server_addr;
    char server_message[3000];

    /* Clean buffers and structures*/
    memset(&server_addr, 0, sizeof(server_addr));

    /* Create socket, we use SOCK_STREAM for TCP */
    socket_desc = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_desc < 0)
    {
        perror("[CLIENT] Unable to create socket\n");
        exit(EXIT_FAILURE);
    }

    // Disable Nagle algorithm
    int flag = 1;
    if (setsockopt(socket_desc, IPPROTO_TCP, TCP_NODELAY, (char *)&flag, sizeof(int)) != 0)
    {
        perror("Error TCP_NODELAY\n");
    }

    /* Set port and IP the same as server-side */
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    server_addr.sin_addr.s_addr = inet_addr(ip_server);

    /* Send connection request to server */
    if (connect(socket_desc, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        perror("[CLIENT] Unable to connect\n");
        exit(EXIT_FAILURE);
    }

    /* Send the message to server */
    if (send(socket_desc, id_client, strlen(id_client), 0) < 0)
    {
        perror("[CLIENT] Unable to send message\n");
        exit(EXIT_FAILURE);
    }

    /* stdin */
    pfds[nfds].fd = STDIN_FILENO;
    pfds[nfds].events = POLLIN;
    nfds++;

    /* tcp socket*/
    pfds[nfds].fd = socket_desc;
    pfds[nfds].events = POLLIN;
    nfds++;

    char input[100];

    while (1)
    {
        poll(pfds, nfds, -1);

        memset(server_message, 0, sizeof(server_message));
        memset(input, 0, sizeof(input));

        size_to_be_received = 0;

        if ((pfds[0].revents & POLLIN) != 0) // stdin
        {
            if (fgets(input, 100, stdin))
            {
                if (strcmp(input, "exit\n") == 0) // close socket
                {
                    // perror("[CLIENT] Exit message\n");
                    close(socket_desc);
                    return 0;
                }

                /* Send the message to server */
                if (send(socket_desc, input, 100, 0) < 0)
                {
                    perror("[CLIENT] Unable to send message\n");
                    exit(EXIT_FAILURE);
                }

                char *p = strtok(input, " ");

                // print subscribing info
                if (p != NULL && strcmp(p, "subscribe") == 0)
                {
                    p = strtok(NULL, " \n");
                    printf("Subscribed to topic.\n");
                }
                else if (p != NULL && strcmp(p, "unsubscribe") == 0)
                {
                    p = strtok(NULL, " \n");
                    printf("Unsubscribed from topic.\n");
                }
            }
        }
        else if ((pfds[1].revents & POLLIN) != 0) // server
        {
            n = recv(socket_desc, &size_to_be_received, sizeof(int), 0);

            if (n < 0)
            {
                perror("[CLIENT] Error while receiving server's msg\n");
                exit(EXIT_FAILURE);
            }

            n = recv(socket_desc, server_message, size_to_be_received, 0);

            if (n < 0)
            {
                perror("[CLIENT] Error while receiving server's msg\n");
                exit(EXIT_FAILURE);
            }

            if (strcmp(server_message, "exit\0") == 0) // server closing
            {
                close(socket_desc);
                return 0;
            }

            // parse message
            msg *message = (msg *)server_message;
            complete_message(message);
        }
    }

    return 0;
}
