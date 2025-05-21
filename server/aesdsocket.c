#include <arpa/inet.h>
#include <asm-generic/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/syslog.h>
#include <sys/types.h>
#include <syslog.h>
#include <unistd.h>

#define BACKLOG 10
#define PORT "9000"
#define AESDFILE "/var/tmp/aesdsocketdata"
#define BUFSIZE 1024

volatile sig_atomic_t shutdown_flag = 0;

struct file_with_lock {
  pthread_mutex_t file_lock;
  FILE *file;
};

void free_file_with_lock(struct file_with_lock *fwl) {
  pthread_mutex_destroy(&(fwl->file_lock));
  if (fwl->file != NULL) {
    fclose(fwl->file);
  }
  free(fwl);
  fwl = NULL;
}

typedef struct client_thread_node {
  int clientfd;
  pthread_t thread;
  bool thread_exited;
  bool thread_err;
  char ipstr[INET6_ADDRSTRLEN];
  struct file_with_lock *fwl;

  struct client_thread_node *next;

} client_thread_node_t;

client_thread_node_t *new_client_thread_node(int clientfd, pthread_t thread,
                                             char ipstr[INET6_ADDRSTRLEN],
                                             struct file_with_lock *fwl) {
  client_thread_node_t *new_node = malloc(sizeof(client_thread_node_t));
  if (new_node == NULL) {
    syslog(LOG_ERR, "Error allocating memory for new client thread node");
    return NULL;
  }
  new_node->clientfd = clientfd;
  new_node->thread = thread;
  new_node->thread_exited = false;
  new_node->thread_err = false;
  new_node->fwl = fwl;
  strcpy(new_node->ipstr, ipstr);

  return new_node;
}

void llist_remove_at(client_thread_node_t **head, int pos);
void llist_remove_by_pthread(client_thread_node_t **head, pthread_t thread) {
  client_thread_node_t *current = *head;
  client_thread_node_t *previous= *head;

  if (*head == NULL) {
    syslog(LOG_INFO, "Client thread list is empty");
  } else if ((*head)->thread == thread) {
    *head = current->next;
    free(current);
    current = NULL;
  } else {
    while (current != NULL && current->thread != thread) {
      previous = current;
      current = current->next;
    }
    if (current !=  NULL) {
      // found the node with the pthread needed
      previous->next = current->next;
      free(current);
      current = NULL;
    } else {
      syslog(LOG_ERR, "Thread %lu not found", thread);
    }
  }
}
void llist_add_to_end(client_thread_node_t *head,
                      client_thread_node_t *new_node) {
  if (head == NULL) {
    return;
  }

  client_thread_node_t *curr = head;
  while (curr->next != NULL) {
    curr = curr->next;
  }

  curr->next = new_node;
}

void *handle_connection(void *client_thread_arg) {
  client_thread_node_t *node = (client_thread_node_t *)client_thread_arg;

  // receive messages
  char *buf = malloc(sizeof(char) * BUFSIZE);
  if (buf == NULL) {
    syslog(LOG_ERR, "Error on mallocing buffer for reading");
    node->thread_err = true;
    pthread_exit(NULL);
  }

  if (node->fwl->file == NULL) {
    syslog(LOG_ERR, "File pointer is NULL");
    node->thread_err = true;
    pthread_exit(NULL);
  }
  int read_bytes = 0;
  char *line = NULL;
  size_t len = 0;
  ssize_t read_line_count;

  while ((read_bytes = recv(node->clientfd, buf, BUFSIZE, 0)) > 0) {
    syslog(LOG_INFO, "recv bytes %d", read_bytes);

    char *newline_pos = (char *)memchr(buf, '\n', read_bytes);

    // found a newline in the buffer, write to the file and then
    // send file contents

    // LOCK MUTEX
    // this is where the file contents are written to, so mutex lock that
    pthread_mutex_lock(&(node->fwl->file_lock));
    if (newline_pos != NULL) {

      // the +1 is there to include the newline character from the buffer
      fwrite(buf, sizeof(char), newline_pos - buf + 1, node->fwl->file);
      // fflush is here to force the file to be written and not stored
      // in the kernel buffer
      fflush(node->fwl->file);

      rewind(node->fwl->file);
      while ((read_line_count = getline(&line, &len, node->fwl->file)) != -1) {
        syslog(LOG_INFO, "Sending line %s", line);
        send(node->clientfd, line, read_line_count, 0);
      }
      free(line);
    } else {
      // no newline character found, add whole buffer to file
      syslog(LOG_INFO, "Adding all content to file");
      fwrite(buf, sizeof(char), read_bytes, node->fwl->file);
      fflush(node->fwl->file);
      syslog(LOG_INFO, "All content added");
    }
    // UNLOCK MUTEX
    pthread_mutex_unlock(&(node->fwl->file_lock));
  }

  node->thread_exited = true;
  free(buf);
  buf = NULL;
  if (read_bytes == 0) {
    syslog(LOG_INFO, "Closed connection from %s", node->ipstr);
  }
  if (read_bytes == -1) {
    node->thread_err = true;
  }
  pthread_exit(NULL);
}

