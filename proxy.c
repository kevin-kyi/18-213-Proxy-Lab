/*
 * tiny.c - A simple, iterative HTTP/1.0 Web server that uses the
 *     GET method to serve static and dynamic content.
 *
 * Updated 04/2017 - Stanley Zhang <szz@andrew.cmu.edu>
 * Fixed some style issues, stop using csapp functions where not appropriate
 *
 *
 * Updated 11/2022 - Gilbert Fan <gsfan@andrew.cmu.edu>, Adittyo Paul <adittyop@andrew.cmu.edu>
 * Updated tiny to use http_parser instead of sscanf. Also changed
 * parse_uri into parse_path instead as the parsed string is a PATH.
 */

#include "csapp.h"
#include "http_parser.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <ctype.h>

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <netdb.h>

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
 * String to use for the User-Agent header.
 * Don't forget to terminate with \r\n
 */
static const char *header_user_agent = "Mozilla/5.0"
                                       " (X11; Linux x86_64; rv:3.10.0)"
                                       " Gecko/20230411 Firefox/63.0.1";

static const char *connection_hdr = "Connection: close\r\n";
static const char *proxy_connection_hdr = "Proxy-Connection: close\r\n";

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

/*
 * read_requesthdrs - read HTTP request headers
 * Returns true if an error occurred, or false otherwise.
 */
bool read_requesthdrs(client_info *client, rio_t *rp, parser_t *parser) {
    char buf[MAXLINE];

    // Read lines from socket until final carriage return reached
    while (true) {
        if (rio_readlineb(rp, buf, sizeof(buf)) <= 0) {
            return true;
        }

        /* Check for end of request headers */
        if (strcmp(buf, "\r\n") == 0) {
            return false;
        }

        // Parse the request header with parser
	    parser_state parse_state = parser_parse_line(parser, buf);
        if (parse_state != HEADER) {
	        clienterror(client->connfd, "400", "Bad Request",
			"Tiny could not parse request headers");
	        return true;
	    }

	    header_t *header = parser_retrieve_next_header(parser);
        printf("%s: %s\n", header->name, header->value);
    }
}

/*
 * serve - handle one HTTP request/response transaction
 */
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

    // parser_new
    // parse lines
    // Construct request: method, host, port, path

    printf("%s", buf);

    /* Parse the request line and check if it's well-formed */
    parser_t *parser = parser_new();

    parser_state parse_state = parser_parse_line(parser, buf);

    if (parse_state != REQUEST) {
        parser_free(parser);
	    clienterror(client->connfd, "400", "Bad Request",
		    "Tiny received a malformed request");
	    return;
    }

    /* Tiny only cares about METHOD and PATH from the request */
    const char *method, *path;
    parser_retrieve(parser, METHOD, &method);
    parser_retrieve(parser, PATH, &path);

    /* Check that the method is GET */
    if (strcmp(method, "GET") != 0) {
	    parser_free(parser);
    	clienterror(client->connfd, "501", "Not Implemented",
		    "Tiny does not implement this method");
    	return;
    }

    /* Check if reading request headers caused an error */
    if (read_requesthdrs(client, &rio, parser)) {
        parser_free(parser);
	    return;
    }

    // header_t
    char *host_temp = "Host: www.cmu.edu:8080\r\n";
    char *conn_temp = "Connection: close\r\n";
    char *proxy_temp = "Proxy-Connection: close\r\n";
    header_t h;
    while (h = parser_retrieve_next_header(parser) != NULL) {
        rio_writen(client->connfd, host_temp, strlen(host_temp));
        rio_writen(client->connfd, header_user_agent, strlen(header_user_agent));
        rio_writen(client->connfd, conn_temp, strlen(conn_temp));
        rio_writen(client->connfd, proxy_temp, strlen(proxy_temp));
    }



    // Send headers: writeup 2 (parse line to do headers)
    //      use rio
    // Send to server:
    //      use rio from buf
    //       rio_writenb(server)
    // free


    // parser_free(parser);
}


int main(int argc, char **argv) {
    int listenfd;

    /* Check command line args */
    if (argc != 2) {
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        exit(1);
    }
    signal(SIGPIPE, SIG_IGN);

    // Open listening file descriptor
    listenfd = open_listenfd(argv[1]);
    if (listenfd < 0) {
        fprintf(stderr, "Failed to listen on port: %s\n", argv[1]);
        exit(1);
    }

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
}




