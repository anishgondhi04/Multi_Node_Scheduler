//
// Starter code used, Created by Alex Brodsky on 2023-04-02.
//

#include "process.h"
#include "prio_q.h"
#include <pthread.h>
#include <stdlib.h>

typedef struct {
    prio_q_t *blocked;
    prio_q_t *ready;
    int node_clock;
    int next_proc_id;
    int node_id;
    int finish_time;
} node_data_t;

static node_data_t *nodes;
static int num_nodes;
static int quantum;
static pthread_mutex_t finished_mutex = PTHREAD_MUTEX_INITIALIZER;
static prio_q_t *finished;

enum {
    PROC_NEW = 0,
    PROC_READY,
    PROC_RUNNING,
    PROC_BLOCKED,
    PROC_FINISHED
};

static char *states[] = {"new", "ready", "running", "blocked", "finished"};

extern int process_init(int cpu_quantum, int node_count) {
    quantum = cpu_quantum;
    num_nodes = node_count;

    nodes = malloc(num_nodes * sizeof(node_data_t));
    for (int i = 0; i < num_nodes; i++) {
        nodes[i].blocked = prio_q_new();
        nodes[i].ready = prio_q_new();
        nodes[i].node_clock = 0;
        nodes[i].next_proc_id = 1;
        nodes[i].node_id = i + 1;
    }

    finished = prio_q_new();
    return 1;
}

static void print_process(context *proc) {
    printf("[%02d] %05d: process %d %s\n", proc->node, nodes[proc->node - 1].node_clock, proc->id, states[proc->state]);
}

static int actual_priority(context *proc) {
    if (proc->priority < 0) {
        return proc->duration;
    }
    return proc->priority;
}

static void insert_in_queue(context *proc, int next_op) {
    node_data_t *node = &nodes[proc->node - 1];

    if (next_op) {
        context_next_op(proc);
        proc->duration = context_cur_duration(proc);
    }

    int op = context_cur_op(proc);

    if (op == OP_DOOP) {
        proc->state = PROC_READY;
        prio_q_add(node->ready, proc, actual_priority(proc));
        proc->wait_count++;
        proc->enqueue_time = node->node_clock;
    } else if (op == OP_BLOCK) {
        proc->state = PROC_BLOCKED;
        proc->duration += node->node_clock;
        prio_q_add(node->blocked, proc, proc->duration);
    } else {
        proc->state = PROC_FINISHED;
        proc->finish_time = node->node_clock;
        pthread_mutex_lock(&finished_mutex);
        prio_q_add(finished, proc, proc->finish_time * 10000 + proc->node * 100 + proc->id);
        pthread_mutex_unlock(&finished_mutex);
    }
    print_process(proc);
}

extern int process_admit(context *proc) {
    node_data_t *node = &nodes[proc->node - 1];
    proc->id = node->next_proc_id++;
    proc->state = PROC_NEW;
    print_process(proc);
    insert_in_queue(proc, 1);
    return 1;
}

void *node_simulate(void *arg) {
    int node_id = *(int *) arg;
    node_data_t *node = &nodes[node_id - 1];
    context *cur = NULL;
    int cpu_quantum;

    while (!prio_q_empty(node->ready) || !prio_q_empty(node->blocked) || cur != NULL) {
        int preempt = 0;

        while (!prio_q_empty(node->blocked)) {
            context *proc = prio_q_peek(node->blocked);
            if (proc->duration > node->node_clock) {
                break;
            }

            prio_q_remove(node->blocked);
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

        if (cur == NULL && !prio_q_empty(node->ready)) {
            cur = prio_q_remove(node->ready);
            cur->wait_time += node->node_clock - cur->enqueue_time;
            cpu_quantum = quantum;
            cur->state = PROC_RUNNING;
            print_process(cur);
        }

        node->node_clock++;
    }
    return NULL;
}

extern void node_stats(FILE *fout) {

    while (!prio_q_empty(finished)) {
        context *proc = prio_q_remove(finished);
        context_stats(proc, stdout);
    }


}