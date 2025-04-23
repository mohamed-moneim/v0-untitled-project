#include "headers.h"

int msgq_id;
int shm_id;
SharedClock *shm_clock;
int scheduler_pid;
int clk_pid;

// Function to read process data from input file
void readProcessFile(const char* filename) {
    FILE *file = fopen(filename, "r");
    if (!file) {
        perror("Error opening process file");
        exit(1);
    }

    char line[100];
    Process process;
    Message msg;

    // Skip comment lines
    while (fgets(line, sizeof(line), file)) {
        if (line[0] == '#' || line[0] == '\n') continue;
        
        // Parse process data
        if (sscanf(line, "%d\t%d\t%d\t%d", &process.id, &process.arrival_time, 
                   &process.runtime, &process.priority) == 4) {
            
            // Initialize Process fields
            process.remaining_time = process.runtime;
            process.waiting_time = 0;
            process.state = READY;
            process.start_time = -1;
            process.finish_time = -1;
            process.last_run_time = -1;
            process.prempted = false;
            process.memsize = 0; // Not used in this implementation
            
            // Wait until the process arrival time
            while (shm_clock->current_time < process.arrival_time) {
                usleep(100000); // Sleep for 100ms
            }
            
            // Send process to scheduler
            msg.mtype = PROCESS_ARRIVAL;
            msg.process = process;
            if (msgsnd(msgq_id, &msg, sizeof(msg.process), !IPC_NOWAIT) == -1) {
                perror("Error sending message");
                exit(1);
            }
            
            printf("Process %d sent to scheduler at time %d\n", 
                   process.id, shm_clock->current_time);
        }
    }
    
    fclose(file);
}

// Function to initialize shared memory for clock
int initClockShm() {
    key_t key = ftok("keyfile", 'C');
    shm_id = shmget(key, sizeof(SharedClock), IPC_CREAT | 0666);
    if (shm_id == -1) {
        perror("Error creating shared memory");
        exit(1);
    }
    
    shm_clock = (SharedClock *)shmat(shm_id, NULL, 0);
    if ((void *)shm_clock == (void *)-1) {
        perror("Error attaching shared memory");
        exit(1);
    }
    
    return shm_id;
}

// Function to initialize message queue
int initMessageQueue() {
    key_t key = ftok("keyfile", 'M');
    msgq_id = msgget(key, IPC_CREAT | 0666);
    if (msgq_id == -1) {
        perror("Error creating message queue");
        exit(1);
    }
    
    return msgq_id;
}

// Function to clean up resources
void clearResources(int signum) {
    // Kill child processes
    if (scheduler_pid > 0) kill(scheduler_pid, SIGKILL);
    if (clk_pid > 0) kill(clk_pid, SIGKILL);
    
    // Remove IPC resources
    msgctl(msgq_id, IPC_RMID, NULL);
    shmdt(shm_clock);
    shmctl(shm_id, IPC_RMID, NULL);
    
    printf("Resources cleared\n");
    exit(0);
}

int main(int argc, char *argv[]) {
    // Set up signal handler for cleanup
    signal(SIGINT, clearResources);
    
    // Initialize IPC
    initClockShm();
    initMessageQueue();
    
    // Create clock process
    clk_pid = fork();
    if (clk_pid == 0) {
        execl("./clk", "clk", NULL);
        perror("Error executing clock");
        exit(1);
    }
    
    // Get scheduling algorithm from user
    int algorithm;
    int quantum = 0;
    
    printf("Choose scheduling algorithm:\n");
    printf("1. Non-preemptive Highest Priority First (HPF)\n");
    printf("2. Shortest Remaining Time Next (SRTN)\n");
    printf("3. Round Robin (RR)\n");
    printf("Enter your choice (1-3): ");
    scanf("%d", &algorithm);
    
    if (algorithm == RR) {
        printf("Enter time quantum for RR: ");
        scanf("%d", &quantum);
    }
    
    // Create scheduler process
    scheduler_pid = fork();
    if (scheduler_pid == 0) {
        char alg_str[10], quantum_str[10];
        sprintf(alg_str, "%d", algorithm);
        sprintf(quantum_str, "%d", quantum);
        
        execl("./scheduler", "scheduler", alg_str, quantum_str, NULL);
        perror("Error executing scheduler");
        exit(1);
    }
    
    // Read process file and send processes to scheduler
    readProcessFile("processes.txt");
    
    // Wait for scheduler to finish
    int status;
    waitpid(scheduler_pid, &status, 0);
    
    // Clean up resources
    clearResources(0);
    
    return 0;
}
