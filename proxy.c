/*
 * Starter code for proxy lab.
 * Feel free to modify this code in whatever way you wish.
 */

/* Some useful includes to help you get started */

#include "csapp.h"

#include <assert.h>
#include <ctype.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/types.h>

//Useful functions included for parsing in serve
#include "http_parser.h"


/*
 * Debug macros, which can be enabled by adding -DDEBUG in the Makefile
 * Use these if you find them useful, or delete them if not
 */
#ifdef DEBUG
#define dbg_assert(...) assert(__VA_ARGS__)
#define dbg_printf(...) fprintf(stderr, __VA_ARGS__)
#else
#define dbg_assert(...)
#define dbg_printf(...)
#endif

/*
 * Max cache and object sizes
 * You might want to move these to the file containing your cache implementation
 */
#define MAX_CACHE_SIZE (1024 * 1024)
#define MAX_OBJECT_SIZE (100 * 1024)

/*
 * String to use for the User-Agent header.
 * Don't forget to terminate with \r\n
 */
static const char *header_user_agent = "Mozilla/5.0"
                                       " (X11; Linux x86_64; rv:3.10.0)"
                                       " Gecko/20191101 Firefox/63.0.1";

/*From tiny.c: */
#define HOSTLEN 256
#define SERVLEN 8

/* Typedef for convenience */
typedef struct sockaddr SA;

/* Information about a connected client. */
typedef struct {
    struct sockaddr_in addr;    // Socket address
    socklen_t addrlen;          // Socket address length
    int connfd;                 // Client connection file descriptor
    char host[HOSTLEN];         // Client host
    char serv[SERVLEN];         // Client service (port)
} client_info;

/* URI parsing results. */
typedef enum {
    PARSE_ERROR,
    PARSE_STATIC,
    PARSE_DYNAMIC
} parse_result;



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
            "<!DOCTYPE html>\r\n" \
            "<html>\r\n" \
            "<head><title>Tiny Error</title></head>\r\n" \
            "<body bgcolor=\"ffffff\">\r\n" \
            "<h1>%s: %s</h1>\r\n" \
            "<p>%s</p>\r\n" \
            "<hr /><em>The Tiny Web server</em>\r\n" \
            "</body></html>\r\n", \
            errnum, shortmsg, longmsg);
    if (bodylen >= MAXBUF) {
        return; // Overflow!
    }

    /* Build the HTTP response headers */
    buflen = snprintf(buf, MAXLINE,
            "HTTP/1.0 %s %s\r\n" \
            "Content-Type: text/html\r\n" \
            "Content-Length: %zu\r\n\r\n", \
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



bool read_requesthdrs(parser_t *par, client_info *client, rio_t *rp) {
    char buf[MAXLINE];
    parser_state ps; 


    while (true) {
        if (rio_readlineb(rp, buf, sizeof(buf)) <= 0) {
            return true;
        }

        /* Check for end of request headers */
        if (strcmp(buf, "\r\n") == 0) {
            return false;
        }

        /* Parse header into name and value */
        if (HEADER != (ps = parser_parse_line(par, buf))) {
            /* Error parsing header */
            clienterror(client->connfd, "400", "Bad Request",
                        "Tiny could not parse request headers");
            return true;
        }

    }
    return false;
}






void serve(client_info *client) {
    // Get some extra info about the client (hostname/port)
    // This is optional, but it's nice to know who's connected
    int res = getnameinfo(
            (SA *) &client->addr, client->addrlen,
            client->host, sizeof(client->host),
            client->serv, sizeof(client->serv),
            0);
    if (res == 0) {
        printf("Accepted connection from %s:%s\n", client->host, client->serv);
    }
    else {
        fprintf(stderr, "getnameinfo failed: %s\n", gai_strerror(res));
    }

    rio_t rio;
    rio_readinitb(&rio, client->connfd);

    /* Read request line */
    char buf[MAXLINE];
    if (rio_readlineb(&rio, buf, sizeof(buf)) <= 0) {
        return;
    }


    parser_t *par = parser_new();
    parser_parse_line(par, buf);

    const char *method, *host, *port, *path;
    parse_retrieve(par, METHOD, &method);
    parse_retrieve(par, HOST, &host);
    parse_retrieve(par, PORT, &port);
    parse_retrieve(par, PATH, &path);

    if (strcmp(method, "GET") != 0) {
        clienterror(client->connfd, "501", "Not Implemented",
                    "Tiny does not implement this method");
        parser_free(par);
        return;
    }

    if(read_requesthdrs(client, &rio, par)){
        parser_free(par);
        return;
    }


    int client_fd = open_clientfd(host, port);
    if (client_fd < 0){
        fprintf(stderr, "Connection to host failed");
        parser_free(par);
        close(client_fd);
        return;
    }


    
    
    
    
    //server termination calls
    size_t r;
    rio_t nRio;

    while ((r = rio_readnb(&nRio, buf, MAXLINE)) != 0){
        rio_writen(client->connfd, buf, r);
    }

    return;
}




int main(int argc, char **argv) {
    int listenfd;

    /*From: tiny.c*/

    /* Check Command line Args */
    if (argc != 2){
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        exit(1);
    }


    listenfd = open_listenfd(argv[1]);
    if (listenfd < 0) {
        fprintf(stderr, "Failed to listen on port: %s\n", argv[1]);
        exit(1);
    }


    //signal(SIGPIPE, SIG_IGN);
    while (1) {
        /* Allocate space on the stack for client info */
        client_info client_data;
        client_info *client = &client_data;

        /* Initialize the length of the address */
        client->addrlen = sizeof(client->addr);

        /* accept() will block until a client connects to the port */
        client->connfd = accept(listenfd,
                (SA *) &client->addr, &client->addrlen);
        if (client->connfd < 0) {
            perror("accept");
            continue;
        }

        /* Connection is established; serve client */
        serve(client);
        close(client->connfd);
    }

    printf("%s", header_user_agent);
    return 0;
}




