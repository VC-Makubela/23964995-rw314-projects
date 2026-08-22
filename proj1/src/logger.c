#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
 #ifdef _OPENMP
#include <omp.h>
#else
#include <mpi.h>
#endif
#include "logger.h"
#include "proc_structs.h"

#define DEBUG
//#define DEBUG_DEEP

/**
 * OpenMP threads have thread numbers
 * MPI processes have process ranks
 * If not OpenMP, but MPI is not initialised, use rank 0
 */
 #ifdef _OPENMP
inline int my_id() { return omp_get_thread_num();}
const char *unit = "thr";
#else
inline int my_id() { int flag, rank = 0;MPI_Initialized(&flag);if (flag) MPI_Comm_rank(MPI_COMM_WORLD, &rank);return rank;}
const char *unit = "p";
#endif

extern int errno;
int read_array_from_open_file(FILE *fptr, int *array, int *size);

// Open the output file for the calling thread/process, in append mode
// return a pointer to the open file or NULL in case of an error
FILE* open_logfile() {
	char* filename = NULL;
	FILE* fptr = NULL;

	filename = calloc(32, sizeof(char));
	if (filename) {
		sprintf(filename,"%s%d.log", unit, my_id());
		fptr = fopen(filename, "a");
		free(filename);
	}

	return fptr;
}

// Open the output file for the calling thread/process, in append mode
// return a pointer to the open file or NULL in case of an error
FILE* open_outfile() {
	char* filename = NULL;
	FILE* fptr = NULL;

	filename = calloc(32, sizeof(char));
	if (filename) {
		sprintf(filename,"%s%d.out", unit, my_id());
		fptr = fopen(filename, "a");
		free(filename);
	}

	return fptr;
}

/* Close the logfile pointed to by fptr */
void close_file(FILE* fptr) {
  fclose(fptr);
}

/* Logging request resource */
void log_request_acquired(char* proc_name, char* resource_name) {
  FILE* fptr_log = open_logfile();
  FILE* fptr_out = open_outfile();
  fprintf(fptr_log, "%s req %s: acquired\n", proc_name, resource_name);
  fprintf(fptr_out, "%s req %s: acquired\n", proc_name, resource_name);
  #ifdef DEBUG
  printf("%s req %s: acquired\n", proc_name, resource_name);
  #endif
  fflush(fptr_log);
  fflush(fptr_out);
  close_file(fptr_log);
  close_file(fptr_out);
}

void log_request_waiting(char* proc_name, char* resource_name) {
  FILE* fptr_log = open_logfile();
  FILE* fptr_out = open_outfile();
  fprintf(fptr_log, "%s req %s: waiting\n", proc_name, resource_name);
  fprintf(fptr_out, "%s req %s: waiting\n", proc_name, resource_name);
  #ifdef DEBUG
  printf("%s req %s: waiting\n", proc_name, resource_name);
  #endif
  fflush(fptr_log);
  fflush(fptr_out);
  close_file(fptr_log);
  close_file(fptr_out);
}

void log_ready(char* proc_name) {
  FILE* fptr_log = open_logfile();
  FILE* fptr_out = open_outfile();
  fprintf(fptr_log, "%s: ready\n", proc_name);
  fprintf(fptr_out, "%s: ready\n", proc_name);
  #ifdef DEBUG
  printf("%s: added to ready queue\n", proc_name);
  #endif
  fflush(fptr_log);
  fflush(fptr_out);
  close_file(fptr_log);
  close_file(fptr_out);
}

void log_unknown_instr(char* proc_name) {
  FILE* fptr = open_outfile();
  fprintf(fptr, "%s: unknown instruction\n", proc_name);
  #ifdef DEBUG
  printf("%s: unknown instruction\n", proc_name);
  #endif

  fflush(fptr);
  close_file(fptr);
}

void log_no_instr(char* proc_name) {
  FILE* fptr = open_outfile();
  fprintf(fptr, "%s: no instruction to execute\n", proc_name);
  #ifdef DEBUG
  printf("%s: no instruction to execute\n", proc_name);
  #endif

  fflush(fptr);
  close_file(fptr);
}

void log_release_released(char* proc_name, char* resource_name) {
  FILE* fptr_log = open_logfile();
  FILE* fptr_out = open_outfile();
  fprintf(fptr_log, "%s rel %s: released\n", proc_name, resource_name);
  fprintf(fptr_out, "%s rel %s: released\n", proc_name, resource_name);
  #ifdef DEBUG
  printf("%s rel %s: released\n", proc_name, resource_name);
  #endif
  fflush(fptr_log);
  fflush(fptr_out);
  close_file(fptr_log);
  close_file(fptr_out);
}

