#include "headers.h"

int shm_id;
SharedClock *shm_clock;

int main() {
    // Attach to shared memory
    key_t key = ftok("keyfile", 'C');
    shm_id = shmget(key, sizeof(SharedClock), 0666);
    shm_clock = (SharedClock *)shmat(shm_id, NULL, 0);
    
    // Initialize clock
    shm_clock->current_time = 0;
    
    // Increment clock every second
    while (1) {
        sleep(1);
        shm_clock->current_time++;
    }
    
    return 0;
}
