/*
 *   CSC469 Winter 2016 A3
 *   Instructor: Bogdan Simion
 *   Date:       19/03/2016
 *  
 *      File:      client_main.c 
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
#include <signal.h>

#include <netinet/in.h>
#include <netdb.h>

#include "client.h"
#include <errno.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>

/*************** GLOBAL VARIABLES ******************/

static char *option_string = "h:t:u:n:";
int udp_addr_len;
int cmh_size;

/* For communication with chat server */
/* These variables provide some extra clues about what you will need to
 * implement.
 */
char server_host_name[MAX_HOST_NAME_LEN];

/* For control messages */
u_int16_t server_tcp_port;
struct sockaddr_in server_tcp_addr;

/* For chat messages */
u_int16_t server_udp_port;
struct sockaddr_in server_udp_addr;
int udp_socket_fd;

/* Needed for REGISTER_REQUEST */
char member_name[MAX_MEMBER_NAME_LEN];
u_int16_t client_udp_port; 

/* Initialize with value returned in REGISTER_SUCC response */
u_int16_t member_id = 0;

/* For communication with receiver process */
pid_t receiver_pid;
char ctrl2rcvr_fname[MAX_FILE_NAME_LEN];
int ctrl2rcvr_qid;

/* For reconnection when the server has failed */
#define MAX_RETRIES 5		// Number of reconnect retries
int connect_retries = MAX_RETRIES;
#define TCP_RETRIES 5		// Number of TCP request retries
char receiver_opened = 0;
char last_channel[MAX_ROOM_NAME_LEN];
// Variables for automatic name retrying
char suffix_appended = 0;
int suffix_len = 3;

/* MAX_MSG_LEN is maximum size of a message, including header+body.
 * We define the maximum size of the msgdata field based on this.
 */
#define MAX_MSGDATA (MAX_MSG_LEN - sizeof(struct chat_msghdr))

pthread_mutex_t read_mutex = PTHREAD_MUTEX_INITIALIZER;
char * user_input_buffer;
char ready_to_read;

/************* FUNCTION DEFINITIONS ***********/

static void usage(char **argv) {

	printf("usage:\n");

#ifdef USE_LOCN_SERVER
	printf("%s -n <client member name>\n",argv[0]);
#else 
	printf("%s -h <server host name> -t <server tcp port> -u <server udp port> -n <client member name>\n",argv[0]);
#endif /* USE_LOCN_SERVER */

	exit(1);
}



void shutdown_clean() {
	/* Function to clean up after ourselves on exit, freeing any 
	 * used resources 
	 */

	/* Add to this function to clean up any additional resources that you
	 * might allocate.
	 */

	msg_t msg;

	/* 1. Send message to receiver to quit */
	msg.mtype = RECV_TYPE;
	msg.body.status = CHAT_QUIT;
	msgsnd(ctrl2rcvr_qid, &msg, sizeof(struct body_s), 0);

	/* 2. Close open fd's */
	close(udp_socket_fd);

	/* 3. Wait for receiver to exit */
	waitpid(receiver_pid, 0, 0);

	/* 4. Destroy message channel */
	unlink(ctrl2rcvr_fname);
	if (msgctl(ctrl2rcvr_qid, IPC_RMID, NULL)) {
		perror("cleanup - msgctl removal failed");
	}

	free(user_input_buffer);
	exit(0);
}

void handle_int()
{
	printf("%s\n", "ctrl-c caught");
	shutdown_clean();
}

int initialize_client_only_channel(int *qid)
{
	/* Create IPC message queue for communication with receiver process */

	int msg_fd;
	int msg_key;

	/* 1. Create file for message channels */

	snprintf(ctrl2rcvr_fname,MAX_FILE_NAME_LEN,"/tmp/ctrl2rcvr_channel.XXXXXX");
	msg_fd = mkstemp(ctrl2rcvr_fname);

	if (msg_fd  < 0) {
		perror("Could not create file for communication channel");
		return -1;
	}

	close(msg_fd);

	/* 2. Create message channel... if it already exists, delete it and try again */

	msg_key = ftok(ctrl2rcvr_fname, 42);

	if ( (*qid = msgget(msg_key, IPC_CREAT|IPC_EXCL|S_IREAD|S_IWRITE)) < 0) {
		if (errno == EEXIST) {
			if ( (*qid = msgget(msg_key, S_IREAD|S_IWRITE)) < 0) {
				perror("First try said queue existed. Second try can't get it");
				unlink(ctrl2rcvr_fname);
				return -1;
			}
			if (msgctl(*qid, IPC_RMID, NULL)) {
				perror("msgctl removal failed. Giving up");
				unlink(ctrl2rcvr_fname);
				return -1;
			} else {
				/* Removed... try opening again */
				if ( (*qid = msgget(msg_key, IPC_CREAT|IPC_EXCL|S_IREAD|S_IWRITE)) < 0) {
					perror("Removed queue, but create still fails. Giving up");
					unlink(ctrl2rcvr_fname);
					return -1;
				}
			}

		} else {
			perror("Could not create message queue for client control <--> receiver");
			unlink(ctrl2rcvr_fname);
			return -1;
		}
    
	}

	return 0;
}