void log_wake_up(char* proc_name, char *resource_name) {
  FILE* fptr_log = open_logfile();
  fprintf(fptr_log, "Wake up process %s waiting for %s\n", proc_name, resource_name);
  fflush(fptr_log);
  close_file(fptr_log);

  #ifdef DEBUG
  printf("Wake up process %s waiting for %s\n", proc_name, resource_name);
  #endif

  FILE* fptr_out = open_outfile();
  fprintf(fptr_out, "Wake up process %s waiting for %s\n", proc_name, resource_name);
  fflush(fptr_out);
  close_file(fptr_out);
}

void log_release_error(char* proc_name, char* resource_name) {
  FILE* fptr_log = open_logfile();
  fprintf(fptr_log, "%s rel %s: error nothing to release\n", proc_name, resource_name);
  fflush(fptr_log);
  close_file(fptr_log);

  #ifdef DEBUG
  printf("%s rel %s: error nothing to release\n", proc_name, resource_name);
  #endif

  FILE* fptr_out = open_outfile();
  fprintf(fptr_out, "%s rel %s: error nothing to release\n", proc_name, resource_name);
  fflush(fptr_out);
  close_file(fptr_out);
}

void log_terminated(char *proc_name) {
  FILE* fptr_log = open_logfile();
  FILE* fptr_out = open_outfile();
  // fprintf(fptr_log, "%s terminated\n", proc_name);
  fprintf(fptr_out, "%s terminated\n", proc_name);
  #ifdef DEBUG
  printf("%s terminated\n", proc_name);
  #endif
  fflush(fptr_log);
  fflush(fptr_out);
  close_file(fptr_log);
  close_file(fptr_out);
}

void log_send(char *proc_name, char* msg, char* mailbox) {
  FILE* fptr_log = open_logfile();
  FILE* fptr_out = open_outfile();
  fprintf(fptr_log, "%s sending message%s to mailbox %s\n", proc_name, msg, mailbox);
  fprintf(fptr_out, "%s sending message%s to mailbox %s\n", proc_name, msg, mailbox);
  #ifdef DEBUG
  printf("%s sending message%s to mailbox %s\n", proc_name, msg, mailbox);
  #endif
  fflush(fptr_log);
  fflush(fptr_out);
  close_file(fptr_log);
  close_file(fptr_out);
}

void log_recv(char *proc_name, char* msg, char* mailbox) {
  FILE* fptr_log = open_logfile();
  fprintf(fptr_log, "%s received message%s from mailbox %s\n", proc_name, msg, mailbox);
  fflush(fptr_log);
  close_file(fptr_log);

  #ifdef DEBUG
  printf("%s received message%s from mailbox %s\n", proc_name, msg, mailbox);
  #endif

  FILE* fptr_out = open_outfile();
  fprintf(fptr_out, "%s received message%s from mailbox %s\n", proc_name, msg, mailbox);
  fflush(fptr_out);
  close_file(fptr_out);
}

void log_acquired_lock(char* lock_name) {
  #ifdef DEBUG_DEEP
  FILE* fptr_log = open_logfile();
  fprintf(fptr_log, "acquired %s\n", lock_name);
  fflush(fptr_log);
  close_file(fptr_log);

  printf("acquired %s\n", lock_name);

  FILE* fptr_out = open_outfile();
  fprintf(fptr_out, "acquired %s\n", lock_name);
  fflush(fptr_out);
  close_file(fptr_out);
  #endif
}

void log_released_lock(char* lock_name) {
  #ifdef DEBUG_DEEP
  FILE* fptr_log = open_logfile();
  fprintf(fptr_log, "released %s\n", lock_name);
  fflush(fptr_log);
  close_file(fptr_log);

  printf("released %s\n", lock_name);

  FILE* fptr_out = open_outfile();
  fprintf(fptr_out, "released %s\n", lock_name);
  fflush(fptr_out);
  close_file(fptr_out);
  #endif
}

void log_deadlock_detected(void) {
  FILE* fptr_log = open_logfile();
  fprintf(fptr_log, "Deadlock detected:");
  fflush(fptr_log);
  close_file(fptr_log);

  #ifdef DEBUG
  printf("Deadlock detected:");
  #endif

  FILE* fptr_out = open_outfile();
  fprintf(fptr_out, "Deadlock detected:");
  fflush(fptr_out);
  close_file(fptr_out);
}

void log_blocked_procs(void) {
  FILE* fptr_log = open_logfile();
  fprintf(fptr_log, "No deadlock detected, but blocked process(es) found:");
  fflush(fptr_log);
  close_file(fptr_log);

  #ifdef DEBUG
  printf("No deadlock detected, but blocked process(es) found:");
  #endif

  FILE* fptr_out = open_outfile();
  fprintf(fptr_out, "No deadlock detected, but blocked process(es) found:");
  fflush(fptr_out);
  close_file(fptr_out);
}

/**
 * @brief Print the names of the global resources available in the system in linked list order
 */
