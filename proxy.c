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
static const char *header_connection = "Connection close\r\n";
static const char *header_proxy = "Proxy-Connection: close\r\n";

void print_cache(cache_t *c) {
    sio_printf("*****************PRINTING CACHE********************\n");
    for (block_t *n = c->head; n != NULL; n = n->next) {
        sio_printf("ADDRESS: %p\n", n);
        sio_printf("REF CNT: %ld\n", n->refCount);
        sio_printf("URL: %s\n", n->key);

        sio_printf("******************************************\n");
    }
    sio_printf("************END PRINT****************\n");
}

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
            return false;
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
            return false;
        }
    }
    return false;
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
    parser_state pState = parser_parse_line(parser, buf);

    if (pState != REQUEST) {
        parser_free(parser);
        clienterror(connfd, "400", "Bad Request",
                    "Server received malformed request");
        return;
    }

    const char *host;
    const char *port;
    const char *path;
    const char *method;
    const char *uri;
    parser_retrieve(parser, HOST, &host);
    parser_retrieve(parser, PORT, &port);
    parser_retrieve(parser, PATH, &path);
    parser_retrieve(parser, METHOD, &method);
    parser_retrieve(parser, URI, &uri);

    // Error's from Tiny.c(serve)

    if (strcmp("GET", method) != 0) {
        parser_free(parser);
        clienterror(connfd, "501", "Not Implemented",
                    "Proxy does not implmement this method");
        return;
    }

    if (read_requesthdrs(connfd, &rio, parser)) {
        parser_free(parser);
        return;
    }

    // Cache implementation:
    // char *line = malloc(strlen(uri) + 1);
    // memcpy(line, uri, strlen(uri) + 1);

    // pthread_mutex_lock(&mutex);
    // block_t *block = find_key(line, cache);
    // pthread_mutex_unlock(&mutex);

    // if(block != NULL){
    //     sio_printf("Found!\n");
    //     pthread_mutex_lock(&mutex);
    //     block->refCount += 1;
    //     pthread_mutex_unlock(&mutex);

    //     if (rio_writen(connfd, block->data, block->blockSize) < 0){
    //         fprintf(stderr, "Error: client response");
    //         return;
    //     }

    //     pthread_mutex_lock(&mutex);
    //     block->refCount --;
    //     if ((block->refCount == 0) && !find_key(block->key, cache)){
    //         free(block->data);
    //         free(block->key);
    //         free(block);
    //         pthread_mutex_unlock(&mutex);
    //         parser_free(parser);

    //     }
    //     pthread_mutex_unlock(&mutex);

    //     pthread_mutex_lock(&mutex);
    //     update_LRU(cache, block);
    //     pthread_mutex_unlock(&mutex);

    //     parser_free(parser);
    //     return;
    // }
    // sio_printf("NOT FOUND! \n");

    int client_fd = open_clientfd(host, port);
    if (client_fd < 0) {
        fprintf(stderr, "Could not connect to host: %s\n", host);
        parser_free(parser);
        close(client_fd);
        return;
    }

    // char bufHeader[MAXLINE];
    // char *version = "HTTP/1.0\r\n";
    // size_t size = snprintf(bufHeader, MAXLINE, "%s %s %s", method, path,
    // version);

    // if (rio_writen(client_fd, bufHeader, size) < 0){
    //     fprintf(stderr, "Error writing response headers\n");
    //     return;
    // }

    // header_t *header = parser_lookup_header(parser, "Host");
    // if (header != NULL) {
    //     host = header->value;
    //     size =
    //         snprintf(bufHeader, MAXLINE,
    //                 "Host: %s\r\n"
    //                 "User-Agent: %s\r\n"
    //                 "%s"
    //                 "%s",
    //                 host, header_user_agent, header_connection,
    //                 header_proxy);
    // } else {
    //     size = snprintf(bufHeader, MAXLINE,
    //                     "Host: %s:%s\r\n"
    //                     "User-Agent: %s\r\n"
    //                     "%s"
    //                     "%s",
    //                     host, port, header_user_agent, header_connection,
    //                     header_proxy);
    // }

    // if (rio_writen(client_fd, bufHeader, size) < 0) {
    //     fprintf(stderr, "Error writing response headers\n");
    //     return;
    // }
    // //Forwarding headers
    // header = parser_retrieve_next_header(parser);
    // while (header != NULL) {
    //     if ((strcmp(header->name, "Host") != 0) &&
    //         (strcmp(header->name, "User-Agent") != 0) &&
    //         (strcmp(header->name, "Connection") != 0) &&
    //         (strcmp(header->name, "Proxy-Connection") != 0)){
    //         size = snprintf(bufHeader, MAXLINE, "%s: %s\r\n", header->name,
    //         header->value);

    //         if (rio_writen(client_fd, bufHeader, size) < 0){
    //             fprintf(stderr, "Error writing response headers\n");
    //             return;
    //         }
    //     }
    //     header = parser_retrieve_next_header(parser);

    // }

    // snprintf(bufHeader, MAXLINE, "\r\n");

    if (rio_writen(client_fd, "\r\n", MAXLINE) < 0) {
        fprintf(stderr, "Error writing response headers\n");
        return;
    }

    rio_t ser;
    rio_readinitb(&ser, client_fd);

    // server termination
    size_t numBytes;
    // size_t totalBytes = 0;
    // bool addFlag = 1;
    char bufTerm[MAXLINE];
    // char rBuf[MAX_OBJECT_SIZE];

    while ((numBytes = rio_readnb(&ser, bufTerm, MAXLINE)) != 0) {
        rio_writen(connfd, bufTerm, numBytes);

        // if (totalBytes + numBytes <= MAX_OBJECT_SIZE) {
        //     totalBytes += numBytes;
        //     memcpy(rBuf + totalBytes - numBytes, bufTerm, numBytes);
        // } else{
        //     addFlag = 0;
        // }
    }
    // if (addFlag) {
    //     pthread_mutex_lock(&mutex);
    //     char *data = malloc(totalBytes);
    //     memcpy(data, rBuf, totalBytes);
    //     char *key = malloc(strlen(uri) + 1);
    //     memcpy(key, uri, strlen(uri) + 1);

    //     insert_block(cache, totalBytes, key, data);
    //     pthread_mutex_unlock(&mutex);
    // }

    // print_cache(cache);

    // parser_free(parser);
    close(client_fd);
}

void *thread(void *vargp) {
    fprintf(stderr, "IN THREAD! \n");
    int connfd = *((int *)vargp);
    pthread_detach(pthread_self());
    Free(vargp);
    serve(connfd);
    close(connfd);
}

int main(int argc, char **argv) {
    int listenfd;

    // cache = init_cache();
    // pthread_mutex_init(&mutex, NULL);
    signal(SIGPIPE, SIG_IGN);

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

    while (1) {
        pthread_t tid;

        client_info client_data;
        client_info *client = &client_data;
        client->addrlen = sizeof(client->addr);
        int *connfdp = malloc(sizeof(int));
        *connfdp = accept(listenfd, (SA *)&client->addr, &client->addrlen);

        // if (client->connfd < 0) {
        if (*connfdp < 0) {
            perror("accept");
            continue;
        }

        /* Connection Thread is established; serve client */
        pthread_create(&tid, NULL, thread, (void *)connfdp);
    }
    // return 0;
}
