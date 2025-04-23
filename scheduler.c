#include "headers.h"

int msgq_id;
int shm_id;
SharedClock *shm_clock;
int algorithm;
int quantum;
FILE *log_file;
FILE *perf_file;

// Data structures for different scheduling algorithms
PriorityQueue* hpf_queue = NULL;    // For HPF algorithm
PriorityQueue* srtn_queue = NULL;   // For SRTN algorithm
CircularQueue* rr_queue = NULL;     // For RR algorithm

// Process table for tracking all processes
Process* process_table = NULL;
int process_count = 0;
Process* running_process = NULL;

// Statistics
int total_runtime = 0;
int idle_time = 0;
int last_clock = 0;
double *turnaround_times = NULL;
double *weighted_turnaround_times = NULL;
int finished_count = 0;

// Function to initialize scheduler
void initScheduler(int alg) {
    algorithm = alg;
    
    // Open log file
    log_file = fopen("scheduler.log", "w");
    if (!log_file) {
        perror("Error opening log file");
        exit(1);
    }
    
    // Write header to log file
    fprintf(log_file, "#At time x process y state arr w total z remain y wait k\n");
    
    // Attach to shared memory and message queue
    key_t key_shm = ftok("keyfile", 'C');
    shm_id = shmget(key_shm, sizeof(SharedClock), 0666);
    shm_clock = (SharedClock *)shmat(shm_id, NULL, 0);
    
    key_t key_msg = ftok("keyfile", 'M');
    msgq_id = msgget(key_msg, 0666);
    
    last_clock = shm_clock->current_time;
    
    // Initialize appropriate data structure based on algorithm
    if (algorithm == HPF) {
        hpf_queue = createPriorityQueue(100);  // Assuming max 100 processes
    } else if (algorithm == SRTN) {
        srtn_queue = createPriorityQueue(100);
    } else if (algorithm == RR) {
        rr_queue = createCircularQueue(100);
    }
}

// Function to handle process arrival
void processArrival(Process process) {
    // Add process to process table
    process_count++;
    process_table = realloc(process_table, process_count * sizeof(Process));
    process_table[process_count - 1] = process;
    
    // Allocate memory for statistics arrays
    turnaround_times = realloc(turnaround_times, process_count * sizeof(double));
    weighted_turnaround_times = realloc(weighted_turnaround_times, process_count * sizeof(double));
    
    printf("Process %d arrived at time %d\n", process.id, shm_clock->current_time);
    
    // Add process to appropriate queue based on algorithm
    if (algorithm == HPF) {
        insertPriorityPriorityQueue(hpf_queue, process);
    } else if (algorithm == SRTN) {
        insertRuntimePriorityQueue(srtn_queue, process);
    } else if (algorithm == RR) {
        enqueueCircularQueue(rr_queue, process);
    }
    
    // Schedule process based on algorithm
    scheduleProcess();
}

// Function to find process in process table by ID
Process* findProcessById(int id) {
    for (int i = 0; i < process_count; i++) {
        if (process_table[i].id == id) {
            return &process_table[i];
        }
    }
    return NULL;
}

// Function to handle process termination
void processTermination(int pid) {
    // Find process in process table
    Process* process = NULL;
    for (int i = 0; i < process_count; i++) {
        if (process_table[i].pid == pid) {
            process = &process_table[i];
            break;
        }
    }
    
    if (process == NULL) {
        printf("Error: Process with PID %d not found\n", pid);
        return;
    }
    
    // Update process state
    process->state = FINISHED;
    process->finish_time = shm_clock->current_time;
    process->remaining_time = 0;
    
    // Calculate statistics
    int turnaround = process->finish_time - process->arrival_time;
    double weighted_turnaround = (double)turnaround / process->runtime;
    
    turnaround_times[finished_count] = turnaround;
    weighted_turnaround_times[finished_count] = weighted_turnaround;
    finished_count++;
    
    // Log process termination
    logProcess(process, "finished");
    
    printf("Process %d finished at time %d\n", process->id, shm_clock->current_time);
    
    // If this was the running process, set running_process to NULL
    if (running_process == process) {
        running_process = NULL;
    }
    
    // Schedule next process
    scheduleProcess();
}

