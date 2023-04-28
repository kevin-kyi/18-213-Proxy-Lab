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

// URI Cache implementation:
#include "cache.h"

pthread_mutex_t mutex;
cache_t *cache;

#define HOSTLEN 256
#define SERVLEN 8
#define MAX_OBJECT_SIZE (100 * 1024)
#define MAX_CACHE_SIZE (1024 * 1024)

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

bool read_requesthdrs(int connfd, rio_t *rp, parser_t *parser) {
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
            clienterror(connfd, "400", "Bad Request",
                        "Tiny could not parse request headers");
            return true;
        }
    }
}

// void serve(client_info *client) {
void serve(int connfd) {

    rio_t rio;
    parser_t *parser;
    char buf[MAXLINE];

    rio_readinitb(&rio, connfd);

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
    const char *uri;
    parser_retrieve(parser, URI, &uri);

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
        clienterror(connfd, "400", "Bad Request",
                    "Server received malformed request");
        return;
    }

    if (strcmp("GET", method) != 0) {
        clienterror(connfd, "501", "Not Implemented",
                    "Proxy does not implmement this method");
        return;
    }

    // Cache implementation:
    pthread_mutex_lock(&mutex);
    block_t *block = find_key(uri, cache);
    pthread_mutex_unlock(&mutex);

    if (block != NULL) {
        pthread_mutex_lock(&mutex);
        block->refCount += 1;
        pthread_mutex_unlock(&mutex);

        rio_writen(connfd, block->data, block->blockSize);

        pthread_mutex_lock(&mutex);
        block->refCount -= 1;
        pthread_mutex_unlock(&mutex);

        pthread_mutex_lock(&mutex);
        update_LRU(cache, block);
        pthread_mutex_unlock(&mutex);

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

    if (read_requesthdrs(connfd, &rio, parser)) {
        parser_free(parser);
        return;
    }

    header_t *header;

    // Forwarding headers
    char bufHeader[MAXLINE];
    while ((header = parser_retrieve_next_header(parser)) != NULL) {
        int rioValid = 0;
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

    snprintf(bufHeader, MAXLINE, "\r\n");
    rio_writen(client_fd, bufHeader, strlen(bufHeader));

    // server termination
    rio_writen(client_fd, "\r\n", MAXLINE);
    // char bufTerm[MAXLINE];
    char bufTerm[MAXLINE];
    size_t numBytes;
    size_t totalBytes = 0;

    char rBuf[MAX_CACHE_SIZE];

    while ((numBytes = rio_readnb(&ser, bufTerm, MAXLINE)) > 0) {
        rio_writen(connfd, bufTerm, numBytes);
        totalBytes += numBytes;
        if (totalBytes <= MAX_OBJECT_SIZE) {
            memcpy(rBuf + totalBytes - numBytes, bufTerm, numBytes);
        }
    }
    if (totalBytes <= MAX_OBJECT_SIZE) {
        pthread_mutex_lock(&mutex);
        insert_block(cache, totalBytes, uri, rBuf);
        pthread_mutex_unlock(&mutex);
    }

    return;
}

void *thread(void *vargp) {
    int connfd = *((int *)vargp);
    pthread_detach(pthread_self());
    free(vargp);
    serve(connfd);
    close(connfd);
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
    cache = init_cache();
    pthread_mutex_init(&mutex, NULL);
    signal(SIGPIPE, SIG_IGN);
    while (1) {

        int *connfdp;
        client_info client_data;
        client_info *client = &client_data;
        client->addrlen = sizeof(client->addr);
        connfdp = Malloc(sizeof(int));
        *connfdp = accept(listenfd, (SA *)&client->addr, &client->addrlen);

        // if (client->connfd < 0) {
        if (client->connfd < 0) {
            perror("accept");
            continue;
        }

        /* Connection Thread is established; serve client */
        pthread_create(&tid, NULL, thread, connfdp);
    }
    return 0;
}
