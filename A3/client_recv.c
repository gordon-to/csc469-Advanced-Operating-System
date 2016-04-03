/*
 *   CSC469 Winter 2016 A3
 *   Instructor: Bogdan Simion
 *   Date:       19/03/2016
 *  
 *      File:      client_recv.c 
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
#include <netdb.h>

#include "client.h"

#define PORT_START 7000

static char *option_string = "f:";
int udp_socket_fd;

/* For communication with chat client control process */
int ctrl2rcvr_qid;
char ctrl2rcvr_fname[MAX_FILE_NAME_LEN];


void usage(char **argv) {
	printf("usage:\n");
	printf("%s -f <msg queue file name>\n",argv[0]);
	exit(1);
}


void open_client_channel(int *qid) {

	/* Get messsage channel */
	key_t key = ftok(ctrl2rcvr_fname, 42);

	if ((*qid = msgget(key, 0400)) < 0) {
		perror("open_channel - msgget failed");
		fprintf(stderr,"for message channel ./msg_channel\n");

		/* No way to tell parent about our troubles, unless/until it 
		 * wait's for us.  Quit now.
		 */
		exit(1);
	}

	return;
}

void send_error(int qid, u_int16_t code)
{
	/* Send an error result over the message channel to client control process */
	msg_t msg;

	msg.mtype = CTRL_TYPE;
	msg.body.status = RECV_NOTREADY;
	msg.body.value = code;

	if (msgsnd(qid, &msg, sizeof(struct body_s), 0) < 0) {
		perror("send_error msgsnd");
	}
							 
}

void send_ok(int qid, u_int16_t port)
{
	/* Send "success" result over the message channel to client control process */
	msg_t msg;

	msg.mtype = CTRL_TYPE;
	msg.body.status = RECV_READY;
	msg.body.value = port;

	if (msgsnd(qid, &msg, sizeof(struct body_s), 0) < 0) {
		perror("send_ok msgsnd");
	} 

}

void init_receiver()
{

	/* 1. Make sure we can talk to parent (client control process) */
	printf("Trying to open client channel\n");

	open_client_channel(&ctrl2rcvr_qid);

	/**** YOUR CODE TO FILL IMPLEMENT STEPS 2 AND 3 ****/

	/* 2. Initialize UDP socket for receiving chat messages. */
	struct sockaddr_in server_udp_addr;
	if ((udp_socket_fd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
		perror("Socket creation failed\n");
		send_error(ctrl2rcvr_qid, SOCKET_FAILED);
		exit(1);
	}
	
	// number of retries of ports
	int num_port_retry = 200;

	int port_serv = PORT_START;
	memset((char*)&server_udp_addr, 0, sizeof(server_udp_addr));
	server_udp_addr.sin_family = AF_INET; // IPv4
	server_udp_addr.sin_port = htons(port_serv);
	server_udp_addr.sin_addr.s_addr = htonl(INADDR_ANY);

	// start from port 7000, keep trying until free port up num_port_retry retries
	while ((bind(udp_socket_fd, (struct sockaddr *) &server_udp_addr, sizeof(server_udp_addr))) < 0) {
		if (port_serv > PORT_START + num_port_retry) {
			perror("Port binding failed\n");
			send_error(ctrl2rcvr_qid, BIND_FAILED); // send error 503 service unavailable
			exit(1);
		}
		port_serv += 1;
		server_udp_addr.sin_port = htons(port_serv);
	}

	/* 3. Tell parent the port number if successful, or failure code if not. 
	 *    Use the send_error and send_ok functions
	 */

	// send socket binded to a port
	send_ok(ctrl2rcvr_qid, port_serv);

}


/* Function to deal with a single message from the chat server */

void handle_received_msg(char *buf)
{

	/**** YOUR CODE HERE ****/
	struct chat_msghdr * msg_head = (struct chat_msghdr *) buf;
	
	// Print msg
	printf("%s::\n", msg_head->sender.member_name);
	printf("%.*s\n", msg_head->msg_len, *msg_head->msgdata);


}



/* Main function to receive and deal with messages from chat server
 * and client control process.  
 *
 * You may wish to refer to server_main.c for an example of the main 
 * server loop that receives messages, but remember that the client 
 * receiver will be receiving (1) connection-less UDP messages from the 
 * chat server and (2) IPC messages on the from the client control process
 * which cannot be handled with the same select()/FD_ISSET strategy used 
 * for file or socket fd's.
 */
void receive_msgs()
{
	char *buf = (char *)malloc(MAX_MSG_LEN);
  
	if (buf == 0) {
		printf("Could not malloc memory for message buffer\n");
		exit(1);
	}


	/**** YOUR CODE HERE ****/
	int result;
	msg_t msg;

	while(TRUE) {

		/**** YOUR CODE HERE ****/;
		// check if parent has msg
		result = msgrcv(ctrl2rcvr_qid, &msg, sizeof(struct body_s), CTRL_TYPE, IPC_NOWAIT);
		if (result > 0) {
		// parent has msg
			if (msg.body.status == CHAT_QUIT) {
				// break to cleanup
				break;
			}
		} else if (result != -1 && errno == ENOMSG) {
			perror("msgrcv error");
		}

		// Get udp msg
		if (recv(udp_socket_fd, buf, MAX_MSG_LEN, MSG_DONTWAIT) > 0) {
			handle_received_msg(buf);
		}
		sleep(1);

	}

	/* Cleanup */
	free(buf);
	return;
}


int main(int argc, char **argv) {
	char option;

	printf("RECEIVER alive: parsing options! (argc = %d\n",argc);

	while((option = getopt(argc, argv, option_string)) != -1) {
		switch(option) {
		case 'f':
			strncpy(ctrl2rcvr_fname, optarg, MAX_FILE_NAME_LEN);
			break;
		default:
			printf("invalid option %c\n",option);
			usage(argv);
			break;
		}
	}

	if(strlen(ctrl2rcvr_fname) == 0) {
		usage(argv);
	}

	printf("Receiver options ok... initializing\n");

	init_receiver();

	receive_msgs();

	return 0;
}
