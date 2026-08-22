/**
 * @mainpage Process Simulation - manager.c
 */

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <omp.h>

#include "proc_structs.h"
#include "proc_syntax.h"
#include "logger.h"
#include "manager.h"

/* ============================= */
/* CONSTANTS                     */
/* ============================= */

#define LOWEST_PRIORITY INT_MAX  /* 0 is highest priority, INT_MAX is lowest */
#define NOT_MAPPED      -1       /* used in deadlock detection               */

/* ============================= */
/* GLOBAL QUEUES & RESOURCES     */
/* ============================= */

static pcb_queue_t terminatedq;
static pcb_queue_t waitingq;
static pcb_queue_t readyq;
static resource_t *system_resources;

/* ============================= */
/* TERMINATION COUNTERS          */
/* ============================= */

static int total_processes  = 0;
static int waiting_count    = 0;
static int terminated_count = 0;

/* ============================= */
/* OMP LOCKS                     */
/* ============================= */

static omp_lock_t readyq_lock;
static omp_lock_t waitingq_lock;
static omp_lock_t terminatedq_lock;
static omp_lock_t resource_lock;

static int num_threads_global = 1;

/* ============================= */
/* FUNCTION DECLARATIONS         */
/* ============================= */

/* Scheduling */
bool_t terminate(void);
bool_t load_new_processes(void);
void   schedule_fcfs(void);
void   schedule_rr(int quantum);
void   schedule_priority(void);

/* Instruction execution */
int    execute_instr(pcb_t *proc);
bool_t acquire_resource(pcb_t *proc, char *resource_name);
bool_t release_resource(pcb_t *proc, char *resource_name);

/* Queue helpers */
void    enqueue(pcb_t *proc, pcb_queue_t *queue, int status);
pcb_t  *dequeue(pcb_queue_t *queue);
pcb_t  *dequeue_highest_priority(pcb_queue_t *queue);
void    remove_from_queue(pcb_queue_t *queue, pcb_t *target);
pcb_t  *find_highest_priority(pcb_queue_t *queue);
void    enqueue_ready_silent(pcb_t *pcb);

/* Deadlock */
struct pcb_t *detect_deadlock(void);

/* Argument helpers */
int   get_num_threads(int num_args, char **argv);
char *get_data(int num_args, char **argv);
int   get_algo(int num_args, char **argv);
int   get_time_quantum(int num_args, char **argv);
void  print_args(int num_thr, char *data, int sched, int tq);
void  print_queues(pcb_t *cur_pcb);

/*OTHER HELPERS*/
void clear_old_logs(int num_thr); 

/* ============================= */
/* MAIN                          */
/* ============================= */

int main(int argc, char **argv)
{
  int   num_thr      = get_num_threads(argc, argv);
  char *data         = get_data(argc, argv);
  int   scheduler    = get_algo(argc, argv);
  int   time_quantum = get_time_quantum(argc, argv);
  clear_old_logs(num_thr);

  print_args(num_thr, data, scheduler, time_quantum);

  bool_t success = FALSE;

  if (strcmp(data, "generate") == 0) {
#ifdef DEBUG_MNGR
    printf("****Generate processes and initialise the system\n");
#endif
    success = init_loader_from_generator();
  } else {
#ifdef DEBUG_MNGR
    printf("Parse process file and initialise the system: %s\n", data);
#endif
    success = init_loader_from_files(data);
    /*This prevents longterm_scheduler() from returning the same 	 PCB repeatedly*/
   // if (success) load_ready_procs(1);

  }

  if (success) {
    init_system();
    system_resources = get_resources();
    printf("***********Scheduling processes************\n");
    log_msg("***********Scheduling processes************\n");
    schedule_processes(num_thr, (schedule_t)scheduler, time_quantum);
    dealloc_data_structures();
  } else {
    printf("Error: no processes to schedule\n");
  }

  return EXIT_SUCCESS;
}

/* ============================= */
/* INIT SYSTEM                   */
/* ============================= */

/**
 * @brief The linked list of loaded processes is moved to the readyqueue.
 *        The waiting and terminated queues are intialised to empty
 */
