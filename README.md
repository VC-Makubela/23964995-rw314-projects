# RW314 Project 1 — Process Scheduling Simulation

## Overview
This project implements a process scheduling simulation in C using OpenMP.  
The system supports multiple scheduling algorithms and manages processes, resources, and deadlock detection.

## Features
- Scheduling algorithms:
  - First Come First Serve (FCFS)
  - Round Robin (RR)
  - Priority Scheduling (PRIOR)
- Resource management (REQ / REL instructions)
- Waiting, ready, and terminated queues
- Deadlock detection
- Parallel execution using OpenMP

## Project Structure
proj1/
├── data/ # Input process files
├── src/ # Source code
├── Makefile # Build instructions
├── run.sh # Script to run the program
└── README.md # Project documentation


## Compilation
To compile the project, run:

```bash
make

This will generate the executable:

schedule_processes
Running the Program
./schedule_processes <threads> <datafile> <algorithm> <quantum>
Arguments
<threads> — number of OpenMP threads
<datafile> — input file (e.g., data/process1.list)
<algorithm>:
0 = Priority
1 = Round Robin
2 = FCFS
<quantum> — time quantum (only used for Round Robin)
Example
./schedule_processes 1 data/process1.list 2 2
Output
thrX.log — execution log (must match specification exactly)
thrX.out — system state output (queues, resources, etc.)
Notes
The program follows the RW314 project specification.
Deadlock detection handles multiple resource instances correctly.
All provided test cases pass successfully.
Author

Student Number: 23964995

