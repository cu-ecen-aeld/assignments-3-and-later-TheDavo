#include <arpa/inet.h>
#include <asm-generic/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdbool.h>
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
  exit(0);
}

void print_usage(void) {
  printf("USAGE for aesdsocket\n");
  printf("aesdsocket [-d]\n");
  printf("OPTIONS:\n");
  printf("\t-d: run aesdsocket as a daemon\n");
}

int main(int argc, char **argv) {

  bool daemon;
  if (argc == 2 && strncmp(argv[1], "-d", 2) == 0) {
    daemon = true;
  } else if (argc == 1) {
    daemon = false;
  } else {
    print_usage();
    return (-1);
  }
  struct sigaction sa = {
    .sa_handler = sighandler,
    // .sa_mask = {0},
    // .sa_flags = 0
  };
  sigaction(SIGINT, &sa, NULL);
  // avoid signal blocking issues
  sigemptyset(&sa.sa_mask);
  sigaction(SIGTERM, &sa, NULL);

  openlog("aesdsocket", LOG_PID, LOG_USER);

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

  sockfd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
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

  // fork here if in daemon mode
  if (daemon) {
    pid_t fork_pid = fork();
    if (fork_pid == -1) {
      syslog(LOG_ERR, "Error on creating fork");
      freeaddrinfo(res);
      closelog();
      close(sockfd);
      return (-1);
    }

    // parent process, end here
    if (fork_pid != 0) {
      syslog(LOG_INFO, "Exiting as fork for parent");
      freeaddrinfo(res);
      closelog();
      close(sockfd);
      return (0);
    }

    // if not the parent process, child process will take from here
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

  while (1) {
    clientfd = accept(sockfd, (struct sockaddr *)&inc_addr, &inc_addr_size);
    if (clientfd == -1) {
      syslog(LOG_ERR, "Error on accepting client");
      continue; // continue trying to accept clients;
    }

    // accepted a client, log the client IP
    char ipstr[INET6_ADDRSTRLEN];
    struct sockaddr_in *s = (struct sockaddr_in *)&inc_addr;
    inet_ntop(AF_INET, &s->sin_addr, ipstr, sizeof ipstr);
    syslog(LOG_INFO, "Accepted connection from %s", ipstr);

    // receive messages
    char *buf = malloc(sizeof(char) * BUFSIZE);
    if (buf == NULL) {
      syslog(LOG_ERR, "Error on mallocing buffer for reading");
      freeaddrinfo(res);
      closelog();
      close(sockfd);
      close(clientfd);
      fclose(aesdfile);
      remove(AESDFILE);
      return (-1);
    }

    int read_bytes = 0;
    char *line = NULL;
    size_t len = 0;
    ssize_t read_line_count;

    while ((read_bytes = recv(clientfd, buf, BUFSIZE, 0)) > 0) {

      char *newline_pos = (char *)memchr(buf, '\n', read_bytes);

      // found a newline in the buffer, write to the file and then
      // send file contents
      if (newline_pos != NULL) {
        // the +1 is there to include the newline character from the buffer
        fwrite(buf, sizeof(char), newline_pos - buf + 1, aesdfile);
        // fflush is here to force the file to be written and not stored
        // in the kernel buffer
        fflush(aesdfile);

        rewind(aesdfile);
        while ((read_line_count = getline(&line, &len, aesdfile)) != -1) {
          send(clientfd, line, read_line_count, 0);
        }
        free(line);
      } else {
        // no newline character found, add whole buffer to file
        fwrite(buf, sizeof(char), read_bytes, aesdfile);
        fflush(aesdfile);
      }
    }

    if (read_bytes == 0) {
      syslog(LOG_INFO, "Closed connection from %s", ipstr);
      free(buf);
      buf = NULL;
    }
    if (read_bytes == -1) {
      syslog(LOG_ERR, "Error on reading from recv");
      free(buf);
      freeaddrinfo(res);
      shutdown(clientfd, SHUT_RDWR);
      shutdown(sockfd, SHUT_RDWR);
      fclose(aesdfile);
      remove(AESDFILE);
      return (-1);
    }

    if (buf != NULL) {
      free(buf);
    }
  }

  freeaddrinfo(res);
  shutdown(clientfd, SHUT_RDWR);
  shutdown(sockfd, SHUT_RDWR);
  fclose(aesdfile);
  remove(AESDFILE);
  return (0);
}