void init_system(void)
{
  /* init locks once */
  omp_init_lock(&readyq_lock);
  omp_init_lock(&waitingq_lock);
  omp_init_lock(&terminatedq_lock);
  omp_init_lock(&resource_lock);

  readyq.first = longterm_scheduler();
  readyq.last  = NULL;

  /* TODO: Update the states of each process pcb added to readyq */
  /* TODO: Update any counters used to detect termination */
  /* TODO: Set readyq.last to point to the last pcb in the queue linked list */

  total_processes  = get_total_jobs();
  waiting_count    = 0;
  terminated_count = 0;

  pcb_t *cur = readyq.first;
  while (cur != NULL) {
    cur->state = READY;
    if (cur->next == NULL) readyq.last = cur;
    cur = cur->next;
  }

  waitingq.first = waitingq.last = NULL;
  terminatedq.first = terminatedq.last = NULL;

  //print_queues(NULL);
}

/* ============================= */
/* SCHEDULE PROCESSES DISPATCHER */
/* ============================= */

/** @brief Schedules each instruction of each process */
void schedule_processes(int num_thr, schedule_t sched_type, int quantum)
{
  num_threads_global = (num_thr > 0) ? num_thr : 1;
  omp_set_num_threads(num_threads_global);

  switch (sched_type) {
    case PRIOR: schedule_priority();      break;
    case RR:    schedule_rr(quantum);     break;
    case FCFS:  schedule_fcfs();          break;
    default:    break;
  }
}

/* ============================= */
/* TERMINATION CHECK             */
/* ============================= */

/** @brief Return true when there are no more processes to schedule */
bool_t terminate(void)
{
  int w = 0, t = 0, tot = 0;

#pragma omp atomic read
  w = waiting_count;
#pragma omp atomic read
  t = terminated_count;
#pragma omp atomic read
  tot = total_processes;

  omp_set_lock(&readyq_lock);
  int ready_empty = (readyq.first == NULL);
  omp_unset_lock(&readyq_lock);

  if (ready_empty && (w + t >= tot)) return TRUE;
  return FALSE;
}

/* ============================= */
/* LOAD NEW PROCESSES            */
/* ============================= */

/**
 * @brief Call the longterm schedule to check for new arrivals
 * If there are new arrivals, call
 *  log_pcbs("New arrivals in ready queue", new_arrivals);
 */
bool_t load_new_processes(void)
{
  pcb_t *new_arrivals = longterm_scheduler();

  /* TODO: Add new arrivals to the readyq using enqueue */
  /* TODO: and update any counters used to detect termination */

  if (new_arrivals == NULL) return FALSE;

 
  log_pcbs("Longterm scheduler jobs selected", new_arrivals);
   log_msg("\n");


  log_pcbs("New arrivals", new_arrivals);
  log_msg("\n");

  pcb_t *cur = new_arrivals;
  while (cur != NULL) {
    pcb_t *next = cur->next;
    cur->next = NULL;
    

// #pragma omp atomic update
//    total_processes++;

  //enqueue_ready_silent(cur);
 // log_ready(cur->process->name);
  enqueue(cur, &readyq, READY);
  

    cur = next;
  }

  return TRUE;
}

/* ============================= */
/* FCFS SCHEDULER                */
/* ============================= */

/** Schedules processes using FCFS scheduling */
void schedule_fcfs(void)
{
#pragma omp parallel num_threads(num_threads_global)
  {
    while (!terminate()) {

      omp_set_lock(&readyq_lock);
      pcb_t *pcb = dequeue(&readyq);
      omp_unset_lock(&readyq_lock);

      if (pcb == NULL) continue;

      pcb->state = RUNNING;
      //print_queues(pcb);

      while (pcb->state == RUNNING) {

        int status = execute_instr(pcb);

        /* spec: after every instruction, check for new arrivals */
        load_new_processes();
        print_queues(pcb);

        if (status == WAITING) {
          enqueue(pcb, &waitingq, WAITING);
          break;
        } else if (status == TERMINATED) {
          enqueue(pcb, &terminatedq, TERMINATED);
          break;
        }
      }
    }
  }

  detect_deadlock();
  free_manager();
}

/* ============================= */
/* ROUND ROBIN SCHEDULER         */
/* ============================= */

/** Schedules processes using the Round-Robin scheduler. */
void schedule_rr(int quantum)
{
  if (quantum <= 0) quantum = 1;

#pragma omp parallel num_threads(num_threads_global)
  {
    while (!terminate()) {

      omp_set_lock(&readyq_lock);
      pcb_t *pcb = dequeue(&readyq);
      omp_unset_lock(&readyq_lock);

      if (pcb == NULL) continue;

      pcb->state = RUNNING;
      //print_queues(pcb);

      int executed = 0;

      while (pcb->state == RUNNING) {

        int status = execute_instr(pcb);
        executed++;

        load_new_processes();
       print_queues(pcb);

        if (status == WAITING) {
          enqueue(pcb, &waitingq, WAITING);
          break;
        } else if (status == TERMINATED) {
          enqueue(pcb, &terminatedq, TERMINATED);
          
          break;
        }

        /* quantum expired */
        if (executed >= quantum) {
          enqueue(pcb, &readyq, READY);
          break;
        }
      }
    }
  }

  detect_deadlock();
  free_manager();
}

