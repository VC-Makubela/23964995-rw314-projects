/**
  * @file logger.h
  * @description A definition of functions offered by the logger
  */

#ifndef LOGGER_H
#define LOGGER_H

#include "proc_structs.h"

/* Functions */
void log_msg(char *msg);
void log_unknown_instr(char *proc_name);
void log_no_instr(char *proc_name);

void log_request_acquired(char* proc_name, char* resource_name);
void log_request_waiting(char* proc_name, char* resource_name);
void log_ready(char* proc_name);
void log_release_released(char* proc_name, char* resource_name);
void log_release_error(char* proc_name, char* resource_name);
void log_wake_up(char* proc_name, char* resource_name);
void log_terminated(char *proc_name);
void log_send(char *proc_name, char* msg, char* mailbox);
void log_recv(char *proc_name, char* msg, char* mailbox);
void log_acquired_lock(char *lock_name);
void log_released_lock(char *lock_name);
void log_deadlock_detected(void);
void log_blocked_procs(void);

void log_avail_resources(struct resource_t *resources_ll);
void log_alloc_resources(pcb_t *proc, struct resource_t *resource_ll);
void log_pcbs(char *msg, pcb_t *pcb);
void log_queue(pcb_t *first_pcb, char *msg);
void log_running(pcb_t *proc, int rank);
void log_instruction(instr_t *instr);
#endif
