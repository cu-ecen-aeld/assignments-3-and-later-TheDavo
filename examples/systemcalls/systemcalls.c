#include "systemcalls.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

/**
 * @param cmd the command to execute with system()
 * @return true if the command in @param cmd was executed
 *   successfully using the system() call, false if an error occurred,
 *   either in invocation of the system() call, or if a non-zero return
 *   value was returned by the command issued in @param cmd.
 */
bool do_system(const char *cmd) {

  /*
   * TODO  add your code here
   *  Call the system() function with the command set in the cmd
   *   and return a boolean true if the system() call completed with success
   *   or false() if it returned a failure
   */

  int retval = system(cmd);
  if (retval == 0) {
    return true;
  } else {
    printf("Error calling cmd %s\n", cmd);
    printf("Error no: %d\n", errno);
    return false;
  }
}

/**
 * @param count -The numbers of variables passed to the function.
 *   The variables are command to execute.
 *   followed by arguments to pass to the command
 *   Since exec() does not perform path expansion, the command to execute needs
 *   to be an absolute path.
 * @param ... - A list of 1 or more arguments after the @param count argument.
 *   The first is always the full path to the command to execute with execv()
 *   The remaining arguments are a list of arguments to pass to the command
 *   in execv()
 * @return true if the command @param ... with arguments @param arguments
 *   were executed successfully
 *   using the execv() call
 *   false if an error occurred, either in invocation
 *   of the fork, waitpid, or execv() command, or if a non-zero return value
 *   was returned by the command issued in @param arguments with the
 *   specified arguments.
 */

bool do_exec(int count, ...) {
  va_list args;
  va_start(args, count);
  char *command[count + 1];
  int i;
  for (i = 0; i < count; i++) {
    command[i] = va_arg(args, char *);
  }
  command[count] = NULL;
  // this line is to avoid a compile warning before your implementation is
  // complete and may be removed
  command[count] = command[count];

  /*
   * TODO:
   *   Execute a system command by calling fork, execv(),
   *   and wait instead of system (see LSP page 161).
   *   Use the command[0] as the full path to the command to execute
   *   (first argument to execv), and use the remaining arguments
   *   as second argument to the execv() command.
   *
   */
  pid_t p = fork();
  if (p == -1) {
    perror("error on fork()");
    return false;
  } else if (p != 0) {
    // parent process
    int status;
    pid_t child_pid = waitpid(p, &status, 0);
    if (WIFEXITED(status)) {
      // child process completed, check the status
      int err = WEXITSTATUS(status);
      if (err) {
        printf("Error calling pid %d\n", child_pid);
        printf("Error from WEXITSTATUS: %d\n", err);
        return false;
      }
    }
  } else {
    // child process
    printf("command[0]: %s\n", command[0]);
    int retval = execv(command[0], command);

    printf("Retval from execv %d\n", retval);
    printf("Error executing execv\n");
    printf("Error no: %d\n", errno);
    exit(retval);
  }

  va_end(args);

  return true;
}

/**
 * @param outputfile - The full path to the file to write with command output.
 *   This file will be closed at completion of the function call.
 * All other parameters, see do_exec above
 */
bool do_exec_redirect(const char *outputfile, int count, ...) {
  va_list args;
  va_start(args, count);
  char *command[count + 1];
  int i;
  for (i = 0; i < count; i++) {
    command[i] = va_arg(args, char *);
  }
  command[count] = NULL;
  // this line is to avoid a compile warning before your implementation is
  // complete and may be removed
  command[count] = command[count];

  /*
   * TODO
   *   Call execv, but first using https://stackoverflow.com/a/13784315/1446624
   *   as a reference, redirect standard out to a file specified by outputfile.
   *   The rest of the behaviour is same as do_exec()
   */
  int redir_fd = open(outputfile, O_WRONLY | O_TRUNC | O_CREAT, 0644);
  if (redir_fd < 0) {
    perror("Error creating file with open\n");
  }

  pid_t child_pid;
  switch (child_pid = fork()) {
  case -1:
    perror("Error using fork()");
    return false;
  case 0:
    // child process
    // redirect the standard output to the new file
    if (dup2(redir_fd, 1) < 0) {
      perror("Error redirecting file to outputfile");
    }
    close(redir_fd);
    int retval = execv(command[0], command);
    if (retval == -1) {
      printf("Error executing execv\n");
      printf("Error no: %d\n", errno);
      return false;
    }
  default:
    close(redir_fd);
    int status;
    pid_t ret_pid = waitpid(child_pid, &status, 0);
    if (WIFEXITED(status)) {
      int err = WEXITSTATUS(status);
      if (err) {
        printf("Error calling pid %d\n", ret_pid);
        printf("Error no: %d\n", errno);
        printf("Error from WEXITSTATUS: %d\n", err);
        return false;
      }
    }
  }

  va_end(args);

  return true;
}
