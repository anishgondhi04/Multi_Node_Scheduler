//
// Starter code used, Created by Alex Brodsky on 2023-04-02.
//

#include "process.h"
#include "prio_q.h"
#include <pthread.h>
#include <stdlib.h>

/* This struct is used as the datastructure for each node
 * helps in creating seperate queue for each node
 * Improves code readability and provides better error handling
*/
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

// Mutex lock to prevent race condition on finished queue* (shared with all the threads).
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

/* Initialize the simulation
 * @params:
 *   quantum: the CPU quantum to use in the situation
 *   node_count: The number of nodes available for
 * @returns:
 *   returns 1
 */
extern int process_init(int cpu_quantum, int node_count) {
    /* Set up the queues for each node, store the quantum, and store the node_count
     * Assume the queues will be allocated
     */
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

    // Initializing shared queue for finished processes
    // shared queue
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
    printf("[%02d] %5.5d: process %d %s\n", proc->node, nodes[proc->node - 1].node_clock, proc->id,
           states[proc->state]);
}

/* Compute priority of process, depending on whether SJF or priority based scheduling is used
 * @params:
 *   proc: process' context
 * @returns:
 *   priority of process
 */
static int actual_priority(context *proc) {
    if (proc->priority < 0) {
        return proc->duration;
    }
    return proc->priority;
}

/* Use the node to access the correct queue
 * Insert process into appropriate queue based on the primitive it is performing
 * @params:
 *   proc: process' context
 *   next_op: if true, current primitive is done, so move IP to next primitive.
 * @returns:
 *   none
 */
static void insert_in_queue(context *proc, int next_op) {
    node_data_t *node = &nodes[proc->node - 1];

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
        prio_q_add(node->ready, proc, actual_priority(proc));
        proc->wait_count++;
        proc->enqueue_time = node->node_clock;
    } else if (op == OP_BLOCK) {
        /* Use the duration field of the process to store their wake-up time.
         */
        proc->state = PROC_BLOCKED;
        proc->duration += node->node_clock;
        prio_q_add(node->blocked, proc, proc->duration);
    } else {
        /* Use the node_clock to store the finish_time of the process.
        */
        proc->state = PROC_FINISHED;
        proc->finish_time = node->node_clock;

        /*
         * Mutex lock used to prevent race conditions
         * Threads access the finished queue (shared variable) one at a time
         */
        pthread_mutex_lock(&finished_mutex);
        prio_q_add(finished, proc, proc->finish_time * 10000 + proc->node * 100 + proc->id);
        pthread_mutex_unlock(&finished_mutex);
    }
    print_process(proc);
}

/* Admit a process into the simulation
 * @params:
 *   proc: pointer to the program context of the process to be admitted
 * @returns:
 *   returns 1
 */
extern int process_admit(context *proc) {
    /* Use the proc->node to use assigned node parameters.
     * Use a static variable to assign each process a unique process id.
     */
    node_data_t *node = &nodes[proc->node - 1];
    proc->id = node->next_proc_id++;
    proc->state = PROC_NEW;
    print_process(proc);
    insert_in_queue(proc, 1);
    return 1;
}

/* Perform the simulation
 * @params:
 *   none
 * @returns:
 *   returns NULL
 */
void *node_simulate(void *arg) {

    /*
     * Threads are created for each node to simulate in increasing order of node_id
     * All the process simulated in each node and node_id is used to keep tracks of queues
     * Context switching will only affect the order of output,
     * but will not make any difference in process simulation
     */
    int node_id = *(int *) arg;
    node_data_t *node = &nodes[node_id - 1];
    context *cur = NULL;
    int cpu_quantum;

    /* We can only stop when all processes are in the finished state
     * no processes are readdy, running, or blocked
     */
    while (!prio_q_empty(node->ready) || !prio_q_empty(node->blocked) || cur != NULL) {
        int preempt = 0;

        /* Step 1: Unblock processes
         * If any of the unblocked processes have higher priority than current running process
         *   we will need to preempt the current running process
         */
        while (!prio_q_empty(node->blocked)) {
            /* We can stop ff process at head of queue should not be unblocked
             */
            context *proc = prio_q_peek(node->blocked);
            if (proc->duration > node->node_clock) {
                break;
            }

            /* Move from blocked and reinsert into appropriate queue
             */
            prio_q_remove(node->blocked);
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
        if (cur == NULL && !prio_q_empty(node->ready)) {
            cur = prio_q_remove(node->ready);
            cur->wait_time += node->node_clock - cur->enqueue_time;
            cpu_quantum = quantum;
            cur->state = PROC_RUNNING;
            print_process(cur);
        }

        /* next clock tick
        */
        node->node_clock++;
    }
    return NULL;
}

/* pulls the processes from finished queue in FIFO and calls Context_stats for each process in that order
 * @params:
 *   fout: FILE into which the output should be written
 * @returns:
 *   none
 */
extern void node_stats(FILE *fout) {

    while (!prio_q_empty(finished)) {
        context *proc = prio_q_remove(finished);
        context_stats(proc, stdout);
    }


}