// Function to update waiting times for all processes
void updateWaitingTimes() {
    int current_time = shm_clock->current_time;
    int time_diff = current_time - last_clock;
    
    if (time_diff <= 0) return;
    
    // If no process is running, increment idle time
    if (running_process == NULL) {
        idle_time += time_diff;
    }
    
    // Update waiting time for all ready processes
    for (int i = 0; i < process_count; i++) {
        if (process_table[i].state == READY) {
            process_table[i].waiting_time += time_diff;
        }
    }
    
    last_clock = current_time;
    total_runtime += time_diff;
}

// Function to run a process
void runProcess(Process* process) {
    // If process is starting for the first time
    if (process->start_time == -1) {
        process->start_time = shm_clock->current_time;
        process->state = RUNNING;
        logProcess(process, "started");
    } else {
        // Process is resuming
        process->state = RUNNING;
        logProcess(process, "resumed");
    }
    
    // Fork and exec the process
    int pid = fork();
    if (pid == 0) {
        // Child process
        char remaining_time_str[10];
        sprintf(remaining_time_str, "%d", process->remaining_time);
        
        execl("./process", "process", remaining_time_str, NULL);
        perror("Error executing process");
        exit(1);
    }
    
    process->pid = pid;
    process->last_run_time = shm_clock->current_time;
    running_process = process;
    
    // Wait for process to finish or be preempted
    if (algorithm == HPF) {
        // Non-preemptive HPF: wait for process to finish
        int status;
        waitpid(pid, &status, 0);
        processTermination(pid);
    } else if (algorithm == SRTN) {
        // SRTN: check for preemption on each clock tick or process arrival
        // This is handled in the main loop
    } else if (algorithm == RR) {
        // RR: run for quantum time
        sleep(quantum);
        
        // Check if process has finished
        int status;
        if (waitpid(pid, &status, WNOHANG) > 0) {
            processTermination(pid);
        } else {
            // Process has not finished, preempt it
            kill(pid, SIGSTOP);
            stopProcess(process);
        }
    }
}

// Function to stop a process
void stopProcess(Process* process) {
    process->state = STOPPED;
    process->remaining_time -= (shm_clock->current_time - process->last_run_time);
    if (process->remaining_time < 0) process->remaining_time = 0;
    process->prempted = true;
    
    logProcess(process, "stopped");
    
    // Add process back to appropriate queue
    if (algorithm == SRTN) {
        insertRuntimePriorityQueue(srtn_queue, *process);
    } else if (algorithm == RR) {
        enqueueCircularQueue(rr_queue, *process);
    }
    
    // Set running_process to NULL
    running_process = NULL;
    
    // Schedule next process
    scheduleProcess();
}

// Function to log process state changes
void logProcess(Process* process, const char* state) {
    fprintf(log_file, "At time %d process %d %s arr %d total %d remain %d wait %d", 
            shm_clock->current_time, process->id, state, process->arrival_time, 
            process->runtime, process->remaining_time, process->waiting_time);
    
    // Add TA and WTA for finished processes
    if (strcmp(state, "finished") == 0) {
        int turnaround = process->finish_time - process->arrival_time;
        double weighted_turnaround = (double)turnaround / process->runtime;
        fprintf(log_file, " TA %d WTA %.2f", turnaround, weighted_turnaround);
    }
    
    fprintf(log_file, "\n");
    fflush(log_file);
}

