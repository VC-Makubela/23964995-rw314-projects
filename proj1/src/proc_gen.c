/**
 * @file proc_gen.c
 */
#include <stdio.h> 
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <limits.h>
#include <time.h>

#include "proc_structs.h"

#define READING 0
#define END_OF_FILE 2
#define RELEASE 1

const int MAX_NAME_SZ = 32;
const int MAX_MSG_SZ = 32;
const int MAX_PROCS = 5;
const int MAX_NEW_PROCS = 5;
const int MAX_RESOURCES = 5;
const int MAX_MAILBOXES = -1;
const int SUPPORTED_INSTR = 2;
const int MAX_INSTRS_PER_PROC = 100;

int num_resources = 5;
int num_mailboxes = 2;
int num_instructions = 4;
bool_t srand_called = FALSE;

int generate_processes(bool_t priority_sched);

void generate_instrs(char *process_name);
int generate_prio(int *pr_alloc, int nth);
char *generate_name(char type, int id);
char *generate_msg(char *msg);

/**
 * @brief Generates a random number of processes and loads them into the data structures using the loader 
 */
bool_t generate_procs(void) {
  srand(time(0));
  srand_called = TRUE;
  init_loader();
  int n = generate_processes(FALSE);

  return n ;
}

/**
 * @brief generates a list of processes and loads it with functions provided in loader.h
 *
 */
int generate_processes(bool_t priority_sched) {
  char* name;
  int i, proc_priority = 0;
  int* priorities_allocated = NULL;
  bool_t success = FALSE;

  /* Generate and load a list of processes */
  int num_processes = rand() % MAX_PROCS;
  if (num_processes < 1) num_processes = 1;
  for(i = 0; i < num_processes; i++) {
    name = generate_name('P', get_total_jobs()+1);
    if (priority_sched) proc_priority = generate_prio(priorities_allocated, i);
    success = load_process(name, proc_priority);
    generate_instrs(name);
    if (success == FALSE) fprintf(stderr, "Error generating instructions for process %s", name);
  }

  /* Generate and load a list of resources */
  num_resources = MAX_RESOURCES;
  if (num_resources < 1) num_resources = 1;
  for(i = 0; i < num_resources; i++) {
    int duplicate = rand() % 2;
    if (duplicate) name = generate_name('R', i);
    else name = generate_name('R', i + 1);
    success = load_resource(name);
    if (success == FALSE) fprintf(stderr, "Error loading resource %s", name);
  }

  /* Generate and load a list of mailboxes */
  if (MAX_MAILBOXES != -1) {
    num_mailboxes = rand() % MAX_MAILBOXES;
    if (num_mailboxes < 1) num_mailboxes = 1;
    for(i = 0; i < num_mailboxes; i++) {
      name = generate_name('m', i);
      success = load_mailbox(name);
      if (success == FALSE) fprintf(stderr, "Error loading mailbox %s", name);
    }
  }

#   ifdef DEBUG_LOADER
  printf("Generated: %d processes, %d resources, %d mailboxes\n", num_processes, num_resources, num_mailboxes);
#   endif
  return num_processes;
}

/**
 * @brief Reads the defined instruction for a process and loads it.
 *
 * Reads the list of instructions for each process and loads it in the
 * appropriate datastructure using the functions defined in loader.h
 *
 * @param process_id
 *
 */
void generate_instrs(char *process_name) {
  char *name;

  for (int i = 0; i < num_instructions; i++) {
    int instruction = random() % SUPPORTED_INSTR;
    switch (instruction) {
      case SEND_OP:
      case RECV_OP:
        name = generate_name('m', rand() % num_mailboxes);
        load_instruction(process_name, instruction, name, generate_msg(name));
        break;
      case REQ_OP:
        name = generate_name('R', rand() % num_resources);
        load_instruction(process_name, instruction, name, NULL);
        #ifdef RELEASE
        if ((rand() % 2 == 1) && (++i < num_instructions)) {
          load_instruction(process_name, REL_OP, name, NULL);
        }
        #endif
        break;
      case REL_OP:
        name = generate_name('R', rand() & num_resources);
        load_instruction(process_name, instruction, name, NULL);
      default:
        break;
    }
  }
#   ifdef DEBUG_LOADER
  print_instructions(process_name, NULL);
#   endif
}

int generate_prio(int *pr_alloc, int nth) {
  int pr = 0;
    pr = rand() % MAX_PROCS * 100 + 1;

  return pr;
}

/**
 * @brief Generates a name for a process, resource, or mailbox
 * @return A pointer to a string for the name
 */
char* generate_name(char type, int id) {
  char* name = calloc(MAX_NAME_SZ, sizeof(char));

  if (id < 10 * MAX_NAME_SZ) {
    sprintf(name, "%c%d", type, id);
  } else {
    printf("Error: name too long: id >= %d \n", 10 * MAX_NAME_SZ);
  }

  return name;
}

/**
 * @brief Generates a msg to send
 * @return A pointer to a string for the msg
 */
char *generate_msg(char *mailbox) {
  char* msg = calloc(MAX_MSG_SZ, sizeof(char));

  sprintf(msg, "Msg %s", mailbox);
  return msg;
}
