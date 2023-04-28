#include "csapp.h"
#include "http_parser.h"
#include <pthread.h>

#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>

#define HOSTLEN 256
#define SERVLEN 8

/*From tiny.c implementation:*/
/* Typedef for convenience */
typedef struct sockaddr SA;

/* Information about a connected client. */
typedef struct {
    struct sockaddr_in addr; // Socket address
    socklen_t addrlen;       // Socket address length
    int connfd;              // Client connection file descriptor
    char host[HOSTLEN];      // Client host
    char serv[SERVLEN];      // Client service (port)
} client_info;

/* URI parsing results. */
typedef enum { PARSE_ERROR, PARSE_STATIC, PARSE_DYNAMIC } parse_result;

/*
 * String to use for the User-Agent header.
 * Don't forget to terminate with \r\n
 */
static const char *header_user_agent = "Mozilla/5.0"
                                       " (X11; Linux x86_64; rv:3.10.0)"
                                       " Gecko/20230411 Firefox/63.0.1";

/*
 * clienterror - returns an error message to the client
 */
void clienterror(int fd, const char *errnum, const char *shortmsg,
                 const char *longmsg) {
    char buf[MAXLINE];
    char body[MAXBUF];
    size_t buflen;
    size_t bodylen;

    /* Build the HTTP response body */
    bodylen = snprintf(body, MAXBUF,
                       "<!DOCTYPE html>\r\n"
                       "<html>\r\n"
                       "<head><title>Tiny Error</title></head>\r\n"
                       "<body bgcolor=\"ffffff\">\r\n"
                       "<h1>%s: %s</h1>\r\n"
                       "<p>%s</p>\r\n"
                       "<hr /><em>The Tiny Web server</em>\r\n"
                       "</body></html>\r\n",
                       errnum, shortmsg, longmsg);
    if (bodylen >= MAXBUF) {
        return; // Overflow!
    }

    /* Build the HTTP response headers */
    buflen = snprintf(buf, MAXLINE,
                      "HTTP/1.0 %s %s\r\n"
                      "Content-Type: text/html\r\n"
                      "Content-Length: %zu\r\n\r\n",
                      errnum, shortmsg, bodylen);
    if (buflen >= MAXLINE) {
        return; // Overflow!
    }

    /* Write the headers */
    if (rio_writen(fd, buf, buflen) < 0) {
        fprintf(stderr, "Error writing error response headers to client\n");
        return;
    }

    /* Write the body */
    if (rio_writen(fd, body, bodylen) < 0) {
        fprintf(stderr, "Error writing error response body to client\n");
        return;
    }
}

bool read_requesthdrs(client_info *client, rio_t *rp, parser_t *parser) {
    char buf[MAXLINE];

    while (true) {
        if (rio_readlineb(rp, buf, sizeof(buf)) <= 0) {
            return true;
        }

        /* Check for end of request headers */
        if (strcmp(buf, "\r\n") == 0) {
            return false;
        }

        /* Parse header into name and value */
        parser_state ps = parser_parse_line(parser, buf);
        if (ps != HEADER) {
            /* Error parsing header */
            clienterror(client->connfd, "400", "Bad Request",
                        "Tiny could not parse request headers");
            return true;
        }
    }
}

