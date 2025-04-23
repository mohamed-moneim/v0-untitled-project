#include "headers.h"

int msgq_id;
int shm_id;
SharedClock *shm_clock;

int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("Usage: %s <remaining_time>\n", argv[0]);
        exit(1);
    }
    
    // Parse arguments
    int remaining_time = atoi(argv[1]);
    
    // Attach to shared memory and message queue
    key_t key_shm = ftok("keyfile", 'C');
    shm_id = shmget(key_shm, sizeof(SharedClock), 0666);
    shm_clock = (SharedClock *)shmat(shm_id, NULL, 0);
    
    key_t key_msg = ftok("keyfile", 'M');
    msgq_id = msgget(key_msg, 0666);
    
    // Get process ID
    int pid = getpid();
    
    // Simulate CPU-bound process
    int start_time = shm_clock->current_time;
    while (shm_clock->current_time - start_time < remaining_time) {
        // Busy wait
    }
    
    // Send termination message to scheduler
    Message msg;
    msg.mtype = PROCESS_TERMINATION;
    msg.process.id = pid;
    
    if (msgsnd(msgq_id, &msg, sizeof(msg.process), !IPC_NOWAIT) == -1) {
        perror("Error sending termination message");
        exit(1);
    }
    
    return 0;
}