/* ============================= */
/* PRIORITY SCHEDULER            */
/* ============================= */

/**
 * @brief Priority scheduling with preemption.
 *        Lower value = higher priority.
 *        Ready queue must not be reordered; we scan it each time.
 *
 * Note: This is parallelised in the same style as FCFS/RR:
 * each worker thread picks the highest-priority job available at selection time.
 * After each instruction, the running worker checks if a higher-priority job
 * is now ready and, if so, yields by re-enqueueing itself.
 */
void schedule_priority(void)
{
#pragma omp parallel num_threads(num_threads_global)
  {
    while (!terminate()) {

      omp_set_lock(&readyq_lock);
      pcb_t *pcb = dequeue_highest_priority(&readyq);
      omp_unset_lock(&readyq_lock);

      if (pcb == NULL) continue;

      pcb->state = RUNNING;
      //print_queues(pcb);

      while (pcb->state == RUNNING) {

        int status = execute_instr(pcb);
        load_new_processes();
        print_queues(pcb);

        if (status == WAITING) {
          enqueue(pcb, &waitingq, WAITING);
          break;
        } else if (status == TERMINATED) {
          enqueue(pcb, &terminatedq, TERMINATED);
          break;
        }

        /* preemption check: if someone in readyq has higher priority, yield */
        omp_set_lock(&readyq_lock);
        pcb_t *challenger = find_highest_priority(&readyq);
        int should_preempt = (challenger != NULL && challenger->priority < pcb->priority);
        omp_unset_lock(&readyq_lock);

        if (should_preempt) {
          enqueue(pcb, &readyq, READY);
          break;
        }
      }
    }
  }

  detect_deadlock();
  free_manager();
}

/* ============================= */
/* EXECUTE INSTRUCTION           */
/* ============================= */

/**
 * @brief Call the correct function to execute the next instruction of the process
 *  If there is an unknown / no instruction, call the appropriate log function:
 *    log_unknown_instr() / log_no_instr()
 *  If the instruction was to release a resource, and it was successful,
 *    wake up the first process in the waiting queue waiting for this resource
 *    and if there is a process to wake up, log it with the log_wake_up() function
 *  Update the status of the process in its pcb and return its status,
 *    so that the scheduler can act accordingly
 **/
int execute_instr(pcb_t *pcb)
{
  if (pcb == NULL) return TERMINATED;

  /* no instruction */
  if (pcb->next_instruction == NULL) {
    log_no_instr(pcb->process->name);
    pcb->state = TERMINATED;
    return TERMINATED;
  }

  instr_t *instr = pcb->next_instruction;

  switch (instr->type) {

    case REQ_OP: {
      bool_t acquired = acquire_resource(pcb, instr->resource_name);
      if (acquired == FALSE) {
        /* DO NOT advance PC on failed request */
        pcb->state = WAITING;
        return WAITING;
      }
      /* success -> advance PC */
      pcb->next_instruction = instr->next;
      break;
    }

    case REL_OP: {
      bool_t released = release_resource(pcb, instr->resource_name);

      /* Always advance PC (even on error) */
      pcb->next_instruction = instr->next;

      /* If released, wake up first waiter for that resource */
      if (released == TRUE) {

        pcb_t *to_wake = NULL;

        omp_set_lock(&waitingq_lock);
        pcb_t *cur = waitingq.first;
        while (cur != NULL) {
          instr_t *wi = cur->next_instruction;
          if (wi != NULL &&
              wi->type == REQ_OP &&
              wi->resource_name != NULL &&
              strcmp(wi->resource_name, instr->resource_name) == 0) {
            to_wake = cur;
            remove_from_queue(&waitingq, cur);
            break;
          }
          cur = cur->next;
        }
        omp_unset_lock(&waitingq_lock);

        if (to_wake != NULL) {
#pragma omp atomic update
          waiting_count--;

          log_wake_up(to_wake->process->name, instr->resource_name);
          enqueue(to_wake, &readyq, READY);
        }
      }
      break;
    }

    case SEND_OP:
    case RECV_OP:
    default:
      log_unknown_instr(pcb->process->name);
      /* skip unknown */
      pcb->next_instruction = instr->next;
      break;
  }

  /* finished? */
  if (pcb->next_instruction == NULL) {
    pcb->state = TERMINATED;
    return TERMINATED;
  }

  pcb->state = RUNNING;
  return RUNNING;
}