void raise_shutdown_flag(int signo) {
  syslog(LOG_INFO, "Caught signal, exiting");
  shutdown_flag = 1;
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
      .sa_handler = &raise_shutdown_flag,
      // .sa_mask = {0},
      // .sa_flags = 0
  };
  sigaction(SIGINT, &sa, NULL);
  // avoid signal blocking issues
  sigemptyset(&sa.sa_mask);
  sigaction(SIGTERM, &sa, NULL);

  openlog("aesdsocket", LOG_PID, LOG_USER);
  int sockfd;
  int clientfd;
  struct addrinfo hints;
  struct addrinfo *res;

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
    chdir("/");
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

  struct file_with_lock *fwl = malloc(sizeof(struct file_with_lock));
  if (pthread_mutex_init(&(fwl->file_lock), NULL) < 0) {
    syslog(LOG_ERR, "Error initializing mutex");
    return (-1);
  }
  // create file to read/write to
  fwl->file = fopen(AESDFILE, "a+");
  if (fwl->file == NULL) {
    syslog(LOG_ERR, "Error on creating aesdfile");
    freeaddrinfo(res);
    closelog();
    close(sockfd);
    return (-1);
  }

  client_thread_node_t *ll_head = NULL;

  // shutdown_flag is raised when SIGINT or SIGTERM is raised
  // this way the while loop has a way to exit
  while (!shutdown_flag) {
    clientfd = accept(sockfd, (struct sockaddr *)&inc_addr, &inc_addr_size);
    if (clientfd == -1) {
      if (shutdown_flag)
        break;
      syslog(LOG_ERR, "Error on accepting client");
      continue; // continue trying to accept clients;
    }

    // accepted a client, log the client IP
    char ipstr[INET6_ADDRSTRLEN];
    struct sockaddr_in *s = (struct sockaddr_in *)&inc_addr;
    inet_ntop(AF_INET, &s->sin_addr, ipstr, sizeof ipstr);
    syslog(LOG_INFO, "Accepted connection from %s", ipstr);

    // should make a new thread here and add that to the linked list
    syslog(LOG_INFO, "Adding new thread and node\n");
    pthread_t conn_tid;
    client_thread_node_t *new_node =
        new_client_thread_node(clientfd, conn_tid, ipstr, fwl);
    pthread_create(&conn_tid, NULL, handle_connection, new_node);
    syslog(LOG_INFO, "New thread with id %lu made", conn_tid);

    if (ll_head == NULL) {
      syslog(LOG_INFO, "Assigning new head node\n");
      ll_head = new_node;
    } else {
      llist_add_to_end(ll_head, new_node);
      syslog(LOG_INFO, "Added new node to list\n");
    }

    // new client added, search through linked list for any finished threads
    // and pthread_join() them
    client_thread_node_t *current_node = ll_head;
    syslog(LOG_INFO, "Traversing linked list to join threads");
    if (current_node) {
      while (current_node != NULL) {
        if (current_node->thread_exited == true) {
          syslog(LOG_INFO, "joining thread %lu", current_node->thread);
          pthread_join(current_node->thread, NULL);
          syslog(LOG_INFO, "closing clientfd%d", current_node->clientfd);

          shutdown(current_node->clientfd, SHUT_RDWR);
          client_thread_node_t *tmp = current_node->next;
          llist_remove_by_pthread(&ll_head, current_node->thread);
          current_node = tmp;
        } else {
          // continue as normal
          current_node = current_node->next;
        }
      }
    }
  }

  syslog(LOG_INFO, "Cleaning up, exit signal caught");
  freeaddrinfo(res);
  shutdown(sockfd, SHUT_RDWR);
  closelog();
  free_file_with_lock(fwl);
  remove(AESDFILE);
  return (0);
}
