#include "headers.h"

int msgq_id;
int shm_id;
SharedClock *shm_clock;
int algorithm;
int quantum;
FILE *log_file;
FILE *perf_file;

// PCB list and current running process
PCB *process_table = NULL;
int process_count = 0;
int running_process = -1;

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
}

// Function to handle process arrival
void processArrival(PCB process) {
    // Add process to process table
    process_count++;
    process_table = realloc(process_table, process_count * sizeof(PCB));
    process_table[process_count - 1] = process;
    
    // Allocate memory for statistics arrays
    turnaround_times = realloc(turnaround_times, process_count * sizeof(double));
    weighted_turnaround_times = realloc(weighted_turnaround_times, process_count * sizeof(double));
    
    printf("Process %d arrived at time %d\n", process.id, shm_clock->current_time);
    
    // Schedule process based on algorithm
    scheduleProcess();
}

// Function to handle process termination
void processTermination(int pid) {
    // Find process in process table
    for (int i = 0; i < process_count; i++) {
        if (process_table[i].id == pid) {
            // Update process state
            process_table[i].state = FINISHED;
            process_table[i].finish_time = shm_clock->current_time;
            process_table[i].remaining_time = 0;
            
            // Calculate statistics
            int turnaround = process_table[i].finish_time - process_table[i].arrival_time;
            double weighted_turnaround = (double)turnaround / process_table[i].runtime;
            
            turnaround_times[finished_count] = turnaround;
            weighted_turnaround_times[finished_count] = weighted_turnaround;
            finished_count++;
            
            // Log process termination
            logProcess(&process_table[i], "finished");
            
            printf("Process %d finished at time %d\n", pid, shm_clock->current_time);
            
            // If this was the running process, set running_process to -1
            if (running_process == i) {
                running_process = -1;
            }
            
            break;
        }
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
    if (running_process == -1) {
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
void runProcess(PCB *process) {
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
    
    process->last_run_time = shm_clock->current_time;
    
    // Wait for process to finish or be preempted
    if (algorithm == HPF) {
        // Non-preemptive HPF: wait for process to finish
        int status;
        waitpid(pid, &status, 0);
        processTermination(process->id);
    } else if (algorithm == SRTN) {
        // SRTN: check for preemption on each clock tick or process arrival
        // This is handled in the main loop
    } else if (algorithm == RR) {
        // RR: run for quantum time
        sleep(quantum);
        
        // Check if process has finished
        int status;
        if (waitpid(pid, &status, WNOHANG) > 0) {
            processTermination(process->id);
        } else {
            // Process has not finished, preempt it
            kill(pid, SIGSTOP);
            stopProcess(process);
        }
    }
}

// Function to stop a process
void stopProcess(PCB *process) {
    process->state = STOPPED;
    process->remaining_time -= (shm_clock->current_time - process->last_run_time);
    if (process->remaining_time < 0) process->remaining_time = 0;
    
    logProcess(process, "stopped");
    
    // Set running_process to -1
    running_process = -1;
    
    // Schedule next process
    scheduleProcess();
}

// Function to log process state changes
void logProcess(PCB *process, const char *state) {
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

// Function to schedule processes based on algorithm
void scheduleProcess() {
    updateWaitingTimes();
    
    // If a process is already running, return (for non-preemptive algorithms)
    if (running_process != -1 && algorithm == HPF) {
        return;
    }
    
    int next_process = -1;
    
    if (algorithm == HPF) {
        // Highest Priority First (non-preemptive)
        int highest_priority = 11; // Higher than max priority (10)
        
        for (int i = 0; i < process_count; i++) {
            if (process_table[i].state == READY && process_table[i].priority < highest_priority) {
                highest_priority = process_table[i].priority;
                next_process = i;
            }
        }
    } else if (algorithm == SRTN) {
        // Shortest Remaining Time Next
        int shortest_time = INT_MAX;
        
        for (int i = 0; i < process_count; i++) {
            if ((process_table[i].state == READY || (i == running_process && process_table[i].state == RUNNING)) && 
                process_table[i].remaining_time < shortest_time) {
                shortest_time = process_table[i].remaining_time;
                next_process = i;
            }
        }
        
        // If the next process is different from the running process, preempt
        if (running_process != -1 && next_process != running_process) {
            // Stop the current running process
            stopProcess(&process_table[running_process]);
        }
    } else if (algorithm == RR) {
        // Round Robin
        // Find the next ready process after the current running process
        int start = (running_process == -1) ? 0 : (running_process + 1) % process_count;
        int i = start;
        
        do {
            if (process_table[i].state == READY) {
                next_process = i;
                break;
            }
            i = (i + 1) % process_count;
        } while (i != start);
    }
    
    // If a process was selected, run it
    if (next_process != -1) {
        running_process = next_process;
        runProcess(&process_table[next_process]);
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
    
    return 0;
}