// Function to log system state every second
void logSystemState() {
    static int last_log_time = -1;
    
    // Only log once per second
    if (shm_clock->current_time == last_log_time) {
        return;
    }
    
    last_log_time = shm_clock->current_time;
    
    fprintf(log_file, "At time %d: System state:\n", shm_clock->current_time);
    
    // Log running process if any
    if (running_process != NULL) {
        fprintf(log_file, "  Running process: %d (remaining: %d)\n", 
                running_process->id, running_process->remaining_time);
    } else {
        fprintf(log_file, "  No process running\n");
    }
    
    // Log ready processes
    fprintf(log_file, "  Ready processes: ");
    int ready_count = 0;
    for (int i = 0; i < process_count; i++) {
        if (process_table[i].state == READY) {
            fprintf(log_file, "%d ", process_table[i].id);
            ready_count++;
        }
    }
    if (ready_count == 0) {
        fprintf(log_file, "none");
    }
    fprintf(log_file, "\n");
    
    // Log blocked processes (if any)
    fprintf(log_file, "  Blocked processes: ");
    int blocked_count = 0;
    for (int i = 0; i < process_count; i++) {
        if (process_table[i].state == STOPPED) {
            fprintf(log_file, "%d ", process_table[i].id);
            blocked_count++;
        }
    }
    if (blocked_count == 0) {
        fprintf(log_file, "none");
    }
    fprintf(log_file, "\n");
    
    // Log finished processes
    fprintf(log_file, "  Finished processes: ");
    int finished_count_local = 0;
    for (int i = 0; i < process_count; i++) {
        if (process_table[i].state == FINISHED) {
            fprintf(log_file, "%d ", process_table[i].id);
            finished_count_local++;
        }
    }
    if (finished_count_local == 0) {
        fprintf(log_file, "none");
    }
    fprintf(log_file, "\n");
    
    // Log queue sizes
    if (algorithm == HPF) {
        fprintf(log_file, "  HPF Queue size: %d\n", hpf_queue->size);
    } else if (algorithm == SRTN) {
        fprintf(log_file, "  SRTN Queue size: %d\n", srtn_queue->size);
    } else if (algorithm == RR) {
        fprintf(log_file, "  RR Queue size: %d\n", rr_queue->size);
    }
    
    // Log CPU utilization so far
    double cpu_util = 0;
    if (total_runtime > 0) {
        cpu_util = 100.0 * (total_runtime - idle_time) / total_runtime;
    }
    fprintf(log_file, "  CPU utilization: %.2f%%\n", cpu_util);
    
    fprintf(log_file, "-----------------------------------\n");
    fflush(log_file);
}

// Function to display the currently running process
void displayRunningProcess() {
    static int last_display_time = -1;
    
    // Only display once per second
    if (shm_clock->current_time == last_display_time) {
        return;
    }
    
    last_display_time = shm_clock->current_time;
    
    printf("\n===== Time: %d =====\n", shm_clock->current_time);
    if (running_process != NULL) {
        printf("Running Process: ID=%d, Priority=%d, Remaining Time=%d\n", 
               running_process->id, 
               running_process->priority,
               running_process->remaining_time);
    } else {
        printf("No process running (CPU idle)\n");
    }
    printf("===================\n");
}

// Function to schedule processes based on algorithm
void scheduleProcess() {
    updateWaitingTimes();
    
    // If a process is already running, return (for non-preemptive algorithms)
    if (running_process != NULL && algorithm == HPF) {
        return;
    }
    
    Process next_process;
    bool found_next = false;
    
    if (algorithm == HPF) {
        // Highest Priority First (non-preemptive)
        if (hpf_queue->size > 0) {
            next_process = removePriorityPriorityQueue(hpf_queue);
            found_next = true;
        }
    } else if (algorithm == SRTN) {
        // Shortest Remaining Time Next
        if (srtn_queue->size > 0) {
            // Get process with shortest remaining time
            Process shortest = removeRuntimePriorityQueue(srtn_queue);
            
            // If a process is running, compare remaining times
            if (running_process != NULL) {
                // If running process has shorter or equal remaining time, keep it running
                if (running_process->remaining_time <= shortest.remaining_time) {
                    // Put shortest back in the queue
                    insertRuntimePriorityQueue(srtn_queue, shortest);
                    return;
                } else {
                    // Preempt the running process
                    kill(running_process->pid, SIGSTOP);
                    stopProcess(running_process);
                    
                    // Update shortest process state
                    Process* process_ptr = findProcessById(shortest.id);
                    if (process_ptr != NULL) {
                        process_ptr->state = READY;
                        next_process = *process_ptr;
                        found_next = true;
                    }
                }
            } else {
                // No process running, just run the shortest
                Process* process_ptr = findProcessById(shortest.id);
                if (process_ptr != NULL) {
                    process_ptr->state = READY;
                    next_process = *process_ptr;
                    found_next = true;
                }
            }
        }
    } else if (algorithm == RR) {
        // Round Robin
        if (!isCircularQueueEmpty(rr_queue)) {
            next_process = dequeueCircularQueue(rr_queue);
            found_next = true;
        }
    }
    
    // If a process was selected, run it
    if (found_next) {
        Process* process_ptr = findProcessById(next_process.id);
        if (process_ptr != NULL) {
            runProcess(process_ptr);
        }
    }
}

