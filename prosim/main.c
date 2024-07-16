// Name: Anish Gondhi, Assignment 3
// Student id: B00857638
// This code uses multiple threads to mimic multiple nodes to simulate processes same as Assignment2
// Starter code used, Created by Alex Brodsky on 2023-04-02.
//

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include "context.h"
#include "process.h"

int main() {
    int num_procs;
    int quantum;
    int num_nodes;

    /* Read in the header of the process description with minimal validation
    */
    if (scanf("%d %d %d", &num_procs, &quantum, &num_nodes) < 3) {
        fprintf(stderr, "Bad input, expecting number of processes, quantum size, and number of nodes\n");
        return -1;
    }

    /* We use an array of pointers to contexts to track the processes.
    */
    context **procs = calloc(num_procs, sizeof(context *));

    process_init(quantum, num_nodes);

    /* Load and admit each process, if an error occurs, we just give up.
     */
    for (int i = 0; i < num_procs; i++) {
        procs[i] = context_load(stdin);
        if (!procs[i]) {
            fprintf(stderr, "Bad input, could not load program description\n");
            return -1;
        }
        process_admit(procs[i]);
    }

    // Creating threads for each node
    pthread_t threads[num_nodes];
    int node_ids[num_nodes];

    // launching simulation for each node by calling node_simulate
    for (int i = 0; i < num_nodes; i++) {
        node_ids[i] = i + 1;
        pthread_create(&threads[i], NULL, node_simulate, &node_ids[i]);
    }

    // waiting for all threads to complete execution
    for (int i = 0; i < num_nodes; i++) {
        pthread_join(threads[i], NULL);
    }

    /* Output the statistics for processes in order of Finishing.
     */
    node_stats(stdout);

    free(procs);

    return 0;
}