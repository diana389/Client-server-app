#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <math.h>
#include <poll.h>

#define MAXLINE 2000

struct sockaddr_in cliaddr;

struct pollfd pfds[MAXLINE];
int nfds = 0;

typedef struct msg
{
    char topic[50];
    unsigned int type;
    int payload_int;              // type 0
    float payload_short_real;     // type 1
    float payload_float;          // type 2
    char payload_string[MAXLINE]; // type 3

} msg;

typedef struct mesg
{
    int size;
    struct sockaddr_in cliaddr;
    char topic[50];
    char content[MAXLINE];
} mesg;

int msg_count = 1;

void complete_message(int n, char *buffer, msg *message)
{
    memcpy(message->topic, buffer, 50 * sizeof(char));
    memcpy(&message->type, buffer + 50, sizeof(char));

    // unsigned int type = 0;
    // memcpy(&type, buffer + 50, sizeof(char));

    printf("\n%d Client %d: %s --- %d --- \n", msg_count++, n, message->topic, message->type);
    // printf("\n%d Client %d: %s --- %d --- \n", msg_count++, n, message->topic, type);

    // if (type == 0)
    // {
    //     uint8_t sign = 0;
    //     memcpy(&sign, buffer + 51, sizeof(char));
    //     memcpy(&message->payload_int, buffer + 52, sizeof(int));

    //     message->payload_int = ntohl(message->payload_int);

    //     if (sign) // negativ
    //         message->payload_int = -message->payload_int;

    //     printf("%d\n", message->payload_int);
    // }

    // if (type == 1)
    // {
    //     uint16_t num = 0;
    //     memcpy(&num, buffer + 51, sizeof(uint16_t));

    //     message->payload_short_real = ntohs(num) / 100.00;
    //     printf("%.4f\n", message->payload_short_real);
    // }

    // if (type == 2)
    // {
    //     uint8_t sign = 0, power = 0;
    //     uint32_t num = 0;
    //     memcpy(&sign, buffer + 51, sizeof(char));
    //     memcpy(&num, buffer + 52, sizeof(int));
    //     memcpy(&power, buffer + 56, sizeof(char));

    //     message->payload_float = ntohl(num);
    //     message->payload_float = message->payload_float / pow(10.0, power);

    //     if (sign) // negativ
    //         message->payload_float = -message->payload_float;

    //     printf("%.4f\n", message->payload_float);
    // }

    // if (type == 3)
    // {
    //     memcpy(message->payload_string, buffer + 51, (n - 50) * sizeof(char));
    //     printf("%s\n", message->payload_string);
    // }
}

int main(int argc, char const *argv[])
{
    char id_client[50], ip_server[20];
    int PORT = atoi(argv[3]);

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

    // printf("[CLIENT] Socket created successfully\n");

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
    // printf("[CLIENT] Connected with server successfully\n");

    /* Send the message to server */
    if (send(socket_desc, id_client, strlen(id_client), 0) < 0)
    {
        perror("[CLIENT] Unable to send message\n");
        exit(EXIT_FAILURE);
    }

    pfds[nfds].fd = socket_desc;
    pfds[nfds].events = POLLIN;
    nfds++;

    // printf("[CLIENT] Server's response: ");
    int count = 1;

    while (1)
    {
        poll(pfds, nfds, -1);

        memset(server_message, 0, sizeof(server_message));

        if ((pfds[0].revents & POLLIN) != 0)
        {

            int n = recv(socket_desc, server_message, 2072, 0);

            if (n < 0)
            {
                perror("[CLIENT] Error while receiving server's msg\n");
                exit(EXIT_FAILURE);
            }

            if (strcmp(server_message, "exit\0") == 0)
            {
                printf("[CLIENT] Exit message\n");
                // free(server_message);
                close(socket_desc);
                return 0;
            }

            int size;
            char *aux = server_message;

            // for (int i = 0; i < 150; i++)
            //     printf("(%d %d) ", i, server_message[i]);

            // printf("\n\nn = %d\n", n);
            memcpy(&size, aux, sizeof(int));

            aux += sizeof(int);

            memcpy(&cliaddr, aux, sizeof(cliaddr));

            aux += sizeof(cliaddr);
            aux += 50;

            //     printf("n = %d\n", n);
            // printf("%d size = %d\n", count++, size);
            // printf("msg = %s\n", aux);

            msg message;
            complete_message(size, aux, &message);

            aux += size;
        }
    }

    return 0;
}
