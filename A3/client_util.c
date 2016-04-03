/*
 *   CSC469 Winter 2016 A3
 *   Instructor: Bogdan Simion
 *   Date:       19/03/2016
 *  
 *      File:      client_util.c 
 *      Author:    Angela Demke Brown
 *      Version:   1.0.0
 *      Date:      17/11/2010
 *   
 * Please report bugs/comments to bogdan@cs.toronto.edu
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <netinet/in.h>
#include <netdb.h>

#include "client.h"

/* Currently, all functions in this file are only used if the client is 
 * retrieving chatserver parameters from the location server.
 */

#ifdef USE_LOCN_SERVER
static void build_req(char *buf)
{
	/* Write an HTTP GET request for the chatserver.txt file into buf */

	int nextpos;

	sprintf(buf,"GET /~csc469h/winter/chatserver.txt HTTP/1.0\r\n");

	nextpos = strlen(buf);
	sprintf(&buf[nextpos],"\r\n");
}
#endif /* USE_LOCN_SERVER */

#ifdef USE_LOCN_SERVER
static char *skip_http_headers(char *buf)
{
	/* Given a pointer to a buffer which contains an HTTP reply,
	 * skip lines until we find a blank, and then return a pointer
	 * to the start of the next line, which is the reply body.
	 * 
	 * DO NOT call this function if buf does not contain an HTTP
	 * reply message.  The termination condition on the while loop 
	 * is ill-defined for arbitrary character arrays, and may lead 
	 * to bad things(TM). 
	 *
	 * Feel free to improve on this.
	 */

	char *curpos;
	int n;
	char line[256];

	curpos = buf;

	while ( sscanf(curpos,"%256[^\n]%n",line,&n) > 0) {
		if (strlen(line) == 1) { /* Just the \r was consumed */
			/* Found our blank */
			curpos += n+1; /* skip line and \n at end */
			break;
		}
		curpos += n+1;
	}

	return curpos;
}
#endif /* USE_LOCN_SERVER */

#ifdef USE_LOCN_SERVER 
int retrieve_chatserver_info(char *chatserver_name, u_int16_t *tcp_port, u_int16_t *udp_port)
{
	int locn_socket_fd;
	char *buf;
	int buflen;
	int code;
	int  n;

	/* Initialize locnserver_addr. 
	 * We use a text file at a web server for location info
	 * so this is just contacting the CDF web server 
	 */

	/* 
	 * 1. Set up TCP connection to web server "www.cdf.toronto.edu", 
	 *    port 80 
	 */
	
	// Set up socket
	if((locn_socket_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
		perror("Socket creation failed");
		exit(1);
	}
	
	// Resolve host
	struct sockaddr_in locnserver_addr;
	struct hostent* server = NULL;
	if(!(server = gethostbyname("www.cdf.toronto.edu"))) {
		fprintf(stderr,"Error resolving location server host name.\n");
		exit(1);
	}
	
	// Setup server address
	memset((char*)&locnserver_addr, 0, sizeof(locnserver_addr));
	locnserver_addr.sin_family = AF_INET;
	locnserver_addr.sin_port = htons(80);
	memcpy((char*)&locnserver_addr.sin_addr.s_addr, (char*)server->h_addr, server->h_length);
	
	// Connect
	if(connect(locn_socket_fd, (struct sockaddr*)&locnserver_addr, sizeof(locnserver_addr)) < 0) {
		perror("TCP connection failed");
	}

	/* The code you write should initialize locn_socket_fd so that
	 * it is valid for the write() in the next step.
	 */

	/* 2. write HTTP GET request to socket */

	buf = (char *)malloc(MAX_MSG_LEN);
	bzero(buf, MAX_MSG_LEN);
	build_req(buf);
	buflen = strlen(buf);

	write(locn_socket_fd, buf, buflen);

	/* 3. Read reply from web server */

	read(locn_socket_fd, buf, MAX_MSG_LEN);
	close(locn_socket_fd);

	/* 
	 * 4. Check if request succeeded.  If so, skip headers and initialize
	 *    server parameters with body of message.  If not, print the 
	 *    STATUS-CODE and STATUS-TEXT and return -1.
	 */

	/* Ignore version, read STATUS-CODE into variable 'code' , and record
	 * the number of characters scanned from buf into variable 'n'
	 */
	sscanf(buf, "%*s %d%n", &code, &n);

	if(code == 200) {
		// We are given a chat server to connect to
		char* body = skip_http_headers(buf);
		char delim[] = " ";
		
		strncpy(chatserver_name, strtok(body, delim), MAX_HOST_NAME_LEN);
		*tcp_port = (u_int16_t)strtol(strtok(NULL, delim), NULL, 10);
		*udp_port = (u_int16_t)strtol(strtok(NULL, delim), NULL, 10);
		// printf("%s, %d, %d\n", chatserver_name, *tcp_port, *udp_port);
	} else {
		// Request failed
		printf("Request to location server failed, status: %d%s\n", code, strtok(buf, (char*)"\n") + n);
		exit(1);
	}

	/* 5. Clean up after ourselves and return. */

	free(buf);
	return 0;

}

#endif /* USE_LOCN_SERVER */
