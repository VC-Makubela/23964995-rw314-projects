/**
 * @file parser.c
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <limits.h>

#include "proc_syntax.h"
#include "proc_parser.h"
#include "proc_structs.h"

#define READING 0
#define END_OF_FILE 2
#define NAME_SZ 5

FILE *open_process_file(char *filename);
bool_t read_processes(FILE *fptr, char *line);
bool_t read_ready_processes(FILE *fptr, char *line);
bool_t read_resources(FILE *fptr, char *line);
bool_t read_mailboxes(FILE *fptr, char *line);
int read_process(FILE *fptr, char *line);
void read_req_resource(FILE *fptr, char *line);
void read_rel_resource(FILE *fptr, char *line);
char *read_comms_send(FILE *fptr, char *line);
char *read_comms_recv(FILE *fptr, char *line);
int read_string(FILE *fptr, char *line);
unsigned short int read_number(FILE *fptr, int *number);
bool_t str_to_priority(char *string, int *priority);

/**
 * @brief Reads in a specified file, parse it and store it in the associated data-structure.
 *
 * Reads the process.list file and parse it. It reads the processes and
 * continues by reading the resources. Next the function looks for the process
 * setup and reads the request and release statements. At each stage of the
 * parsing each element is stored in the specific datastructure.
 *
 * @param filename A string with the location of the process.list file for reading.
 */
int parse_process_file(char *filename) {
  FILE *fptr = NULL;
  char line[1024];
  int status;
  int success = FALSE;

  fptr = open_process_file(filename);

  if (fptr == NULL) {
    printf("File is NULL. Exiting.");
    success = FALSE;
  } else {

    init_loader();

    read_string(fptr, line);
    if (read_processes(fptr, line)) read_string(fptr, line);
    if (read_ready_processes(fptr, line)) read_string(fptr, line);
    if (read_resources(fptr, line)) read_string(fptr, line);
    if (read_mailboxes(fptr, line)) read_string(fptr, line);
    /* Skip all the whitespaces of the next line */
    if (strcmp(line, "") == 0) read_string(fptr, line);

    /* Read the list of instructions listed for each process */
    status = READING;
    while (status != END_OF_FILE) status = read_process(fptr, line);

    dealloc_last_job_name();
    fclose(fptr);
    success = TRUE;
  }
  return success;
}

/**
 * @brief Opens the file with filename and return a pointer to the file.
 *
 * Opens a file, with filename, for read-only. If the file could not be
 * open return NULL else return the pointer to the file.
 *
 * @param filename The name of the file to open
 *
 * @return A file pointer
 */
FILE *open_process_file(char *filename) {

  FILE *file = fopen(filename, "r");

  if (file == NULL) {
    fprintf(stderr, "Error opening %s\n", filename);
  }

  return file;
}

/**
 * @brief Reads a number from a file.
 *
 * @param fptr pointer to the file
 * @param number pointer to the number to be read
 *
 * @return TRUE if a newline was reached after reading the number
 */
unsigned short int read_number(FILE *fptr, int *number)
{
  int digit, ch;

  *number = 0;
  while ((ch = fgetc(fptr)) && isdigit(ch)) {
    digit = atoi((char *) &ch);
    if (*number > (INT_MAX - digit) / 10) {
      fprintf(stderr, "number too large");
    } else {
      *number = 10 * *number + digit;
    }
  }
  return (ch != '\n');
}

/**
 * @brief Converts string to an integer if it is a number
 *
 * @param the string
 * @param the integer value of the number represented by string if it is a number
 *
 * @return TRUE if string is a number, else FALSE 
 */
bool_t str_to_priority(char *string, int *priority)
{
  bool_t success = TRUE;

  if ((isdigit(string[0]) && strlen(string) < NAME_SZ)) *priority = atoi(string);
  else {
    printf("Priority to high %s\n", string);
    success = FALSE;
  }

  return success;
}