/* ============================= */
/* ACQUIRE RESOURCE              */
/* ============================= */

/**
 * @brief Acquire a resource for a process if it is available
 *     NB: Do not remove the resource from the system_resources list
 *     Update the allocated field
 * If the resource was successfully acquired, the following log messages must be called:
 *    log_request_acquired(cur_pcb->process->name, resource_name);
 *    log_avail_resources(system_resources);
 *    log_msg("\n");
 */
bool_t acquire_resource(pcb_t *cur_pcb, char *resource_name)
{
  if (cur_pcb == NULL || resource_name == NULL) return FALSE;

  omp_set_lock(&resource_lock);

  resource_t *res = system_resources;
  resource_t *free_match = NULL;

  while (res != NULL) {
    if (strcmp(res->name, resource_name) == 0) {
      if (res->allocated == NULL && free_match == NULL) {
        free_match = res;
      }
    }
    res = res->next;
  }

  if (free_match != NULL) {
    free_match->allocated = cur_pcb;
    omp_unset_lock(&resource_lock);

    log_request_acquired(cur_pcb->process->name, resource_name);
    log_avail_resources(system_resources);
    log_msg("\n");

    return TRUE;
  }

  omp_unset_lock(&resource_lock);
  return FALSE;
}
/* ============================= */
/* RELEASE RESOURCE              */
/* ============================= */

/**
 * @brief Execute the release instruction for the process
 *  Update the allocated field
 * If the release was successful, call:
 *  log_release_released(...)
 *  log_avail_resources(...)
 *  log_msg("\n");
 * If unsuccessful, call:
 *  log_release_error(...)
 */
bool_t release_resource(pcb_t *proc, char *resource_name)
{
  if (proc == NULL || resource_name == NULL) return FALSE;

  omp_set_lock(&resource_lock);

  resource_t *res = system_resources;
  while (res != NULL) {
    if (strcmp(res->name, resource_name) == 0 && res->allocated == proc) {
      res->allocated = NULL;
      omp_unset_lock(&resource_lock);

      log_release_released(proc->process->name, resource_name);
      log_avail_resources(system_resources);
      log_msg("\n");

      return TRUE;
    }
    res = res->next;
  }

  omp_unset_lock(&resource_lock);

  /* Nothing to release (spec: log error but continue) */
  log_release_error(proc->process->name, resource_name);
  return FALSE;
}

/* ============================= */
/* QUEUE OPERATIONS              */
/* ============================= */

/**
 * @brief Enqueue process <code>pcb</code> to <code>queue</code>
 * Log the enqueue operation appropriately, depending on <code>status</code>
 *   log_ready(pcb->process->name);
 *   log_request_waiting(pcb->process->name, pcb->next_instruction->resource_name);
 *   log_terminated(pcb->process->name);
 */
void enqueue(pcb_t *pcb, pcb_queue_t *queue, int status)
{
  if (pcb == NULL || queue == NULL) return;

  pcb->state = status;
  pcb->next  = NULL;

  /* lock the correct queue */
  if (queue == &readyq) omp_set_lock(&readyq_lock);
  else if (queue == &waitingq) omp_set_lock(&waitingq_lock);
  else if (queue == &terminatedq) omp_set_lock(&terminatedq_lock);

  if (queue->first == NULL) {
    queue->first = pcb;
    queue->last  = pcb;
  } else {
    queue->last->next = pcb;
    queue->last = pcb;
  }

  if (queue == &readyq) omp_unset_lock(&readyq_lock);
  else if (queue == &waitingq) omp_unset_lock(&waitingq_lock);
  else if (queue == &terminatedq) omp_unset_lock(&terminatedq_lock);

  /* logging + counters */
  if (status == READY) {
    log_ready(pcb->process->name);
  } else if (status == WAITING) {
#pragma omp atomic update
    waiting_count++;
    if (pcb->next_instruction != NULL) {
      log_request_waiting(pcb->process->name, pcb->next_instruction->resource_name);
    } else {
      /* should not happen, but avoid NULL deref */
      log_request_waiting(pcb->process->name, "UNKNOWN");
    }
  } else if (status == TERMINATED) {
#pragma omp atomic update
    terminated_count++;
    log_terminated(pcb->process->name);
  }
}

