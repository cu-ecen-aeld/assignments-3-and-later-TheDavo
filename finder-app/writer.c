#include <syslog.h>
#include <stdlib.h>
#include <stdio.h>

int main(int argc, char** argv){

  openlog(NULL, 0, LOG_USER);

  // check arguments are valid size
  int required_args = 2;
  
  // add one to account for the file name
  if (argc != required_args + 1 ) {
    // log error and exit early
    syslog(LOG_ERR, "Invalid number of arguments, require %d, received %d", 
           required_args, argc - 1);

    closelog();
    return 1;
  }

  char *filepath = argv[1];
  char *str_to_write = argv[2];

  FILE *writefile = fopen(filepath, "w+");

  if (writefile == NULL) {
    syslog(LOG_ERR, "Unable to open file for writing at %s", filepath);
    closelog();
    return 1;
  }

  fprintf(writefile, "%s", str_to_write);
  syslog(LOG_DEBUG, "Writing %s to %s", str_to_write, filepath);

  fclose(writefile);
  closelog();
  return 0;  
}