/**
 * @brief Reads the list of processes and loads it with functions defined in
 *   data_structs.h
 *
 * Reads a line from the file referenced by fptr and expects it to be the
 * PROCESSES keyword. If true continue to reserve space for the process name
 * and repeatedly read all the processes and load it.
 *
 * Custom change: Reads in prioriy. Unfortunately segfaults if priority is not
 * present (but it was promised to always be present).
 *
 * @param fptr A pointer to the file from which to read.
 * @param line A pointer to a string read from the file.
 */
bool_t read_processes(FILE *fptr, char *line) {
  char *process_name, *nxt_string;
  int priority, not_eol;
  bool_t success = TRUE;

  /* If process list provided */
  if (strcmp(line, PROCESSES) == 0) {
    /* Read next string: process name */
    process_name = malloc(NAME_SZ * sizeof(char));
    not_eol = read_string(fptr, process_name);
    while (not_eol) {
      priority = 0;
       /* Read next string: priority or next process name */
      nxt_string = malloc(NAME_SZ * sizeof(char));
      not_eol = read_string(fptr, nxt_string);
      if (nxt_string != NULL) {
         /* If string read was a priority: assign to variable */
        if (str_to_priority(nxt_string, &priority)) {
          /* If string read was not the end of the line */
          if (not_eol) { 
            /* Read next string: process name */
            nxt_string = malloc(NAME_SZ * sizeof(char));
            not_eol = read_string(fptr, nxt_string);
          }
        } else {
          printf("no priority\n");
        }
      }
      load_process(process_name, priority);
      /* Assing process name */
      process_name = nxt_string;
    }
    success = TRUE;
  } else {
    printf("No process list provided\n");
    success = FALSE;
  }
  return success;
}

/**
 * @brief Reads the number of processes that is ready for scheduling
 *
 * Reads the number
 * defined in data_structs.h
 *
 * @param fptr A pointer to the file from which to read.
 * @param line A pointer to a string read from file.
 */
bool_t read_ready_processes(FILE *fptr, char *line) {
  bool_t success = TRUE;

  if (strcmp(line, SCHEDULE)==0) {
    int num_ready_procs = 0;
    read_number(fptr, &num_ready_procs);
#ifdef DEBUG_LOADER
    printf("#ReadyProcs: %d\n", num_ready_procs);
#endif
    load_ready_procs(num_ready_procs);
  } else {
#ifdef DEBUG_LOADER
    printf("Note: no ready processes\n");
#endif
  }

  return success;
}

/**
 * @brief Reads the list of resources and loads it with functions defined in
 *  data_structs.h
 *
 * Reads the list of resources and loads it with the load_resource function
 * defined in data_structs.h
 *
 * @param fptr A pointer to the file from which to read.
 * @param line A pointer to a string read from the file.
 */
bool_t read_resources(FILE *fptr, char *line) {
  char *resourceName;
  bool_t success = TRUE;

  /* If resource list provided */
  if (strcmp(line, RESOURCES)==0) {
    resourceName = malloc(NAME_SZ * sizeof(char));
    /* While not the last resource */
    while (read_string(fptr, resourceName) != 0) {
      /* Load resource */ 
      load_resource(resourceName);
      resourceName = malloc(NAME_SZ * sizeof(char));
    }
    /* Load last resource */
    load_resource(resourceName);
    success = TRUE;
  } else {
#ifdef DEBUG_LOADER
    printf("Note: no resource list provided\n");
#endif
    success = FALSE;
  }

  return success;
}

/**
 * @brief Reads the list of mailboxes and loads it.
 *
 * Reads the list of mailboxes and loads it with the load_mailbox function
 * defined in data_structs.h
 *
 * @param fptr A pointer to the file from which to read.
 * @param line A pointer to a string read from file.
 */