pcb_t *dequeue(pcb_queue_t *queue)
{
  if (queue == NULL || queue->first == NULL) return NULL;

  pcb_t *pcb = queue->first;
  queue->first = pcb->next;
  if (queue->first == NULL) queue->last = NULL;
  pcb->next = NULL;
  return pcb;
}

pcb_t *find_highest_priority(pcb_queue_t *queue)
{
  if (queue == NULL || queue->first == NULL) return NULL;

  pcb_t *best = queue->first;
  pcb_t *cur  = queue->first->next;
  while (cur != NULL) {
    if (cur->priority < best->priority) best = cur;
    cur = cur->next;
  }
  return best;
}

void remove_from_queue(pcb_queue_t *queue, pcb_t *target)
{
  if (queue == NULL || target == NULL || queue->first == NULL) return;

  if (queue->first == target) {
    queue->first = target->next;
    if (queue->first == NULL) queue->last = NULL;
    target->next = NULL;
    return;
  }

  pcb_t *prev = queue->first;
  while (prev->next != NULL && prev->next != target) {
    prev = prev->next;
  }

  if (prev->next == target) {
    prev->next = target->next;
    if (queue->last == target) queue->last = prev;
    target->next = NULL;
  }
}

pcb_t *dequeue_highest_priority(pcb_queue_t *queue)
{
  if (queue == NULL || queue->first == NULL) return NULL;

  pcb_t *best = find_highest_priority(queue);
  if (best == NULL) return NULL;

  remove_from_queue(queue, best);
  best->next = NULL;
  return best;
}

/* ============================= */
/* DEADLOCK DETECTION            */
/* ============================= */

/**
 * @brief detect deadlock
 * If deadlock is detected, the following log function must be called
 *  log_deadlock_detected();
 *
 * Prints deadlock cycles and blocked processes to stdout.
 */
struct pcb_t *detect_deadlock(void)
{
  omp_set_lock(&waitingq_lock);

  int n = 0;
  for (pcb_t *p = waitingq.first; p != NULL; p = p->next) n++;

  if (n == 0) {
    omp_unset_lock(&waitingq_lock);
    printf("No deadlock detected\n");
    return NULL;
  }

  pcb_t **procs = (pcb_t **)malloc((size_t)n * sizeof(pcb_t *));
  if (procs == NULL) {
    omp_unset_lock(&waitingq_lock);
    return NULL;
  }

  pcb_t *cur = waitingq.first;
  for (int i = 0; i < n; i++) {
    procs[i] = cur;
    cur = cur->next;
  }

  omp_unset_lock(&waitingq_lock);

  int *adj = (int *)calloc((size_t)n * (size_t)n, sizeof(int));
  int *in_deadlock = (int *)calloc((size_t)n, sizeof(int));
  if (adj == NULL || in_deadlock == NULL) {
    free(procs);
    free(adj);
    free(in_deadlock);
    return NULL;
  }

  omp_set_lock(&resource_lock);
  for (int i = 0; i < n; i++) {
    instr_t *wi = procs[i]->next_instruction;
    if (wi == NULL || wi->type != REQ_OP || wi->resource_name == NULL) continue;

    for (resource_t *r = system_resources; r != NULL; r = r->next) {
      if (strcmp(r->name, wi->resource_name) != 0) continue;
      if (r->allocated == NULL) continue;

      for (int j = 0; j < n; j++) {
        if (procs[j] == r->allocated) {
          adj[i * n + j] = 1;
          break;
        }
      }
    }
  }
  omp_unset_lock(&resource_lock);

  int deadlock_found = 0;