void log_avail_resources(struct resource_t *resource_ll)
{
  FILE *fptr = open_outfile();
  fprintf(fptr, "Available:");
  #ifdef DEBUG
  printf("Available:");
  #endif
  while (resource_ll != NULL) {
    if (resource_ll->allocated == NULL) {
      fprintf(fptr, " %s", resource_ll->name);
      #ifdef DEBUG
      printf(" %s", resource_ll->name);
      #endif
    }
    resource_ll = resource_ll->next;
  }
  fprintf(fptr, " ");
  #ifdef DEBUG
  printf(" ");
  #endif

  fflush(fptr);
  close_file(fptr);
}

/**
 * @brief Print the names of the resources allocated to <code>process</code>
 */
void log_alloc_resources(pcb_t *proc, struct resource_t *resource_ll)
{
  FILE *fptr = open_outfile();

  if (proc) {
    fprintf(fptr, "Allocated to %s:", proc->process->name);
    #ifdef DEBUG
    printf("Allocated to %s:", proc->process->name);
    #endif
    while (resource_ll != NULL) {
      if (resource_ll->allocated == proc) {
        fprintf(fptr, " %s", resource_ll->name);
        #ifdef DEBUG
        printf(" %s", resource_ll->name);
        #endif
      }
      resource_ll = resource_ll->next;
    }
    fprintf(fptr, " ");
    #ifdef DEBUG
    printf(" ");
    #endif
  }

  fflush(fptr);
  close_file(fptr);
}

/**
 * @brief Print <code>msg</code> and the names of the processes in <code>pcb</code> in linked list order.
 */
void log_pcbs(char *msg, pcb_t *pcb) {
  FILE *fptr = open_outfile();

  pcb_t *cur = pcb;
  fprintf(fptr, "%s:", msg);
  #ifdef DEBUG
  printf("%s:", msg);
  #endif
  while(cur != NULL) {
    fprintf(fptr, " %s", cur->process->name);
    #ifdef DEBUG
    printf(" %s", cur->process->name);
    #endif
    cur = cur->next;
  }
  fprintf(fptr, " ");
  #ifdef DEBUG
  printf(" ");
  #endif

  fflush(fptr);
  close_file(fptr);
}

/**
 * @brief Print <code>msg</code> and the names of the processes in <code>queue</code> in linked list order.
 */
void log_queue(pcb_t *first_pcb, char *msg)
{
  FILE *fptr = open_outfile();
  fprintf(fptr, "\n-----------------------------------");
  #ifdef DEBUG
  printf("\n-----------------------------------");
  #endif

  fflush(fptr);
  close_file(fptr);
  log_pcbs(msg, first_pcb);
}

/**
 * @brief Print <code>msg</code> and the names of the process currently running
 */
void log_running(pcb_t *proc, int rank)
{
  FILE *fptr = open_outfile();
  fprintf(fptr, "\n-----------------------------------");
  #ifdef DEBUG
  printf("\n-----------------------------------");
  #endif

  fprintf(fptr, "Running on kernel thread %d:", rank);
  #ifdef DEBUG
  printf("Running on kernel thread %d:", rank);
  #endif
  if (proc != NULL) {
    fprintf(fptr, " %s", proc->process->name);
    #ifdef DEBUG
    printf(" %s", proc->process->name);
    #endif
  }
  fprintf(fptr, " ");
  #ifdef DEBUG
  printf(" ");
  #endif

  fflush(fptr);
  close_file(fptr);
}

/**
 * @brief Print a linked list of instructions
 */
void log_instruction(instr_t *instr) {
  FILE *fptr = open_outfile();

  instr_t *tmp_instr = instr;
  while (tmp_instr != NULL) {
    switch (tmp_instr->type) {
    case REQ_OP:
      fprintf(fptr, "(req %s)\n", tmp_instr->resource_name);
      #ifdef DEBUG
      printf("(req %s)\n", tmp_instr->resource_name);
      #endif
      break;
    case REL_OP:
      fprintf(fptr, "(rel %s)\n", tmp_instr->resource_name);
      #ifdef DEBUG
      printf("(rel %s)\n", tmp_instr->resource_name);
      #endif
      break;
    case SEND_OP:
      fprintf(fptr, "(send %s %s)\n", tmp_instr->resource_name, tmp_instr->msg);
      #ifdef DEBUG
      printf("(send %s %s)\n", tmp_instr->resource_name, tmp_instr->msg);
      #endif
      break;
    case RECV_OP:
      fprintf(fptr, "(recv %s %s)\n", tmp_instr->resource_name, tmp_instr->msg);
      #ifdef DEBUG
      printf("(recv %s %s)\n", tmp_instr->resource_name, tmp_instr->msg);
      #endif
      break;
    }
    tmp_instr = tmp_instr->next;
  }

  fflush(fptr);
  close_file(fptr);
}

/* Logging msg */
void log_msg(char* msg) {
  FILE* fptr = open_outfile();
  fprintf(fptr, "%s", msg);
  #ifdef DEBUG
  printf("%s", msg);
  #endif

  fflush(fptr);
  close_file(fptr);
}
