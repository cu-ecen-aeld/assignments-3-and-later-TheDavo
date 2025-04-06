#include <arpa/inet.h>
#include <asm-generic/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <syslog.h>
#include <unistd.h>

#define BACKLOG 10
#define PORT "9000"
#define AESDFILE "/var/tmp/aesdsocketdata"
#define BUFSIZE 1024

// globals for sig handling
struct addrinfo hints;
struct addrinfo *res;

int sockfd;
int clientfd;

FILE *aesdfile;

void sighandler(int signo) {
  syslog(LOG_INFO, "Caught signal, exiting");

  if (aesdfile != NULL) {
    fclose(aesdfile);
    remove(AESDFILE);
  }

  freeaddrinfo(res);
  shutdown(clientfd, SHUT_RDWR);
  shutdown(sockfd, SHUT_RDWR);

  closelog();
}

int main() {

  struct sigaction sa;

  sa.sa_handler = sighandler;

  sigaction(SIGINT, &sa, NULL);
  sigaction(SIGTERM, &sa, NULL);

  openlog("aesdsocket", 0, 0);

  // from beej's guide to sockets
  // https://beej.us/guide/bgnet/html/split/system-calls-or-bust.html#system-calls-or-bust

  memset(&hints, 0, sizeof hints);
  // do not care if IPv4 or IPv6
  hints.ai_family = AF_UNSPEC;
  // TCP stream sockets
  hints.ai_socktype = SOCK_STREAM;
  // fill in my IP for me
  hints.ai_flags = AI_PASSIVE;

  int status;
  if ((status = getaddrinfo(NULL, PORT, &hints, &res)) != 0) {
    syslog(LOG_ERR, "Error getting addrinfo");
    closelog();
    return (-1);
  }

  sockfd =
      socket(res->ai_family, res->ai_socktype, res->ai_protocol);
  if (sockfd == -1) {
    syslog(LOG_ERR, "Error on getting socket file descriptor");
    freeaddrinfo(res);
    closelog();
    return (-1);
  }

  // get rid of "Adress already in use" error message
  int yes = 1;
  if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes) == -1) {
    syslog(LOG_ERR, "Error on setsockopt");
    freeaddrinfo(res);
    closelog();
    close(sockfd);
    return (-1);
  }

  if (bind(sockfd, res->ai_addr, res->ai_addrlen) == -1) {
    syslog(LOG_ERR, "Error on binding socket");
    freeaddrinfo(res);
    closelog();
    close(sockfd);
    return (-1);
  }

  if (listen(sockfd, BACKLOG) == -1) {
    syslog(LOG_ERR, "Error on listen");
    freeaddrinfo(res);
    closelog();
    close(sockfd);
    return (-1);
  }

  // now can accept incoming connections

  struct sockaddr_storage inc_addr;
  socklen_t inc_addr_size = sizeof inc_addr;

  clientfd = accept(sockfd, (struct sockaddr *)&inc_addr, &inc_addr_size);
  if (clientfd == -1) {
    syslog(LOG_ERR, "Error on accepting first client");
    return (-1);
  }
  getpeername(clientfd, (struct sockaddr *)&inc_addr, &inc_addr_size);

  // TODO: log message showing "Accepted connection from xxx"

  // receive messages
  char *buf = malloc(sizeof(char) * BUFSIZE);
  if (buf == NULL) {
    syslog(LOG_ERR, "Error on mallocing buffer for reading");
    freeaddrinfo(res);
    closelog();
    close(sockfd);
    close(clientfd);
    return (-1);
  }

  // create file to read/write to
  aesdfile = fopen(AESDFILE, "a+");
  if (aesdfile == NULL) {
    syslog(LOG_ERR, "Error on creating aesdfile");
    freeaddrinfo(res);
    closelog();
    close(sockfd);
    close(clientfd);
    return (-1);
  }

  int read_bytes = 0;
  char *line = NULL;
  size_t len = 0;
  ssize_t read_line_count;
  while (1) {
    read_bytes = recv(clientfd, buf, BUFSIZE, 0);
    if (read_bytes == -1) {
      syslog(LOG_ERR, "Error on reading from recv");
      return (-1);
    }

    if (read_bytes == 0) {
      syslog(LOG_INFO, "Client has shutdown/not accepting reads or sends");
      int clientfd =
          accept(sockfd, (struct sockaddr *)&inc_addr, &inc_addr_size);
      if (clientfd == -1) {
        syslog(LOG_ERR, "Error on accepting new client");
        return (-1);
      }
    }
    char *packet = strtok(buf, "\n");
    syslog(LOG_DEBUG, "packet: %s", packet);

    fprintf(aesdfile, "%s\n", packet);

    // all packets have been read in, time to send the file content back to
    // client
    while ((read_line_count = getline(&line, &len, aesdfile)) != -1) {
      send(clientfd, &line, read_line_count, 0);
      printf("%s", packet);
    }
  }

  freeaddrinfo(res);
  shutdown(clientfd, SHUT_RDWR);
  shutdown(sockfd, SHUT_RDWR);
  fclose(aesdfile);
  remove(AESDFILE);
  return (0);
}