  for (int start = 0; start < n; start++) {
    if (in_deadlock[start]) continue;

    int *state = (int *)calloc((size_t)n, sizeof(int));
    int *pos = (int *)malloc((size_t)n * sizeof(int));
    int *stack_nodes = (int *)malloc((size_t)n * sizeof(int));
    int *stack_next = (int *)malloc((size_t)n * sizeof(int));
    int top = 0;
    int found_cycle = 0;

    if (state == NULL || pos == NULL || stack_nodes == NULL || stack_next == NULL) {
      free(procs);
      free(adj);
      free(in_deadlock);
      free(state);
      free(pos);
      free(stack_nodes);
      free(stack_next);
      return NULL;
    }

    for (int i = 0; i < n; i++) pos[i] = NOT_MAPPED;

    stack_nodes[top] = start;
    stack_next[top] = 0;
    top++;
    state[start] = 1;
    pos[start] = 0;

    while (top > 0 && !found_cycle) {
      int v = stack_nodes[top - 1];
      int advanced = 0;

      for (int j = stack_next[top - 1]; j < n; j++) {
        stack_next[top - 1] = j + 1;

        if (!adj[v * n + j] || in_deadlock[j]) continue;

        if (state[j] == 0) {
          stack_nodes[top] = j;
          stack_next[top] = 0;
          state[j] = 1;
          pos[j] = top;
          top++;
          advanced = 1;
          break;
        }

        if (state[j] == 1) {
          deadlock_found = 1;
          found_cycle = 1;
          log_deadlock_detected();
          for (int k = pos[j]; k < top; k++) {
            in_deadlock[stack_nodes[k]] = 1;
          }
          break;
        }
      }

      if (found_cycle) break;
      if (advanced) continue;

      state[v] = 2;
      pos[v] = NOT_MAPPED;
      top--;
    }

    free(state);
    free(pos);
    free(stack_nodes);
    free(stack_next);
  }

  if (!deadlock_found) {
    int blocked_any = 0;
    for (int i = 0; i < n; i++) {
      if (!in_deadlock[i]) {
        blocked_any = 1;
        break;
      }
    }

    if (blocked_any) {
      log_blocked_procs();
      printf("\n");
    } else {
      printf("No deadlock detected\n");
    }
  }

  free(procs);
  free(adj);
  free(in_deadlock);

  return NULL;
}
/* ============================= */
/* FREE MANAGER                  */
/* ============================= */

/** @brief Deallocate the queues */
void free_manager(void)
{
  //print_queues(NULL);

#ifdef DEBUG_MNGR
  printf("\nFreeing the queues...\n");
#endif
  dealloc_pcbs(readyq.first);
  dealloc_pcbs(waitingq.first);
  dealloc_pcbs(terminatedq.first);

  omp_destroy_lock(&readyq_lock);
  omp_destroy_lock(&waitingq_lock);
  omp_destroy_lock(&terminatedq_lock);
  omp_destroy_lock(&resource_lock);
}

/* ============================= */
/* ARGUMENT HELPERS              */
/* ============================= */

int get_num_threads(int num_args, char **argv) {
  if (num_args > 1) return atoi(argv[1]);
  else return 1;
}

char *get_data(int num_args, char **argv) {
  char *data_origin = "generate";
  if (num_args > 2) return argv[2];
  else return data_origin;
}

int get_algo(int num_args, char **argv) {
  if (num_args > 3) return atoi(argv[3]);
  else return 1;
}

int get_time_quantum(int num_args, char **argv) {
  if (num_args > 4) return atoi(argv[4]);
  else return 1;
}

void print_args(int num_thr, char *data, int sched, int tq) {
  printf("Arguments: num_threads = %d, data = %s, scheduler = %s,  time quantum = %d\n",
         num_thr, data, (sched==0)?"priority":(sched==1)?"RR":"FCFS", tq);
}

/**
 * @brief Print the currently running process, as well as all the queued processes
 */
void print_queues(pcb_t *cur_pcb) {
  if (cur_pcb != NULL) log_running(cur_pcb, omp_get_thread_num());
  log_queue(readyq.first, "Ready");
  log_queue(waitingq.first, "Waiting");
  log_queue(terminatedq.first, "Terminated");
  log_msg("\n");
}

void enqueue_ready_silent(pcb_t *pcb)
{
  if (pcb == NULL) return;

  pcb->state = READY;
  pcb->next = NULL;

  omp_set_lock(&readyq_lock);

  if (readyq.first == NULL) {
    readyq.first = pcb;
    readyq.last = pcb;
  } else {
    readyq.last->next = pcb;
    readyq.last = pcb;
  }

  omp_unset_lock(&readyq_lock);
}

void clear_old_logs(int num_thr)
{
  char filename[32];

  for (int i = 0; i < num_thr; i++) {
    sprintf(filename, "thr%d.log", i);
    FILE *f1 = fopen(filename, "w");
    if (f1) fclose(f1);

    sprintf(filename, "thr%d.out", i);
    FILE *f2 = fopen(filename, "w");
    if (f2) fclose(f2);
  }
}