int create_receiver()
{
	/* Create the receiver process using fork/exec and get the port number
	 * that it is receiving chat messages on.
	 */

	int retries = 20;
	int numtries = 0;

	/* 1. Set up message channel for use by control and receiver processes */

	if (initialize_client_only_channel(&ctrl2rcvr_qid) < 0) {
		return -1;
	}

	/* 2. fork/exec xterm */

	receiver_pid = fork();

	if (receiver_pid < 0) {
		fprintf(stderr,"Could not fork child for receiver\n");
		return -1;
	}

	if ( receiver_pid == 0) {
		/* this is the child. Exec receiver */
		char *argv[] = {"xterm",
				"-e",
				"./receiver",
				"-f",
				ctrl2rcvr_fname,
				0
		};

		execvp("xterm", argv);
		printf("Child: exec returned. that can't be good.\n");
		exit(1);
	} 

	/* This is the parent */

	/* 3. Read message queue and find out what port client receiver is using */

	while ( numtries < retries ) {
		int result;
		msg_t msg;
		result = msgrcv(ctrl2rcvr_qid, &msg, sizeof(struct body_s), CTRL_TYPE, IPC_NOWAIT);
		if (result == -1 && errno == ENOMSG) {
			sleep(1);
			numtries++;
		} else if (result > 0) {
			if (msg.body.status == RECV_READY) {
				printf("Start of receiver successful, port %u\n",msg.body.value);
				client_udp_port = msg.body.value;
			} else {
				printf("start of receiver failed with code %u\n",msg.body.value);
				return -1;
			}
			break;
		} else {
			perror("msgrcv");
		}
    
	}

	if (numtries == retries) {
		/* give up.  wait for receiver to exit so we get an exit code at least */
		int exitcode;
		printf("Gave up waiting for msg.  Waiting for receiver to exit now\n");
		waitpid(receiver_pid, &exitcode, 0);
		printf("start of receiver failed, exited with code %d\n",exitcode);
	}

	return 0;
}

