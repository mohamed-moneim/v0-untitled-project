#include <stdio.h>
#include <stdlib.h>
#include <time.h>

int main(int argc, char *argv[]) {
    if (argc != 2) {
        printf("Usage: %s <number_of_processes>\n", argv[0]);
        return 1;
    }
    
    int n = atoi(argv[1]);
    if (n <= 0) {
        printf("Number of processes must be positive\n");
        return 1;
    }
    
    // Seed random number generator
    srand(time(NULL));
    
    // Open output file
    FILE *file = fopen("processes.txt", "w");
    if (!file) {
        perror("Error opening file");
        return 1;
    }
    
    // Write header
    fprintf(file, "#id\tarrival\truntime\tpriority\n");
    
    // Generate processes
    for (int i = 1; i <= n; i++) {
        int arrival = rand() % 20;          // Arrival time between 0-19
        int runtime = 1 + rand() % 20;      // Runtime between 1-20
        int priority = rand() % 11;         // Priority between 0-10
        
        fprintf(file, "%d\t%d\t%d\t%d\n", i, arrival, runtime, priority);
    }
    
    fclose(file);
    printf("Generated %d processes in processes.txt\n", n);
    
    return 0;
}
