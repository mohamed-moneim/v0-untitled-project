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
#include <limits.h>
#include <stdbool.h>

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

// Process structure as provided
typedef struct Process {
    int id;
    int arrival_time;
    int runtime;
    int priority;
    bool prempted;
    int memsize;
    
    // Additional fields for scheduler
    int remaining_time;
    int waiting_time;
    int start_time;
    int finish_time;
    int state;
    int last_run_time;
    int pid; // Actual process ID
} Process;

// Circular Queue Implementation
typedef struct CircularQueue {
    int front, rear, size;
    int capacity;
    Process* array;
} CircularQueue;

// Priority Queue Implementation
typedef struct PriorityQueue {
    Process* array;
    int size;
    int capacity;
} PriorityQueue;

// Message structure for IPC
typedef struct {
    long mtype;
    Process process;
} Message;

// Shared memory structure for clock
typedef struct {
    int current_time;
} SharedClock;

// Function declarations for Circular Queue
CircularQueue* createCircularQueue(int capacity);
int isCircularQueueFull(CircularQueue* queue);
int isCircularQueueEmpty(CircularQueue* queue);
void enqueueCircularQueue(CircularQueue* queue, Process process);
Process dequeueCircularQueue(CircularQueue* queue);

// Function declarations for Priority Queue
PriorityQueue* createPriorityQueue(int capacity);
void swapProcesses(Process* a, Process* b);
void heapifyUpPriority(PriorityQueue* pq, int index);
void heapifyDownPriority(PriorityQueue* pq, int index);
void heapifyUpRuntime(PriorityQueue* pq, int index);
void heapifyDownRuntime(PriorityQueue* pq, int index);
void insertPriorityPriorityQueue(PriorityQueue* pq, Process process);
void insertRuntimePriorityQueue(PriorityQueue* pq, Process process);
Process removePriorityPriorityQueue(PriorityQueue* pq);
Process removeRuntimePriorityQueue(PriorityQueue* pq);
void destroyPriorityQueue(PriorityQueue* pq);

// Function declarations for scheduler
int initClockShm();
int initMessageQueue();
void clearResources(int);
void readProcessFile(const char*);
void initScheduler(int);
void processArrival(Process);
void processTermination(int);
void updateWaitingTimes();
void scheduleProcess();
void runProcess(Process*);
void stopProcess(Process*);
void logProcess(Process*, const char*);
void logSystemState();
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