/* Open a new tcp connection to the server. Returns fd. */
int connect_tcp() {
	// For the connection, we'll use select() to define a custom timeout
	int sockfd;
	int flags;
	fd_set set;
	struct timeval timeout;
	
	// Create the socket
	if((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
		perror("Socket creation failed");
		shutdown_clean();
	}
	
	// Get the current flags, then put the socket into non-blocking mode
	flags = fcntl(sockfd, F_GETFL, 0);
	fcntl(sockfd, F_SETFL, O_NONBLOCK);
	// Set select() variables
	FD_ZERO(&set);
	FD_SET(sockfd, &set);
	timeout.tv_sec = 10;
	timeout.tv_usec = 0;
	
	// Then call connect() and select()
	connect(sockfd, (struct sockaddr*)&server_tcp_addr, sizeof(server_tcp_addr));
	// How come I have to cast this... ^ as struct sockaddr*?
	
	int result = select(sockfd + 1, NULL, &set, NULL, &timeout);
	// Don't forget to put the socket back into blocking mode.
	fcntl(sockfd, F_SETFL, flags);
	if(result == 0) {
		errno = ETIMEDOUT;
		return -1;
	} else if(result < 0) {
		perror("Error occurred on select");
		return -1;
	}
	
	// In case the connection failed
	int so_error;
	socklen_t len = sizeof(so_error);
	
	getsockopt(sockfd, SOL_SOCKET, SO_ERROR, &so_error, &len);
	if (so_error != 0) {
		errno = so_error;
		return -1;
	}
	
	return sockfd;
}

/* Function to send control messages */
void send_ctrl_msg(int sockfd, int type, char* data, int msg_len) {
	// Create the header and fill in the metadata
	struct control_msghdr* cmh;
	char* buf = malloc(msg_len);
	memset(buf, 0, msg_len);
	
	cmh = (struct control_msghdr*)buf;
	cmh->msg_type = htons(type);
	cmh->member_id = htons(member_id);
	cmh->msg_len = htons(msg_len);
	
	// Add the payload if necessary
	if(msg_len > cmh_size) {
		strncpy((char*)cmh->msgdata, data, msg_len - cmh_size);
	}
	
	write(sockfd, buf, cmh->msg_len);
	free(buf);
}

/* Receive and parse the response from the server */
struct control_msghdr* receive_ctrl_msg(int sockfd) {
	char* buf = malloc(MAX_MSG_LEN);
	memset(buf, 0, MAX_MSG_LEN);
	
	if(read(sockfd, buf, MAX_MSG_LEN) < 0) {
		// perror("Error reading from TCP socket");
		free(buf);
		return NULL;
	}
	
	return (struct control_msghdr*)buf;
}

/* Make attempts to send a TCP request to the servers. */
struct control_msghdr* attempt_request(int type, char* data, int msg_len) {
	struct control_msghdr* res = NULL;
	int attempts = TCP_RETRIES;
	
	// Place everything in a retry loop until we get a proper response from
	// those darned buggy servers
	while(!res && attempts > 0) {
		int sockfd = connect_tcp();
		if(sockfd == -1) {
			// Connection failed
			return NULL;
		}
		
		// Call send_ctrl_msg to send the request
		send_ctrl_msg(sockfd, type, data, msg_len);
		
		// Wait for the server's response and get the header information
		res = receive_ctrl_msg(sockfd);
		close(sockfd);
		attempts--;
	}
	
	return res;
}
	

/*********************************************************************/

/* We define one handle_XXX_req() function for each type of 
 * control message request from the chat client to the chat server.
 * These functions should return 0 on success, and a negative number 
 * on error.
 */

int handle_register_req(int port)
{
	// Create the registration data and send the request
	struct register_msgdata* rmd = malloc(sizeof(struct register_msgdata) + MAX_MEMBER_NAME_LEN);
	
	rmd->udp_port = htons(port);
	strncpy((char*)rmd->member_name, member_name, MAX_MEMBER_NAME_LEN);
	int size = cmh_size + sizeof(struct register_msgdata) + strlen(member_name) + 1;
	
	// Use attempt_request() to perform multiple attempts in case of failure
	struct control_msghdr* res = attempt_request(REGISTER_REQUEST, (char*)rmd, size);
	
	// Handle the response
	if(!res) {
		// Something went wrong
		perror("Disconnected");
		return -2;
	} else if(ntohs(res->msg_type) == REGISTER_SUCC) {
		printf("Successfully registered at %s\n", server_host_name);
		member_id = ntohs(res->member_id);
	} else {
		printf("Server registration failed!\n");
		printf("Reason: %s\n", (char*)res->msgdata);
		
		// Special return value for "name taken" error
		// This assumes the error message is exactly as we expect
		if(strncmp((char*)res->msgdata, "Name has already been used!", 10) == 0) {
			free(res);
			return -3;
		}
		
		free(res);
		return -1;
	}
	
	free(res);
	return 0;
}

int handle_room_list_req(char verboose)
{
	// Make the request to the server
	struct control_msghdr* res = attempt_request(ROOM_LIST_REQUEST, NULL, cmh_size);
	
	// Handle the response
	if(!res) {
		// Something went wrong
		perror("Disconnected");
		return -2;
	} else if(ntohs(res->msg_type) == ROOM_LIST_SUCC) {
		if (verboose) {
			printf("Rooms: \n");
			printf("%s\n", (char*)res->msgdata);
		}
	} else {
		if (verboose){
			printf("Room list failed.\n");
			printf("Reason: %s\n", (char*)res->msgdata);
		}
		free(res);
		return -1;
	}
	
	free(res);
	return 0;
}

int handle_member_list_req(char *room_name)
{
	struct control_msghdr* res = attempt_request(MEMBER_LIST_REQUEST, room_name,
					  						cmh_size + strlen(room_name) + 1);
	
	if(!res) {
		perror("Disconnected");
		return -2;
	} else if(ntohs(res->msg_type) == MEMBER_LIST_SUCC) {
		printf("Members: \n");
		printf("%s\n", (char*)res->msgdata);
	} else {
		printf("Member list failed.\n");
		printf("Reason: %s\n", (char*)res->msgdata);
		free(res);
		return -1;
	}
	
	free(res);
	return 0;
}

int handle_switch_room_req(char *room_name)
{
	struct control_msghdr* res = attempt_request(SWITCH_ROOM_REQUEST, room_name,
					  						cmh_size + strlen(room_name) + 1);
	
	if(!res) {
		perror("Disconnected");
		return -2;
	} else if(ntohs(res->msg_type) == SWITCH_ROOM_SUCC) {
		printf("Successfully switched to room \"%s\".\n", room_name);
		if(strlen(last_channel) > 0) {
			// Send a notification to the receiver
			msg_t msg;
			msg.mtype = RECV_TYPE;
			msg.body.status = SWITCHED_CHAN;
			msgsnd(ctrl2rcvr_qid, &msg, sizeof(struct body_s), 0);
		}
		
		strcpy(last_channel, room_name);
	} else {
		printf("Room switch failed.\n");
		printf("Reason: %s\n", (char*)res->msgdata);
		free(res);
		return -1;
	}
	
	free(res);
	return 0;
}

int handle_create_room_req(char *room_name)
{
	struct control_msghdr* res = attempt_request(CREATE_ROOM_REQUEST, room_name,
					  						cmh_size + strlen(room_name) + 1);
	
	if(!res) {
		perror("Disconnected");
		return -2;
	} else if(ntohs(res->msg_type) == CREATE_ROOM_SUCC) {
		printf("Successfully created room \"%s\".\n", room_name);
	} else {
		printf("Room creation failed.\n");
		printf("Reason: %s\n", (char*)res->msgdata);
		free(res);
		return -1;
	}
	
	free(res);
	return 0;
}


int handle_quit_req()
{
	int sockfd = connect_tcp();
	if(sockfd >= 0) {
		send_ctrl_msg(sockfd, QUIT_REQUEST, NULL, cmh_size);
		close(sockfd);
	}
	
	printf("Shutting down...\n");
	shutdown_clean();
	return 0;
}


int init_client()
{
	/* Initialize client so that it is ready to start exchanging messages
	 * with the chat server.
	 *
	 * YOUR CODE HERE
	 */

#ifdef USE_LOCN_SERVER

	/* 0. Get server host name, port numbers from location server.
	 *    See retrieve_chatserver_info() in client_util.c
	 */
	
	retrieve_chatserver_info(server_host_name, &server_tcp_port, &server_udp_port);

#endif
	
	printf("\nAttempting to connect to %s...\n", server_host_name);
	struct hostent* server = NULL;
	if(!(server = gethostbyname(server_host_name))) {
		fprintf(stderr,"ERROR, no such host as %s\n", server_host_name);
		exit(1);
	}
 
	/* 1. initialization to allow TCP-based control messages to chat server */
	memset((char*)&server_tcp_addr, 0, sizeof(server_tcp_addr));
	server_tcp_addr.sin_family = AF_INET;
	server_tcp_addr.sin_port = htons(server_tcp_port);
	memcpy((char*)&server_tcp_addr.sin_addr.s_addr, (char*)server->h_addr, server->h_length);

	/* 2. initialization to allow UDP-based chat messages to chat server */
	if((udp_socket_fd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
		perror("Socket creation failed");
		exit(1);
	}
	
	memset((char*)&server_udp_addr, 0, sizeof(server_udp_addr));
	server_udp_addr.sin_family = AF_INET;
	server_udp_addr.sin_port = htons(server_udp_port);
	memcpy((char*)&server_udp_addr.sin_addr.s_addr, (char*)server->h_addr, server->h_length);
	
	/* 3. spawn receiver process - see create_receiver() in this file. */
	if(!receiver_opened) {
		if(create_receiver() < 0) {
			perror("Error creating receiver");
		}
		receiver_opened = 1;
	}

	/* 4. register with chat server */
	int result = handle_register_req(client_udp_port);
	int suffix = 1;
	
	// We need a while loop because of the repetitive nature of the automatic
	// name retry system
	while(result != 0) {
		if(result == -1) {
			// Unrecoverable registration error.
			shutdown_clean();
		} else if(result == -2) {
			// Connection failed.
			return -2;
		} else if(result == -3) {
			// Automatically attempt to reconnect with new name
			if(!suffix_appended) {
				// First time append
				if(strlen(member_name) + 5 > MAX_MEMBER_NAME_LEN) {
					// The name is too long and we can't append to it
					shutdown_clean();
				}
				// Add the brackets for the first time
				strcat(member_name, " (1)");
				suffix_appended = 1;
			} else if(suffix_len == 4 && suffix < 10) {
				// Small compensation
				suffix_len = 3;
			} else if(suffix == 10) {
				// Special case for reaching 10
				if(strlen(member_name) + 2 > MAX_MEMBER_NAME_LEN) {
					// We've reached double digits and now the name is too long
					printf("Could not re-register with a new name.\n");
					shutdown_clean();
				}
				strcpy(member_name + strlen(member_name) - 3, "(10)");
				suffix_len++;
			} else if(suffix > 99) {
				// I'm just not going to deal with triple digits
				printf("Could not re-register with a new name.\n");
				shutdown_clean();
			}
			
			// Overwrite the suffix
			sprintf(member_name + strlen(member_name) - suffix_len, "(%d)", suffix);
			printf("Trying again as %s\n", member_name);
			result = handle_register_req(client_udp_port);
			suffix++;
		}
	}
    
	return 0;
}

/* This function is to be used when we detect that the server has gone down, and
   attempts to reconnect to a new server. Note that if we're not using the
   location server that we don't have any alternatives to connect to. */
void reconnect() {
	connect_retries = MAX_RETRIES;
	while(connect_retries != 0) {
		// If connect_retries is -1, then we retry indefinitely.
		// Attempt to reconnect by reinitializing all connections
		printf("Reconnecting in 5...");
		if(connect_retries > 0) {
			printf(" Attempts remaining: %d", connect_retries);
		}
		printf("\n");
		
		int i;
		for(i = 5; i > 0; i--) {
			sleep(1);
		}
		
		close(udp_socket_fd);
		if(init_client() == 0) {
			// Successfully reconnected.
			if(strlen(last_channel) > 0) {
				// Attempt to rejoin last channel.
				int result = handle_switch_room_req(last_channel);
				if(result == -1) {
					// Let the user know that they're not in a room
					printf("ATTENTION: Could not rejoin previous room.\n");
					printf("Use !r to list the new server's rooms.\n");
					memset(last_channel, 0, MAX_ROOM_NAME_LEN);
				} else if(result == -2) {
					// In case we somehow disconnect instantly
					connect_retries = MAX_RETRIES;
					continue;
				}
			}
			
			return;
		}
		
		if(connect_retries > 0) {
			connect_retries--;
		}
	}
	
	printf("Server connection failed. Exiting now.\n");
	shutdown_clean();
}



void handle_chatmsg_input(char *inputdata)
{
	/* inputdata is a pointer to the message that the user typed in.
	 * This function should package it into the msgdata field of a chat_msghdr
	 * struct and send the chat message to the chat server.
	 */


	int totalsize = MAX_MSG_LEN + sizeof(struct chat_msghdr);
	char *buf = (char *)malloc(totalsize);
  
	if (buf == 0) {
		printf("Could not malloc memory for message buffer\n");
		shutdown_clean();
		exit(1);
	}

	bzero(buf, totalsize);

	/**** YOUR CODE HERE ****/
	u_int16_t size = strlen(inputdata) + sizeof(struct chat_msghdr) + 1;
	struct chat_msghdr *msg = (struct chat_msghdr *) buf;
	
	msg->sender.member_id = htons(member_id);
	msg->msg_len = htons(size);
	strncpy((char*)msg->msgdata, inputdata, MAX_MSG_LEN);

	// send to udp server
	if(sendto(udp_socket_fd, buf, size, 0, (struct sockaddr*)&server_udp_addr, udp_addr_len) < 0) {
		perror("Sending message failed");
	}
	free(buf);
	return;
}


/* This should be called with the leading "!" stripped off the original
 * input line.
 * 
 * You can change this function in any way you like.
 *
 */
void handle_command_input(char *line)
{
	char cmd = line[0]; /* single character identifying which command */
	int len = 0;
	int goodlen = 0;
	int result;

	line++; /* skip cmd char */

	/* 1. Simple format check */

	switch(cmd) {

	case 'r':
	case 'q':
		if (strlen(line) != 0) {
			printf("Error in command format: !%c should not be followed by anything.\n",cmd);
			return;
		}
		break;

	case 'c':
	case 'm':
	case 's':
		{
			int allowed_len = MAX_ROOM_NAME_LEN;

			if (line[0] != ' ') {
				printf("Error in command format: !%c should be followed by a space and a room name.\n",cmd);
				return;
			}
			line++; /* skip space before room name */

			len = strlen(line);
			goodlen = strcspn(line, " \t\n"); /* Any more whitespace in line? */
			if (len != goodlen) {
				printf("Error in command format: line contains extra whitespace (space, tab or carriage return)\n");
				return;
			}
			if (len > allowed_len) {
				printf("Error in command format: name must not exceed %d characters.\n",allowed_len);
				return;
			}
		}
		break;

	default:
		printf("Error: unrecognized command !%c\n",cmd);
		return;
		break;
	}

	/* 2. Passed format checks.  Handle the command */

	switch(cmd) {

	case 'r':
		result = handle_room_list_req((char)TRUE);
		break;

	case 'c':
		result = handle_create_room_req(line);
		break;

	case 'm':
		result = handle_member_list_req(line);
		break;

	case 's':
		result = handle_switch_room_req(line);
		break;

	case 'q':
		result = handle_quit_req(); // does not return. Exits.
		break;

	default:
		printf("Error !%c is not a recognized command.\n",cmd);
		break;
	}

	if(result == -2) {
		// -2 means we had a server error.
		reconnect();
	}
	
	return;
}

void get_user_input()
{
	char *buf = user_input_buffer;
	char *result_str;

	while(TRUE) {
		if (!ready_to_read) {
			pthread_mutex_lock(&read_mutex);

			bzero(buf, MAX_MSGDATA);

			printf("\n[%s]>  ",member_name);

			result_str = fgets(buf,MAX_MSGDATA,stdin);

			if (result_str == NULL) {
				printf("Error or EOF while reading user input.  Guess we're done.\n");
				break;
			}

			ready_to_read = (char)TRUE;
			pthread_mutex_unlock(&read_mutex);
		} else {
			usleep(10000);
		}
	}
  
}

void heartbeat()
{
	int result = handle_room_list_req((char)FALSE);
	if (result == -2) {
		//server error
		reconnect();
	}
}


void handle_input()
{
	int count = 0;
	while(TRUE){
		if (ready_to_read) {
			pthread_mutex_lock(&read_mutex);
			// need to make sure input valid
			char *buf =user_input_buffer;
			/* Check if control message or chat message */
			if (buf[0] == '!') {
				/* buf probably ends with newline.  If so, get rid of it. */
				int len = strlen(buf);
				if (buf[len-1] == '\n') {
					buf[len-1] = '\0';
				}
				handle_command_input(&buf[1]);
		  
			} else {
				if(strlen(last_channel) > 0) {
					handle_chatmsg_input(buf);
				} else {
					printf("Error: not connected to a channel.\n");
					printf("Use '!s room_name' to join a channel.\n");
				}
			}

			ready_to_read = (char)FALSE;
			pthread_mutex_unlock(&read_mutex);
		} else {
			usleep(10000);
			count += 1;
			if (count >= 1000) {
				heartbeat();
				count = 0;
			}
		}
	}
}

int main(int argc, char **argv)
{
	pthread_t get_input_t;

	char option;
	user_input_buffer = (char *)malloc(MAX_MSGDATA);
	ready_to_read = (char)FALSE;
 
	while((option = getopt(argc, argv, option_string)) != -1) {
		switch(option) {
		case 'h':
			strncpy(server_host_name, optarg, MAX_HOST_NAME_LEN);
			break;
		case 't':
			server_tcp_port = atoi(optarg);
			break;
		case 'u':
			server_udp_port = atoi(optarg);
			break;
		case 'n':
			strncpy(member_name, optarg, MAX_MEMBER_NAME_LEN);
			break;
		default:
			printf("invalid option %c\n",option);
			usage(argv);
			break;
		}
	}
	
#ifdef USE_LOCN_SERVER

	printf("Using location server to retrieve chatserver information\n");

	if (strlen(member_name) == 0) {
		usage(argv);
	}

#else

	if(server_tcp_port == 0 || server_udp_port == 0 ||
	   strlen(server_host_name) == 0 || strlen(member_name) == 0) {
		usage(argv);
	}

#endif /* USE_LOCN_SERVER */

	signal(SIGINT, handle_int);

	cmh_size = sizeof(struct control_msghdr);
	udp_addr_len = sizeof(server_udp_addr);
	memset(last_channel, 0, MAX_ROOM_NAME_LEN);
	
	// We get an extra reconnect attempt for our first connect.
	if(init_client() != 0) {
		reconnect();
	}

	if(pthread_create(&get_input_t, NULL, (void *)get_user_input, NULL) != 0) {
		perror("Failed to create user input thread.");
	} else {
		handle_input();
	}

	return 0;
}
