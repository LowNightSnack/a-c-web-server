#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>

#define BUF_SIZE 1024

const char OK_RESPONSE[] = "HTTP/1.1 200 OK\r\n\r\n";
const char NOT_FOUND_RESPONSE[] = "HTTP/1.1 404 NOT FOUND\r\n\r\n";

int handle_http_request(int fd);

int main() {
	// Disable output buffering
	setbuf(stdout, NULL);

	// You can use print statements as follows for debugging, they'll be visible when running tests.
	printf("Logs from your program will appear here!\n");

	// Uncomment this block to pass the first stage

	int server_fd, client_addr_len;
	struct sockaddr_in client_addr;

	server_fd = socket(AF_INET, SOCK_STREAM, 0);
	if (server_fd == -1) {
		printf("Socket creation failed: %s...\n", strerror(errno));
	  return 1;
	}
	//
	// // Since the tester restarts your program quite often, setting REUSE_PORT
	// // ensures that we don't run into 'Address already in use' errors
	int reuse = 1;
	if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEPORT, &reuse, sizeof(reuse)) < 0) {
		printf("SO_REUSEPORT failed: %s \n", strerror(errno));
		return 1;
	}

	struct sockaddr_in serv_addr = { .sin_family = AF_INET ,
									 .sin_port = htons(4221),
									 .sin_addr = { htonl(INADDR_ANY) },
									};

	if (bind(server_fd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) != 0) {
		printf("Bind failed: %s \n", strerror(errno));
		return 1;
	}

	int connection_backlog = 5;
	if (listen(server_fd, connection_backlog) != 0) {
		printf("Listen failed: %s \n", strerror(errno));
		return 1;
	}

	printf("Waiting for a client to connect...\n");
	client_addr_len = sizeof(client_addr);

	int new_fd = accept(server_fd, (struct sockaddr *) &client_addr, &client_addr_len);
  if (new_fd < 0) {
    printf("Accepting connection failed %s \n", strerror(errno));
    return 1;
  }
 
	printf("Client connected\n");

  int status = handle_http_request(new_fd);

  if (status != 0) {
    printf("Handling http request failed %s \n", strerror(errno));
    return 1;
  }

  close(server_fd);

	return 0;
}

int handle_http_request(int fd) {
  char buffer[BUF_SIZE];

  memset(buffer, 0, BUF_SIZE);

  ssize_t buffer_read = recv(fd, buffer, 1024, 0);
  if (buffer_read < 0) {
    printf("Receiving data failed %s \n", strerror(errno));
    return 1;
  }

  printf("Received data from the client: %s\n", buffer);

  strtok(buffer, " ");
  char *path = strtok(NULL, " ");
  if (path == NULL) {
    printf("Reading path failed %s \n", strerror(errno));
    return 1;
  }
  
  ssize_t bytes_sent;

  if (strcmp(path, "/") == 0) {
    bytes_sent = send(fd, OK_RESPONSE, strlen(OK_RESPONSE), 0);
  } else if (strncmp(path, "/echo", 5) == 0) {
    strtok(path, "/");
    char *echo_str = strtok(NULL, "/");
    int echo_str_len = strlen(echo_str);

    char response[70];
    sprintf(response, "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: %d\r\n\r\n%s", echo_str_len, echo_str);
    bytes_sent = send(fd, response, strlen(response), 0);
  } else {
    bytes_sent = send(fd, NOT_FOUND_RESPONSE, strlen(NOT_FOUND_RESPONSE), 0);
  }

  if (bytes_sent < 0) {
    printf("Sending data failed %s \n", strerror(errno));
    return 1;
  }
}

