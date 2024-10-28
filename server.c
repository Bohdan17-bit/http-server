#define _GNU_SOURCE
#include <string.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <ifaddrs.h>

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <pthread.h>

#include <sys/socket.h>
#include <sys/types.h>
#include <netdb.h>
#include <errno.h>

#define MAX_REQUEST_SIZE 2047
#define SOCKET int
#define ISVALIDSOCKET(s) ((s) >= 0)
#define CLOSESOCKET(s) close(s)
#define GETSOCKETERNO() (errno)

const char *get_content_type(const char *path) {
    const char *last_dot = strrchr(path, '.');
    if(last_dot) {
        if(strcmp(last_dot, ".css") == 0) return "text/css";
        if(strcmp(last_dot, ".csv") == 0) return "text/csv";
        if(strcmp(last_dot, ".htm") == 0) return "text/html";
        if(strcmp(last_dot, ".html") == 0) return "text/html";

        if(strcmp(last_dot, ".gif") == 0) return "image/gif";
        if(strcmp(last_dot, ".ico") == 0) return "image/x-icon";
        if(strcmp(last_dot, ".jpeg") == 0) return "image/jpeg";
        if(strcmp(last_dot, ".jpg") == 0) return "image/jpeg";
        if(strcmp(last_dot, ".png") == 0) return "image/png";
        if(strcmp(last_dot, ".svg") == 0) return "image/svg+xml";

        if(strcmp(last_dot, ".js") == 0) return "application/javascript";
        if(strcmp(last_dot, ".json") == 0) return "application/json";
        if(strcmp(last_dot, ".pdf") == 0) return "application/pdf";

        if(strcmp(last_dot, ".txt") == 0) return "text/plain";
    }

    return "application/octet-stream";
}

struct client_info {
    socklen_t address_length;
    struct sockaddr_storage address;
    SOCKET socket;
    char request[MAX_REQUEST_SIZE+1];
    int received;
    struct client_info *next;
};

static struct client_info *clients = 0;

SOCKET create_socket(const char *host, const char *port) {
    printf("Configuring local address...\n");
    struct addrinfo hints;
    memset(&hints, 0, sizeof(struct addrinfo));

    hints.ai_family = AF_INET;       // Використовується IPv4
    hints.ai_socktype = SOCK_STREAM; // TCP сокет
    hints.ai_flags = 0;              // Не використовуємо AI_PASSIVE, щоб прив'язатися до конкретного IP

    struct addrinfo *bind_address;
    if (getaddrinfo(host, port, &hints, &bind_address) != 0) {
        fprintf(stderr, "getaddrinfo() failed (%d)\n", GETSOCKETERNO());
        exit(1);
    }

    printf("Creating socket...\n");
    SOCKET socket_listen = socket(bind_address->ai_family, bind_address->ai_socktype, bind_address->ai_protocol);

    if (!ISVALIDSOCKET(socket_listen)) {
        fprintf(stderr, "socket() failed (%d)!\n", GETSOCKETERNO());
        freeaddrinfo(bind_address);
        exit(1);
    }

    printf("Binding socket to address %s...\n", host);
    if (bind(socket_listen, bind_address->ai_addr, bind_address->ai_addrlen) != 0) {
        fprintf(stderr, "bind() failed (%d)\n", GETSOCKETERNO());
        freeaddrinfo(bind_address);
        exit(1);
    }

    freeaddrinfo(bind_address);

    printf("Listening...\n");
    if (listen(socket_listen, 10) < 0) {
        fprintf(stderr, "listen() failed (%d)\n", GETSOCKETERNO());
        exit(1);
    }

    return socket_listen;
}

struct client_info *get_client(SOCKET s) {
    struct client_info *client_ptr = clients;
    while(client_ptr) {
        if(client_ptr->socket == s) {
            break;
        }
        client_ptr = client_ptr->next;
    }
    if(client_ptr) {
        return client_ptr;
    }
    struct client_info *n = (struct client_info*) calloc(1, sizeof(struct client_info));
    if(!n) {
        fprintf(stderr, "Out of memory!\n");
        exit(1);
    }
    n->address_length = sizeof(n->address);
    n->next = clients;
    clients = n;
    return n;
}

void drop_client(struct client_info *client) {
    CLOSESOCKET(client->socket);
    struct client_info **ptr = &clients;
    while(*ptr) {
        if(*ptr == client) {
            *ptr = client->next;
            free(client);
            return;
        }
        ptr = &(*ptr)->next;
    }
    fprintf(stderr, "drop client not found!\n");
    exit(1);
}

const char *get_client_address(struct client_info *ci_ptr) {
    static char address_buffer[100];
    getnameinfo((struct sockaddr*)&ci_ptr->address, ci_ptr->address_length, address_buffer,
    sizeof(address_buffer), 0, 0, NI_NUMERICHOST);
    return address_buffer;
}

fd_set wait_on_clients(SOCKET server) {
    fd_set reads;
    FD_ZERO(&reads);
    FD_SET(server, &reads);
    SOCKET max_socket = server;
    struct client_info *ci_ptr = clients;
    while(ci_ptr) {
        FD_SET(ci_ptr->socket, &reads);
        if(ci_ptr->socket > max_socket) {
            max_socket = ci_ptr->socket;
        }
        ci_ptr = ci_ptr->next;
    }

    if (select(max_socket+1, &reads, 0, 0, 0) < 0) {
        fprintf(stderr, "select() failed! (%d)\n", GETSOCKETERNO());
        exit(1);
    }

    return reads;
}

void send_400(struct client_info *client) {
    const char *c400 = "HTTP/1.1 400 Bad Request\r\n"
                        "Connection: close\r\n"
                        "Content-Length: 11\r\n\r\nBad request";
    send(client->socket, c400, strlen(c400), 0);
    drop_client(client);
}

