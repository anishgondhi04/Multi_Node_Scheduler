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

/* Initialize the simulation
 * @params:
 *   quantum: the CPU quantum to use in the situation
 * @returns:
 *   returns 1
 */
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

/* Print state of process
 * @params:
 *   proc: process' context
 * @returns:
 *   none
 */
static void print_process(context *proc) {
    printf("[%02d] %5.5d: process %d %s\n", proc->node, node_clock[proc->node], proc->id, states[proc->state]);
}

/* Compute priority of process, depending on whether SJF or priority based scheduling is used
 * @params:
 *   proc: process' context
 * @returns:
 *   priority of process
 */
static int actual_priority(context *proc) {
    if (proc->priority < 0) {
        /* SJF means duration of current DOOP is the priority
         */
        return proc->duration;
    }
    return proc->priority;
}

/* Insert process into appropriate queue based on the primitive it is performing
 * @params
 *   proc: process' context
 *   next_op: if true, current primitive is done, so move IP to next primitive.
 * @returns:
 *   none
 */
static void insert_in_queue(context *proc, int next_op) {
    /* If current primitive is done, move to next
     */
    if (next_op) {
        context_next_op(proc);
        proc->duration = context_cur_duration(proc);
    }

    int op = context_cur_op(proc);

    /* 3 cases:
     * 1. If DOOP, process goes into ready queue
     * 2. If BLOCK, process goes into blocked queue
     * 3. If HALT, process is not queued
     */
    if (op == OP_DOOP) {
        proc->state = PROC_READY;
        prio_q_add(ready, proc, actual_priority(proc));
        proc->wait_count++;
        proc->enqueue_time = node_clock[proc->node];
    } else if (op == OP_BLOCK) {
        /* Use the duration field of the process to store their wake-up time.
         */
        proc->state = PROC_BLOCKED;
        proc->duration += node_clock[proc->node];
        prio_q_add(blocked, proc, proc->duration);
    } else {
        proc->state = PROC_FINISHED;
    }
    print_process(proc);
}

/* Admit a process into the simulation
 * @params:
 *   proc: pointer to the program context of the process to be admitted
 * @returns:
 *   returns 1
 */
extern int process_admit(context *proc, int node) {
    proc->id = next_proc_id[node]++;
    proc->state = PROC_NEW;
    print_process(proc);
    insert_in_queue(proc, 1);
    return 1;
}

/* Perform the simulation
 * @params:
 *   none
 * @returns:
 *   returns 1
 */
//void *node_simulate(void *arg) {
//    int node = *(int*)arg;
//    context *cur = NULL;
//    int cpu_quantum;
//
//    while(!prio_q_empty(ready[node]) || !prio_q_empty(blocked[node]) || cur != NULL) {
//        // ... (adapt the existing process_simulate logic for a single node)
//        // Use time[node] instead of the global time variable
//        // Use ready[node] and blocked[node] instead of global ready and blocked
//    }
//
//    return NULL;
//}
void *node_simulate(void *arg) {
    int node = *(int*)arg;
    context *cur = NULL;
    int cpu_quantum;

    /* We can only stop when all processes are in the finished state
     * no processes are readdy, running, or blocked
     */
    while (!prio_q_empty(ready[node]) || !prio_q_empty(blocked[node]) || cur != NULL) {
        int preempt = 0;

        /* Step 1: Unblock processes
         * If any of the unblocked processes have higher priority than current running process
         *   we will need to preempt the current running process
         */
        while (!prio_q_empty(blocked[node])) {
            /* We can stop ff process at head of queue should not be unblocked
             */
            context *proc = prio_q_peek(blocked[node]);
            if (proc->duration > node_clock[node]) {
                break;
            }

            /* Move from blocked and reinsert into appropriate queue
             */
            prio_q_remove(blocked[node]);
            insert_in_queue(proc, 1);

            /* preemption is necessary if a process is running, and it has lower priority than
             * a newly unblocked ready process.
             */
            preempt |= cur != NULL && proc->state == PROC_READY &&
                       actual_priority(cur) > actual_priority(proc);
        }

        /* Step 2: Update current running process
         */
        if (cur != NULL) {
            cur->duration--;
            cpu_quantum--;

            /* Process stops running if it is preempted, has used up their quantum, or has completed its DOOP
             */
            if (cur->duration == 0 || cpu_quantum == 0 || preempt) {
                insert_in_queue(cur, cur->duration == 0);
                cur = NULL;
            }
        }

        /* Step 3: Select next ready process to run if none are running
         * Be sure to keep track of how long it waited in the ready queue
         */
        if (cur == NULL && !prio_q_empty(ready[node])) {
            cur = prio_q_remove(ready[node]);
            cur->wait_time += node_clock[node] - cur->enqueue_time;
            cpu_quantum = quantum;
            cur->state = PROC_RUNNING;
            print_process(cur);
        }

        /* next clock tick
         */
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

    // Print finished processes
    while (!prio_q_empty(finished)) {
        context *proc = prio_q_remove(finished);
        context_stats(node_clock[proc->node],proc, stdout);
    }

    return 1;
}
