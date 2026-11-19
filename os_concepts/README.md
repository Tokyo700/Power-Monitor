# OS Concepts Toolkit

A Linux operating-system project in C and Bash. Each module simulates or demonstrates
one topic from the course syllabus, and a Bash menu runs them and saves the output
to log files.

## How to run

```bash
cd OS_Lab_Work/OS_Project_Final
make                       # compiles every C program into bin/
bash scripts/os_toolkit.sh # opens the menu
```

## Modules and the concepts they use

| # | Module (file) | Syllabus unit | OS concepts used |
|---|---------------|---------------|------------------|
| 1 | CPU scheduling (`src/scheduler.c`) | Unit 2 | FCFS, SJF (non-preemptive), Round Robin with time quantum, waiting and turnaround time, Gantt chart |
| 2 | Process creation (`src/process_demo.c`) | Unit 1, Unit 2 | System calls `fork()`, `execvp()`, `waitpid()`, `getpid()`, `getppid()`; parent and child processes; exit status |
| 3 | Producer-consumer (`src/prodcons.c`) | Unit 3 | Threads (pthreads), critical section, mutual exclusion (mutex semaphore), counting semaphores `empty` and `full`, circular buffer |
| 4 | Deadlock (`src/deadlock.c`) | Unit 3 | Banker's algorithm (safe state, safety check), resource-request avoidance with rollback, deadlock detection |
| 5 | Page replacement (`src/paging.c`) | Unit 4 | Paging, demand paging, page faults, FIFO, LRU, Optimal replacement |
| 6 | Disk scheduling (`src/disk.c`) | Unit 5 | Disk head scheduling: FCFS, SSTF, SCAN (elevator) and total head movement |
| 7 | Shell driver (`scripts/os_toolkit.sh`) | Unit 1, Unit 5 | Bash functions, `case`, loops, input validation, pipes, `tee` for logging, `PIPESTATUS`, file handling |

## Sample inputs and expected results

These are standard textbook examples, so you can use them to check the output or
to show in the presentation.

- **CPU scheduling:** 3 processes, arrivals `0 1 2`, bursts `5 3 1`, quantum `2`.
  Round Robin Gantt chart: `P1 P2 P3 P1 P2 P1`.
- **Banker's algorithm:** 5 processes, 3 resources. Allocation `010 200 302 211 002`,
  Maximum `753 322 902 222 433`, Available `332`. Safe sequence: `P1 P3 P4 P0 P2`.
- **Page replacement:** reference string `7 0 1 2 0 3 0 4 2 3 0 3 2 1 2 0 1 7 0 1`,
  3 frames. Faults: FIFO 15, LRU 12, Optimal 9.
- **Disk scheduling:** 200 cylinders, head at 53, requests `98 183 37 122 14 124 65 67`.
  Head movement: FCFS 640, SSTF 236, SCAN 331.

## Project layout

```
OS_Project_Final/
├── Makefile              builds bin/*
├── README.md
├── src/                  one C program per module
├── scripts/os_toolkit.sh menu driver (Bash)
├── bin/                  compiled programs (created by make)
└── logs/                 saved output of each module (created by the script)
```

## Presentation tips

For each module, say which unit it covers and which concept it shows. For example:
"The producer-consumer module uses semaphores to prevent race conditions on the
shared buffer, which is the critical-section problem from Unit 3."
