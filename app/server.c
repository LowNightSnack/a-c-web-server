#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <pthread.h>

#define BUF_SIZE 1024

char* directory = NULL;

const char OK_RESPONSE[] = "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: 6\r\n\r\nSuccess";
const char NOT_FOUND_RESPONSE[] = "HTTP/1.1 404 NOT FOUND\r\nContent-Type: text/plain\r\nContent-Length: 9\r\n\r\nNot Found";

void* handle_connection(void* arg);
int handle_http_request(int fd);

int main(int argc, char *argv[]) {
	// Disable output buffering
	setbuf(stdout, NULL);

	// You can use print statements as follows for debugging, they'll be visible when running tests.
	printf("Logs from your program will appear here!\n");

	// Uncomment this block to pass the first stage

  for (int i = 0; i < argc; i++) {
    if (strcmp(argv[i], "--directory") == 0) {
      directory = argv[i+1];
      break;
    }
  }

	int server_fd;

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
  while(1) {
    struct sockaddr_in client_addr;
    int client_addr_len = sizeof(client_addr);
    int client_fd = accept(server_fd, (struct sockaddr*) &client_addr, &client_addr_len);
    if (client_fd < 0) {
      printf("Accepting connection failed %s \n", strerror(errno));
      return -1;
    }
    printf("Client connected\n");
    int* p_client_fd = malloc(sizeof(int));
    *p_client_fd = client_fd;
    pthread_t tid;
    pthread_create(&tid, NULL, handle_connection, (void *)p_client_fd);
  }

  close(server_fd);

	return 0;
}

void* handle_connection(void* arg) {
  int fd = *(int*)arg;
  free(arg);
  int status = handle_http_request(fd);
}

int handle_http_request(int fd) {
  char buf[BUF_SIZE];

  memset(buf, 0, BUF_SIZE);

  ssize_t buffer_read = recv(fd, buf, 1024, 0);
  if (buffer_read < 0) {
    printf("Receiving data failed %s \n", strerror(errno));
    return -1;
  }

  printf("Received data from the client: %s\n", buf);

  char* buffer = strdup(buf);
  strtok(buffer, " ");
  char* path = strtok(NULL, " ");
  if (path == NULL) {
    printf("Reading path failed %s \n", strerror(errno));
    return -1;
  }
  
  ssize_t bytes_sent;

  if (strcmp(path, "/") == 0) {
    bytes_sent = send(fd, OK_RESPONSE, strlen(OK_RESPONSE), 0);
  } else if (strncmp(path, "/echo", 5) == 0) {
    strtok(path, "/");
    char* echo_str = strtok(NULL, "/");
    int echo_str_len = strlen(echo_str);

    char response[70];
    sprintf(response, "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: %d\r\n\r\n%s", echo_str_len, echo_str);
    bytes_sent = send(fd, response, strlen(response), 0);
  } else if (strncmp(path, "/user-agent", 11) == 0) {
    char* buf_dup = strdup(buf);
    char* header_line = strtok(buf_dup, "\r\n");
    char* user_agent_str = NULL;
    int user_agent_str_len = 0;
    
    while (header_line != NULL) {
      if (strncmp(header_line, "User-Agent", 10) == 0) {
        strtok(header_line, " ");
        user_agent_str = strtok(NULL, " ");
        user_agent_str_len = strlen(user_agent_str);
        break;
      }
      header_line = strtok(NULL, "\r\n");
    }
    
    if (user_agent_str == NULL) {
      printf("Parsing user agent failed %s \n", strerror(errno));
      return -1;
    }
    
    char response[80];
    sprintf(response, "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: %d\r\n\r\n%s", user_agent_str_len, user_agent_str);
    bytes_sent = send(fd, response, strlen(response), 0);
  } else if (strncmp(path, "/files", 6) == 0) {
    strtok(path, "/");
    char* filename = strtok(NULL, "/");
    char file_path[strlen(directory) + strlen(filename) + 1];
    sprintf(file_path, "%s/%s", directory, filename);
    if (access(file_path, F_OK) == 0) {
      FILE* f;
      f = fopen(file_path, "rb");
      fseek(f, 0, SEEK_END);
      long file_size = ftell(f);
      fseek(f, 0, SEEK_SET);
      char* file_buffer = malloc(file_size);
      fread(file_buffer, 1, file_size, f);
      fclose(f);

      char response[1024];
      sprintf(response, "HTTP/1.1 200 OK\r\nContent-Type: application/octet-stream\r\nContent-Length: %ld\r\n\r\n%s", strlen(file_buffer), file_buffer);
      bytes_sent = send(fd, response, strlen(response), 0);
    } else {
      bytes_sent = send(fd, NOT_FOUND_RESPONSE, strlen(NOT_FOUND_RESPONSE), 0);
    }
  } else {
    bytes_sent = send(fd, NOT_FOUND_RESPONSE, strlen(NOT_FOUND_RESPONSE), 0);
  }

  if (bytes_sent < 0) {
    printf("Sending data failed %s \n", strerror(errno));
    return -1;
  }
 
  return 0;
}

