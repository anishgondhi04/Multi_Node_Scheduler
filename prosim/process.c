//
// Starter code used, Created by Alex Brodsky on 2023-05-07.
//

#include "process.h"
#include "prio_q.h"
#include <pthread.h>
#include <stdlib.h>

static prio_q_t **blocked;  // Array of blocked queues, one per node
static prio_q_t **ready;    // Array of ready queues, one per node
static int *node_clock;     // Array of local clocks, one per node
static int *next_proc_id;
static int num_nodes;       // Number of nodes
static pthread_mutex_t finished_mutex = PTHREAD_MUTEX_INITIALIZER;
static prio_q_t *finished;  // Shared queue for finished processes

enum {
    PROC_NEW = 0,
    PROC_READY,
    PROC_RUNNING,
    PROC_BLOCKED,
    PROC_FINISHED
};

static char *states[] = {"new", "ready", "running", "blocked", "finished"};

static int quantum;

extern int process_init(int cpu_quantum, int node_count) {
    quantum = cpu_quantum;
    num_nodes = node_count;

    blocked = malloc(num_nodes * sizeof(prio_q_t*));
    ready = malloc(num_nodes * sizeof(prio_q_t*));
    node_clock = calloc(num_nodes, sizeof(int));
    next_proc_id = calloc(num_nodes, sizeof(int));

    for (int i = 0; i < num_nodes; i++) {
        blocked[i] = prio_q_new();
        ready[i] = prio_q_new();
        next_proc_id[i] = 1;
    }

    finished = prio_q_new();
    return 1;
}

static void print_process(context *proc) {
    printf("[%02d] %5.5d: process %d %s\n", proc->node, node_clock[proc->node], proc->id, states[proc->state]);
}

static int actual_priority(context *proc) {
    if (proc->priority < 0) {
        return proc->duration;
    }
    return proc->priority;
}

static void insert_in_queue(context *proc, int next_op) {
    if (next_op) {
        context_next_op(proc);
        proc->duration = context_cur_duration(proc);
    }

    int op = context_cur_op(proc);

    if (op == OP_DOOP) {
        proc->state = PROC_READY;
        if (ready[proc->node] == NULL) {
            ready[proc->node] = prio_q_new();
        }
        prio_q_add(ready[proc->node], proc, actual_priority(proc));
        prio_q_add(ready[proc->node], proc, actual_priority(proc));  // Fixed: use proc->node
        proc->wait_count++;
        proc->enqueue_time = node_clock[proc->node];
    } else if (op == OP_BLOCK) {
        proc->state = PROC_BLOCKED;
        proc->duration += node_clock[proc->node];
        prio_q_add(blocked[proc->node], proc, proc->duration);  // Fixed: use proc->node
    } else {
        proc->state = PROC_FINISHED;
        pthread_mutex_lock(&finished_mutex);
        prio_q_add(finished, proc, node_clock[proc->node] * 10000 + proc->node * 100 + proc->id);
        pthread_mutex_unlock(&finished_mutex);
    }
    print_process(proc);
}

extern int process_admit(context *proc, int node) {
    proc->node = node;  // Added: set the node for the process
    proc->id = next_proc_id[node]++;
    proc->state = PROC_NEW;
    print_process(proc);
    insert_in_queue(proc, 1);
    return 1;
}

void *node_simulate(void *arg) {
    int node = *(int*)arg;
    context *cur = NULL;
    int cpu_quantum;

    while (!prio_q_empty(ready[node]) || !prio_q_empty(blocked[node]) || cur != NULL) {
        int preempt = 0;

        while (!prio_q_empty(blocked[node])) {
            context *proc = prio_q_peek(blocked[node]);
            if (proc->duration > node_clock[node]) {
                break;
            }

            prio_q_remove(blocked[node]);
            insert_in_queue(proc, 1);

            preempt |= cur != NULL && proc->state == PROC_READY &&
                       actual_priority(cur) > actual_priority(proc);
        }

        if (cur != NULL) {
            cur->duration--;
            cpu_quantum--;

            if (cur->duration == 0 || cpu_quantum == 0 || preempt) {
                insert_in_queue(cur, cur->duration == 0);
                cur = NULL;
            }
        }

        if (cur == NULL && !prio_q_empty(ready[node])) {
            cur = prio_q_remove(ready[node]);
            cur->wait_time += node_clock[node] - cur->enqueue_time;
            cpu_quantum = quantum;
            cur->state = PROC_RUNNING;
            print_process(cur);
        }

        node_clock[node]++;
    }
    return NULL;
}

extern int process_simulate() {
    pthread_t threads[num_nodes];
    int node_ids[num_nodes];

    for (int i = 0; i < num_nodes; i++) {
        node_ids[i] = i;
        pthread_create(&threads[i], NULL, node_simulate, &node_ids[i]);
    }

    for (int i = 0; i < num_nodes; i++) {
        pthread_join(threads[i], NULL);
    }

    while (!prio_q_empty(finished)) {
        context *proc = prio_q_remove(finished);
        context_stats(node_clock[proc->node], proc, stdout);  // Fixed: removed node_clock[proc->node]
    }

    return 1;
}