// Function to generate performance metrics
void generatePerformanceMetrics() {
    // Open performance file
    perf_file = fopen("scheduler.perf", "w");
    if (!perf_file) {
        perror("Error opening performance file");
        exit(1);
    }
    
    // Calculate CPU utilization
    double cpu_utilization = 100.0 * (total_runtime - idle_time) / total_runtime;
    
    // Calculate average weighted turnaround time
    double avg_wta = 0;
    for (int i = 0; i < finished_count; i++) {
        avg_wta += weighted_turnaround_times[i];
    }
    avg_wta /= finished_count;
    
    // Calculate average waiting time
    double avg_waiting = 0;
    for (int i = 0; i < process_count; i++) {
        avg_waiting += process_table[i].waiting_time;
    }
    avg_waiting /= process_count;
    
    // Calculate standard deviation for weighted turnaround time
    double std_wta = 0;
    for (int i = 0; i < finished_count; i++) {
        std_wta += pow(weighted_turnaround_times[i] - avg_wta, 2);
    }
    std_wta = sqrt(std_wta / finished_count);
    
    // Write metrics to file
    fprintf(perf_file, "CPU utilization = %.2f%%\n", cpu_utilization);
    fprintf(perf_file, "Avg WTA = %.2f\n", avg_wta);
    fprintf(perf_file, "Avg Waiting = %.2f\n", avg_waiting);
    fprintf(perf_file, "Std WTA = %.2f\n", std_wta);
    
    fclose(perf_file);
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("Usage: %s <algorithm> [quantum]\n", argv[0]);
        exit(1);
    }
    
    // Parse arguments
    algorithm = atoi(argv[1]);
    if (algorithm == RR && argc < 3) {
        printf("Error: Round Robin requires a time quantum\n");
        exit(1);
    }
    
    if (algorithm == RR) {
        quantum = atoi(argv[2]);
    }
    
    // Initialize scheduler
    initScheduler(algorithm);
    
    // Main loop
    Message msg;
    while (1) {
        // Log system state every second
        logSystemState();
        
        displayRunningProcess();
        
        // Check for messages
        if (msgrcv(msgq_id, &msg, sizeof(msg.process), 0, IPC_NOWAIT) != -1) {
            if (msg.mtype == PROCESS_ARRIVAL) {
                processArrival(msg.process);
            } else if (msg.mtype == PROCESS_TERMINATION) {
                processTermination(msg.process.id);
            }
        }
        
        // Update waiting times
        updateWaitingTimes();
        
        // Check if all processes have finished
        int all_finished = 1;
        for (int i = 0; i < process_count; i++) {
            if (process_table[i].state != FINISHED) {
                all_finished = 0;
                break;
            }
        }
        
        if (all_finished && process_count > 0) {
            break;
        }
        
        // Sleep for a short time
        usleep(100000); // 100ms
    }
    
    // Generate performance metrics
    generatePerformanceMetrics();
    
    // Clean up
    fclose(log_file);
    free(process_table);
    free(turnaround_times);
    free(weighted_turnaround_times);
    
    // Free data structures
    if (algorithm == HPF) {
        destroyPriorityQueue(hpf_queue);
    } else if (algorithm == SRTN) {
        destroyPriorityQueue(srtn_queue);
    } else if (algorithm == RR) {
        free(rr_queue->array);
        free(rr_queue);
    }
    
    return 0;
}
