#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include "context.h"
#include "process.h"

int main() {
    int num_procs;
    int quantum;
    int num_nodes;

    if (scanf("%d %d %d", &num_procs, &quantum, &num_nodes) < 3) {
        fprintf(stderr, "Bad input, expecting number of processes, quantum size, and number of nodes\n");
        return -1;
    }

    context **procs = calloc(num_procs, sizeof(context *));
    process_init(quantum, num_nodes);

    for (int i = 0; i < num_procs; i++) {
        procs[i] = context_load(stdin);
        if (!procs[i]) {
            fprintf(stderr, "Bad input, could not load program description\n");
            return -1;
        }
        process_admit(procs[i]);
    }

    pthread_t threads[num_nodes];
    int node_ids[num_nodes];

    for (int i = 0; i < num_nodes; i++) {
        node_ids[i] = i + 1;
        pthread_create(&threads[i], NULL, node_simulate, &node_ids[i]);
    }

    for (int i = 0; i < num_nodes; i++) {
        pthread_join(threads[i], NULL);
    }

    node_stats(stdout);

    free(procs);

    return 0;
}