void serve(client_info *client) {

    rio_t rio;
    parser_t *parser;
    char buf[MAXLINE];

    rio_readinitb(&rio, client->connfd);

    if (rio_readlineb(&rio, buf, sizeof(buf)) <= 0) {
        return;
    }

    parser = parser_new(); // remember to free
    parser_parse_line(parser, buf);

    const char *host;
    const char *port;
    const char *path;
    const char *method;
    const char *version;

    if ((parser_retrieve(parser, HOST, &host) < 0) ||
        (parser_retrieve(parser, PORT, &port) < 0) ||
        (parser_retrieve(parser, PATH, &path) < 0) ||
        (parser_retrieve(parser, METHOD, &method) < 0) ||
        (parser_retrieve(parser, HTTP_VERSION, &version) < 0)) {
        parser_free(parser);
        return;
    }

    // Error's from Tiny.c(serve)
    if ((strcmp("1.0", version) != 0) && (strcmp("1.1", version) != 0)) {
        clienterror(client->connfd, "400", "Bad Request",
                    "Server received malformed request");
        return;
    }

    if (strcmp("GET", method) != 0) {
        clienterror(client->connfd, "501", "Not Implemented",
                    "Proxy does not implmement this method");
        return;
    }

    int client_fd = open_clientfd(host, port);
    if (client_fd < 0) {
        parser_free(parser);
        return;
    }
    // int n;
    rio_t ser;
    rio_readinitb(&ser, client_fd);
    char bufPar[MAXLINE];

    sprintf(bufPar, "%s %s HTTP/1.0\r\n", method, path);
    rio_writen(client_fd, bufPar, strlen(bufPar));

    if (read_requesthdrs(client, &rio, parser)) {
        parser_free(parser);
        return;
    }

    header_t *header;

    // Forwarding headers
    while ((header = parser_retrieve_next_header(parser)) != NULL) {
        int rioValid = 0;
        char bufHeader[MAXLINE];
        if (strcmp("User-Agent", header->name) == 0) {
            header->value = header_user_agent;
            sprintf(bufHeader, "%s: %s\r\n", header->name, header->value);
            rioValid++;
        } else if (strcmp("Host", header->name) == 0) {
            char bufHost[MAXLINE];
            sprintf(bufHost, "%s:%s", host, port);
            sprintf(bufHeader, "Host: %s\r\n", bufHost);
            rioValid++;
        } else if (strcmp("Connection", header->name) == 0) {
            sprintf(bufHeader, "%s", "Connection: close\r\n");
            rioValid++;
        } else if (strcmp("Proxy-Connection", header->name) == 0) {
            sprintf(bufHeader, "%s", "Proxy-Connection: close\r\n");
            rioValid++;
        } else {
            sprintf(bufHeader, "%s: %s\r\n", header->name, header->value);
            rioValid++;
        }

        if (rioValid != 0)
            rio_writen(client_fd, bufHeader, strlen(bufHeader));
    }
    // server termination
    rio_writen(client_fd, "\r\n", MAXLINE);
    char bufTerm[MAXLINE];
    int r;
    while ((r = rio_readnb(&ser, bufTerm, MAXLINE)) > 0) {
        rio_writen(client->connfd, bufTerm, r);
    }

    return;
}

void *thread(void *vargp) {
    // int connfd = *((int*)(vargp));
    client_info *client = (client_info *)(vargp);

    pthread_detach(pthread_self());
    // Free(vargp);
    int clientfd = client->connfd;
    serve(client);
    close(clientfd);
}

int main(int argc, char **argv) {
    int listenfd;
    pthread_t tid;

    /*From Tiny.c*/
    /* Check command line args */
    if (argc != 2) {
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        exit(1);
    }

    // Open listening file descriptor
    listenfd = open_listenfd(argv[1]);
    if (listenfd < 0) {
        fprintf(stderr, "Failed to listen on port: %s\n", argv[1]);
        exit(1);
    }
    signal(SIGPIPE, SIG_IGN);
    while (1) {
        /* Allocate space on the stack for client info */

        // pthread_t tid;

        /* Initialize the length of the address */

        client_info *client = Malloc(sizeof(client_info));
        client->addrlen = sizeof(client->addr);

        /* accept() will block until a client connects to the port */

        // Threading:

        client->connfd =
            accept(listenfd, (SA *)&client->addr, &client->addrlen);

        // if (client->connfd < 0) {
        if (client->connfd < 0) {
            perror("accept");
            continue;
        }

        /* Connection is established; serve client */
        // serve(client);
        // close(client->connfd);
        // Threading:
        // Pthread_create(&tid, NULL, serve, connfdp);
        pthread_create(&tid, NULL, thread, client);
    }
}
