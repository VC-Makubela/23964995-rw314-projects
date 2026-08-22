/**
 * @file manager.h
 */
#ifndef _MANAGER_H
#define _MANAGER_H

#include "proc_structs.h"
#include "proc_gen.h"

typedef enum {PRIOR = 0, RR, FCFS} schedule_t;

// Add fields to the queue structure as needed
typedef struct pcb_queue_t {
    struct pcb_t *first;
    struct pcb_t *last;
} pcb_queue_t;

void init_system(void);
void schedule_processes(int num_thr, schedule_t algorithm, int time_quantum);
void free_manager(void);

#endif
