#ifndef HEADERS_H
#define HEADERS_H

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <signal.h>
#include <string.h>
#include <math.h>
#include <sys/wait.h>

// Define process states
#define READY 0
#define RUNNING 1
#define STOPPED 2
#define FINISHED 3

// Define message types
#define PROCESS_ARRIVAL 1
#define PROCESS_TERMINATION 2
#define CLOCK_TICK 3

// Define scheduling algorithms
#define HPF 1
#define SRTN 2
#define RR 3

// Process Control Block (PCB) structure
typedef struct {
    int id;
    int arrival_time;
    int runtime;
    int priority;
    int remaining_time;
    int waiting_time;
    int start_time;
    int finish_time;
    int state;
    int last_run_time;
} PCB;

// Message structure for IPC
typedef struct {
    long mtype;
    PCB process;
} Message;

// Shared memory structure for clock
typedef struct {
    int current_time;
} SharedClock;

// Function declarations
int initClockShm();
int initMessageQueue();
void clearResources(int);
void readProcessFile(const char*);
void initScheduler(int);
void processArrival(PCB);
void processTermination(int);
void updateWaitingTimes();
void scheduleProcess();
void runProcess(PCB*);
void stopProcess(PCB*);
void logProcess(PCB*, const char*);
void generatePerformanceMetrics();

// Global variables
extern int msgq_id;
extern int shm_id;
extern SharedClock *shm_clock;
extern int scheduler_pid;
extern int algorithm;
extern int quantum;
extern FILE *log_file;
extern FILE *perf_file;

#endif
