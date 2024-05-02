#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <pthread.h>

#define BUF_SIZE 2048

char* directory = NULL;

const char SEPERATOR[] = "\r\n";
const char OK_RESPONSE_200_HEAD[] = "HTTP/1.1 200 OK";
const char CREATED_RESPONSE_201_HEAD[] = "HTTP/1.1 201 CREATED";
const char NOT_FOUND_404_HEAD[] = "HTTP/1.1 404 NOT FOUND";
const char CONTENT_TYPE_HEAD[] = "Content-Type: ";
const char CONTENT_LENGTH_HEAD[] = "Content-Length: ";
const char SUCCESS_MSG[] = "Success";
const char NOT_FOUND_MSG[] = "Not Found";
const char CREATED_MSG[] = "Created";
const char TEXT_PLAIN[] = "text/plain";
const char OCTET_STREAM[] = "application/octet-stream";

const char OK_RESPONSE[] = "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: 6\r\n\r\nSuccess";
const char NOT_FOUND_RESPONSE[] = "HTTP/1.1 404 NOT FOUND\r\nContent-Type: text/plain\r\nContent-Length: 9\r\n\r\nNot Found";

void* handle_connection(void* arg);
ssize_t index_route_get(int fd);
ssize_t echo_route_get(int fd, char* echo_str);
ssize_t user_agent_route_get(int fd, char* user_agent_str);
ssize_t files_route_get(int fd, char* filepath);
ssize_t files_route_post(int fd, char* filepath, char* content);

int main(int argc, char *argv[]) {
  // look for directory flag
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
  char buf[BUF_SIZE];

  memset(buf, 0, BUF_SIZE);

  ssize_t buffer_read = recv(fd, buf, 1024, 0);
  if (buffer_read < 0) {
    printf("Receiving data failed %s \n", strerror(errno));
    return NULL;
  }

  printf("Received data from the client: %s\n", buf);

  char* buffer = strdup(buf);
  char* method = strtok(buffer, " ");
  char* path = strtok(NULL, " ");
  if (path == NULL) {
    printf("Reading path failed %s \n", strerror(errno));
    return NULL;
  }
  
  ssize_t bytes_sent;

  if (strncmp(method, "GET", 3) == 0 && strcmp(path, "/") == 0) {
    bytes_sent = index_route_get(fd);
  } else if (strncmp(method, "GET", 3) == 0 && strncmp(path, "/echo", 5) == 0) {
    strtok(path, "/");
    char* echo_str = strtok(NULL, "/");
    int echo_str_len = strlen(echo_str);

    bytes_sent = echo_route_get(fd, echo_str); 
  } else if (strncmp(method, "GET", 3) == 0 && strncmp(path, "/user-agent", 11) == 0) {
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
      return NULL;
    }
    
    bytes_sent = user_agent_route_get(fd, user_agent_str);
  } else if (strncmp(method, "GET", 3) == 0 && strncmp(path, "/files", 6) == 0) {
    strtok(path, "/");
    char* filename = strtok(NULL, "/");
    char file_path[strlen(directory) + strlen(filename) + 1];
    sprintf(file_path, "%s/%s", directory, filename);
    
    bytes_sent = files_route_get(fd, file_path);
  } else if (strncmp(method, "POST", 4) == 0 && strncmp(path, "/files", 6) == 0) {
    strtok(path, "/");
    char* filename = strtok(NULL, "/");
    char file_path[strlen(directory) + strlen(filename) + 1];
    sprintf(file_path, "%s/%s", directory, filename);
    char* buf_dup = strdup(buf);
    char* body = strstr(buf_dup, "\r\n\r\n");
    if (body) {
      body += 4;
    }
    
    bytes_sent = files_route_post(fd, file_path, body);
  } else {
    bytes_sent = send(fd, NOT_FOUND_RESPONSE, strlen(NOT_FOUND_RESPONSE), 0);
  }

  if (bytes_sent < 0) {
    printf("Sending data failed %s \n", strerror(errno));
    return NULL;
  }
 
  return NULL;
}

ssize_t index_route_get(int fd) {
  return send(fd, OK_RESPONSE, strlen(OK_RESPONSE), 0);
}

ssize_t echo_route_get(int fd, char* echo_str) {
  char response[70];
  sprintf(response, "%s%s%s%s%s%s%ld%s%s%s", OK_RESPONSE_200_HEAD, SEPERATOR, CONTENT_TYPE_HEAD, TEXT_PLAIN, SEPERATOR, CONTENT_LENGTH_HEAD, strlen(echo_str), SEPERATOR, SEPERATOR, echo_str);
  return send(fd, response, strlen(response), 0);
}

ssize_t user_agent_route_get(int fd, char* user_agent_str) {
  char response[100];
  sprintf(response, "%s%s%s%s%s%s%ld%s%s%s", OK_RESPONSE_200_HEAD, SEPERATOR, CONTENT_TYPE_HEAD, TEXT_PLAIN, SEPERATOR, CONTENT_LENGTH_HEAD, strlen(user_agent_str), SEPERATOR, SEPERATOR, user_agent_str);
  return send(fd, response, strlen(response), 0);
}

ssize_t files_route_get(int fd, char* filepath) {
  if (access(filepath, F_OK) == 0) {
    FILE* f;
    f = fopen(filepath, "rb");
    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    fseek(f, 0, SEEK_SET);
    char* file_buffer = malloc(file_size);
    fread(file_buffer, 1, file_size, f);
    fclose(f);

    char response[1024];
    sprintf(response, "%s%s%s%s%s%s%ld%s%s%s", OK_RESPONSE_200_HEAD, SEPERATOR, CONTENT_TYPE_HEAD, OCTET_STREAM, SEPERATOR, CONTENT_LENGTH_HEAD, strlen(file_buffer), SEPERATOR, SEPERATOR, file_buffer);
    return send(fd, response, strlen(response), 0);
  }
  return send(fd, NOT_FOUND_RESPONSE, strlen(NOT_FOUND_RESPONSE), 0);
}

ssize_t files_route_post(int fd, char* filepath, char* content) {
  FILE* f;
  f = fopen(filepath, "wb");
  printf("filepath: %s, body: %s\n", filepath, content);
  fwrite(content, sizeof(content[0]), strlen(content), f);
  fclose(f);

  char response[200];
  sprintf(response, "%s%s%s%s%s%s%ld%s%s%s", CREATED_RESPONSE_201_HEAD, SEPERATOR, CONTENT_TYPE_HEAD, TEXT_PLAIN, SEPERATOR, CONTENT_LENGTH_HEAD, strlen(CREATED_MSG), SEPERATOR, SEPERATOR, CREATED_MSG);

  return send(fd, response, strlen(response), 0);
}