bool_t read_mailboxes(FILE *fptr, char *line) {
  char *mailboxName;
  bool_t success = TRUE;

  /* If mailbox list provided */
  if (strcmp(line, MAILBOXES)==0) {
    mailboxName = malloc(strlen(line) * sizeof(char));
    /* While not the last mailbox */
    while (read_string(fptr, mailboxName) != 0) {
      load_mailbox(mailboxName);
      mailboxName = malloc(strlen(line) * sizeof(char));
    }
    /* Load last mailbox */
    load_mailbox(mailboxName);
    success = TRUE;
  } else {
#ifdef DEBUG_LOADER
    printf("Note: no mailbox list provided\n");
#endif
    success = FALSE;
  }

  return success;
}

/**
 * @brief Reads the defined instruction for a process and loads it.
 *
 * Reads the list of instructions for each process and loads it in the
 * appropriate datastructure using the functions defined in data_structs.h
 *
 * @param fptr A pointer to the file from which to read.
 * @param line A pointer to a string read from file.
 *
 * @return s Indicates the current status of reading the process.
 */
int read_process(FILE *fptr, char *line) {
  char *resource_name;
  char *process_name;
  char *msg;
  int s;

  s = 0; /* Must test this assignment */

  if (strcmp(line, PROCESS) == 0) {
    /* reads the process name */
    process_name = malloc(sizeof(char)*3);
    read_string(fptr, process_name);
    /* 1. Use the resource_name to find the relevant pcb */
#ifdef DEBUG_LOADER
    printf("Process %s\n", process_name);
#endif
    resource_name = malloc(sizeof(char) * 64);
    while ((s = read_string(fptr, resource_name)) != 0 && s != 2) {
      if (strcmp(resource_name, REQ) == 0) {
        /* Read the REQ resource */
        read_req_resource(fptr, resource_name);
        load_instruction(process_name, REQ_OP,
                 resource_name, NULL);
        /* 2. Store instruction using the pcb pointer */
      } else if (strcmp(resource_name, REL) == 0) {
        /* Read the REL resource */
        read_rel_resource(fptr, resource_name);
        load_instruction(process_name, REL_OP,
                 resource_name, NULL);
      } else if (strcmp(resource_name, SEND) == 0) {
        /* Read the COMMS resource */
        msg = read_comms_send(fptr, resource_name);
        load_instruction(process_name, SEND_OP,
                 resource_name, msg);
      } else if (strcmp(resource_name, RECV) == 0) {
        /* Read the COMMS resource */
        msg = read_comms_recv(fptr, resource_name);
        load_instruction(process_name, RECV_OP,
                 resource_name, msg);
      } else {
        /* Execute on white spaces */
        /* Execute the while loop when encountering new lines and white 
         * spaces, exit the loop when encountering the instructions for
         * the next Process */
        if (strcmp(resource_name, "") != 0) {
          break;
        } else {
          free(resource_name);
        }
      }
      resource_name = malloc(sizeof(char)*64);
    }
    free(resource_name);
  }
  return s;
}

/**
 * @brief Reads the resource name in a request instruction.
 *
 * Uses the read_string function to read the name of the resource specified in
 * this request instruction.
 *
 * @param fptr A pointer to the file from which to read.
 * @param line A pointer to a string read from file.
 */
void read_req_resource(FILE *fptr, char *line) {
  read_string(fptr, line);
#ifdef DEBUG_LOADER
  printf("req %s\n", line);
#endif
}

/**
 * @brief Reads the resource name in a release instruction.
 *
 * Uses the read_string function to read the name of the resource specified in
 * this release instruction.
 *
 * @param fptr A pointer to the file from which to read.
 * @param line A pointer to a string read from file.
 */
void read_rel_resource(FILE *fptr, char *line) {
  read_string(fptr, line);
#ifdef DEBUG_LOADER
  printf("rel %s\n", line);
#endif
}

