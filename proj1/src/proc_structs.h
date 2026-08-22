/**
  * @description A definition of the structures and functions to store the different elements of a process.
  */

#ifndef STRUCTS_H
#define STRUCTS_H

typedef enum {NEW = 0, READY, RUNNING, WAITING, TERMINATED} state_t;
typedef enum {REQ_OP = 0, REL_OP, SEND_OP, RECV_OP} instr_types_t;
typedef enum {FALSE = 0, TRUE = 1} bool_t;

/** Each process has a linked list of instructions to execute.  */
typedef struct instr_t {
  instr_types_t type;
  char *resource_name; /* any resource, including a mailbox */
  char *msg; /* the message of a send or receive instruction */
  struct instr_t *next;
} instr_t;

/** The process_t structure stores the number, name, and the linked list of instructions of a process */
typedef struct process_t {
  int number;
  char *name;
  instr_t *first_instr; /* Do not change until the end of the program when the memory is freed */
} process_t;

/** The mailbox_t structure that represents a mailbox resource */
typedef struct mailbox_t {
  char *name;
  char *msg;
  struct mailbox_t *next;
} mailbox_t;

/** The process control block (PCB) should contain the information
  * required to obtain the the addresses of the pages in memory
  * (via the page tables) where the process is stored.

  * In this code the PCB points directly to a data structure,
  * called a process_in_mem, where the process instructions are stored.
  *
  * Note that next_instruction can point to any of the instructions in the
  * linked list, for example, after the first instruction was executed,
  * next_instruction will point to the 2nd instruction in the process's linked
  * list of instructions stored in process_in_mem, while process_in_memo will
  * still point to the first_instruction in the linked list.
  */
typedef struct pcb_t {
  struct process_t *process; /* process */
  int state; /* see enum state_t */
  struct instr_t *next_instruction; /* a ptr to an instruction in the linked list of instructions */
  int priority; /* used for priority based scheduling */
  struct pcb_t *next;
} pcb_t;

/** The resource_t structure stores a linked list of resources */
typedef struct resource_t {
  char *name;
  pcb_t *allocated; /* a ptr to either the process that has acquired the resource or NULL */
  struct resource_t *next;
} resource_t;

/** Returns a pointer to a linked list of the first n items in the job_queue */
struct pcb_t* longterm_scheduler(void);

/** Returns a pointer to the linked list of the loaded resources */
struct resource_t* get_resources(void);

/** Returns a pointer to the linked list of the loaded mailboxes */
struct mailbox_t* get_mailboxes(void);

/** Returns a pointer to the linked list of the loaded process pcbs */
int  get_total_jobs(void);

/** Deallocates the memory that was allocated for the data structures */
void dealloc_data_structures(void);

/** Deallocates the memory that was allocated for a pcb list */
void dealloc_pcbs(pcb_t *pcb);

/** Deallocates the memory that was allocated for an instruction */
void dealloc_instruction(struct instr_t *i);

/** Deallocates the memory that was allocated for a process name */
void dealloc_last_job_name(void);

/** Returns a pointer to the pcb linked list of parsed processes */
bool_t init_loader_from_files(char *filename);

/** Returns a pointer to the pcb linked list of generated processes */
bool_t init_loader_from_generator(void);

/** Initialise the loader */
void init_loader(void);

/** Creates a pcb for process <code>process_name</code> with <code>priority</code> */
bool_t load_process(char *process_name, int priority);

/** Loads the number of ready processes */
void load_ready_procs(int n);

/** Loads and stores an instruction of process <code>process_name</code> */
bool_t load_instruction(char *process_name, instr_types_t instruction,
    char *resource_name, char *msg);

/** Loads a mailbox */
bool_t load_mailbox(char *mailboxName);

/** Loads a system resource <code>resource_name</code> */
bool_t load_resource(char *resource_name);

/** Prints list of instructions last loaded */
void print_instructions(char *msg, instr_t *next_instr);

#endif