void send_404(struct client_info *client) {
    const char *c404 = "HTTP/1.1 404 Not Found\r\n"
                        "Connection: close\r\n"
                        "Content-Length: 9\r\n\r\nNot Found";
    send(client->socket, c404, strlen(c404), 0);
    drop_client(client);
}

void send_200(struct client_info *client) {
    const char *body = "{status: success, data: Дані отримано}\r\n";
    int content_length = strlen(body);

    char response[1024];
    snprintf(response, sizeof(response),
             "HTTP/1.1 200 OK\r\n"
             "Content-Type: application/json\r\n"
             "Content-Length: %d\r\n"
             "Connection: keep-alive\r\n\r\n"
             "%s", content_length, body);

    send(client->socket, response, strlen(response), 0);
}


void serve_resource(struct client_info *client, const char *path) {
    printf("serve_resource %s %s\n", get_client_address(client), path);
    if(strcmp(path, "/") == 0) {
        path = "/index.html";
    }
    if(strcmp(path, "/") == 0) path = "/index.html";
    if(strlen(path) > 100) {
        send_400(client);
        return;
    }
    if(strstr(path, "..")) {
        send_404(client);
        return;
    }
    char full_path[128];
    sprintf(full_path, "public%s", path);
    FILE *fp = fopen(full_path, "rb");
    if(!fp) {
        send_404(client);
        return 0;
    }
    fseek(fp, 0L, SEEK_END);
    size_t cl = ftell(fp);
    rewind(fp);
    const char *ct = get_content_type(full_path);
    
    #define BUFSIZE 1024

    char buffer[BUFSIZ];

    sprintf(buffer, "HTTP/1.1 200 OK\r\n");
    send(client->socket, buffer, strlen(buffer), 0);

    sprintf(buffer, "Connection: close\r\n");
    send(client->socket, buffer, strlen(buffer), 0);

    sprintf(buffer, "Content-Length: %u\r\n", cl);
    send(client->socket, buffer, strlen(buffer), 0);    

    sprintf(buffer, "Content-Type: %s\r\n", ct);
    send(client->socket, buffer, strlen(buffer), 0);

    sprintf(buffer, "\r\n");
    send(client->socket, buffer, strlen(buffer), 0);

    int read = fread(buffer, 1, BUFSIZE, fp);
    while(read) {
        send(client->socket, buffer, read, 0);
        read = fread(buffer, 1, BUFSIZE, fp);
    }

    fclose(fp);
    drop_client(client);
}

int main() {
    SOCKET server = create_socket("192.168.0.100", "8080");
    while(1) {
        fd_set reads;
        reads = wait_on_clients(server);

        if(FD_ISSET(server, &reads)) {
            struct client_info *client = get_client(-1);

            client->socket = accept(server, (struct sockaddr*) &(client->address),
            &(client->address_length));

            if(!ISVALIDSOCKET(client->socket)) {
                fprintf(stderr, "accept() failed, (%d)\n", GETSOCKETERNO());
                return 1;
            }

            printf("New connection from %s.\n", get_client_address(client));
        }

        struct client_info *client = clients;
        while(client) {
            struct client_info *next = client->next;

            if(FD_ISSET(client->socket, &reads)) {
                if (MAX_REQUEST_SIZE == client->received) {
                    send_400(client);
                    client = next;
                    continue;
                }

                int response_size = recv(client->socket, client->request + client->received,
                            MAX_REQUEST_SIZE - client->received, 0);

                if(response_size < 1) {
                    printf("Unexpected disconnect from %s.\n", get_client_address(client));
                    drop_client(client);
                } 
                
                else {
                    client->received += response_size;
                    client->request[client->received] = 0;

                    char *q = strstr(client->request, "\r\n\r\n");
                    if(q) {
                        *q = 0;

                        if(strncmp("GET /", client->request, 5) == 0) {

                            char *path = client->request + 4;
                            char *end_path = strstr(path, " ");
                            if(!end_path) {
                                send_400(client);
                            } else {
                                *end_path = 0;
                                serve_resource(client, path);
                            }

                        } 
                        
                       else if (strncmp("POST /", client->request, 6) == 0) {
                            printf("%s", client->request);  // Виведення запиту для дебагу

                            // Визначення Content-Length
                            char *content_length_str = strstr(client->request, "Content-Length: ");
                            int content_length = 0;
                            if (content_length_str) {
                                content_length = atoi(content_length_str + 16);
                            } 
                            else {
                                printf("Content-Length not found in POST request\n");
                                send_400(client);
                                client = next;
                                continue;
                            }

                            // Пошук кінця заголовків
                            char *header_end = memchr(client->request, '\0', client->received);
                            if (!header_end) {
                                printf("Headers not properly terminated in POST request\n");
                                send_400(client);
                                client = next;
                                continue;
                            }

                            // Довжина заголовків
                            int headers_length = header_end - client->request + 4; // +4 для включення \r\n\r\n

                            // Перевірка на повний запит
                            if (client->received < headers_length + content_length) {
                                printf("Incomplete POST request received\n");
                                continue;
                            }

                            // Отримуємо тіло
                            char *body = client->request + headers_length; // Тіло запиту починається після заголовків

                            // Переконуємося, що body закінчується правильно
                            body[content_length] = '\0'; // Завершуємо рядок нульовим символом

                            printf("Received POST request with headers:\n%s\nBody:\n%s\n", client->request, body);

                            // Відправка відповіді
                            send_200(client);
                        }

                     else {
                            send_400(client);
                        }
                    }
                }
            }
            client = next;
        }
    }

    printf("\nClosing socket...\n");
    CLOSESOCKET(server);

    printf("Finished.\n");
    return 0;
}