/**
 * @brief Reads the send instruction and the data.
 *
 * Reads all the elements from the send instruction and stores the mailbox as
 * well as the message to be loaded.
 *
 * @param fptr A pointer to the file from which to read.
 * @param line A pointer to a string read from file.
 *
 * @return message The message which the instruction will send.
 */
char *read_comms_send(FILE *fptr, char *line) {
  int ch;
  char *message;
  int index;

  message = malloc(sizeof(char) * 128);
  for (index = 0; index < 128; index++) {
    message[index] = '\0';
  }

  while ((ch = fgetc(fptr)) != '\n') {
    /* Check to make sure that the character is in the
     * range of all the ascii letters */
    if ((ch >= 65 && ch <= 90) || (ch >= 97 && ch <= 122)) {
      index = 1;
      line[0] = ch;
      while ((ch = fgetc(fptr)) != COMMA) {
        if (ch != WHITESPACE) {
          line[index] = ch;
          index++;
        }
      }
      /* Adds the termination character at the string end */
      line[index] = '\0';
      /* Remove leading whitespace */
      while (isspace(ch = fgetc(fptr)));
      /* Skip leading quotation mark */
      if (ch == '"') {
        ch = fgetc(fptr);
      }

      index = 0;
       while (ch != RIGHTBRACKET) {
        message[index] = ch;
        index++;
        ch = fgetc(fptr);
      }
      index--;
      /* Remove trailing whitespace */
      while (isspace(message[index])) {
        message[index] = '\0';
        index--;
      }
      /* Remove trailing quotation mark */
      if (message[index] == '"') {
        message[index] = '\0';
      }
    }
  }
#ifdef DEBUG_LOADER
  printf("send (%s, %s)\n", line, message);
#endif
  return message;
}

/**
 * @brief Reads the receive instruction and the data.
 *
 * Reads all the elements from the receive instruction and stores the mailbox as
 * well as the message to be loaded. In terms of the receive instruction, the
 * message is the variable in which to receive a message from the specified
 * mailbox. In order to conform to the specification, it has been included as a
 * read message.
 *
 * @param fptr A pointer to the file from which to read.
 * @param line A pointer to a string read from file.
 *
 * @return message A placeholder for the variable which receives the message.
 */
char *read_comms_recv(FILE *fptr, char *line) {
  int ch;
  char *message;
  int index;

  message = malloc(sizeof(char) * 128);
  for (index = 0; index < 128; index++) {
    message[index] = '\0';
  }

  while ((ch = fgetc(fptr)) != '\n') {
    /* Check to make sure that the character is in the
    * range of all the ascii letters */
    if ((ch >= 65 && ch <= 90) || (ch >= 97 && ch <= 122)) {
      index = 1;
      line[0] = ch;
      while ((ch = fgetc(fptr)) != COMMA) {
        if (ch != WHITESPACE) {
          line[index] = ch;
          index++;
        }
      }
      line[index] = '\0';
      index = 0;
      while ((ch = fgetc(fptr)) != RIGHTBRACKET) {
        message[index] = ch;
        index++;
      }
    }
  }
#ifdef DEBUG_LOADER
  printf("recv (%s, %s)\n", line, message);
#endif
  return message;
}

/**
 * @brief Reads the next string.
 *
 * Reads the file character for character and constructs a string until a white
 * space or termination character is matched.
 *
 * @param fptr A pointer to the file from which to read.
 * @param line A pointer to space where the string can be stored.
 *
 * return status The status indicates when the END_OF_FILE or NEW LINE has
 * been reached.
 */
int read_string(FILE *fptr, char *line) {
  int index = 0;
  int ch = 0;
  int status = 1;

  ch = fgetc(fptr);
  while (ch != '\n' && ch != ' ') {
    if (ch == EOF) {
      status = END_OF_FILE;
      break;
    }
    line[index] = ch;
    index++;
    ch = fgetc(fptr);
    status = (ch == '\n' ? 0 : 1);
  }
  line[index] = '\0';

  return status;
}
