//
// Starter code used, Created by Alex Brodsky on 2023-05-07.
//

#ifndef PROSIM_PROCESS_H
#define PROSIM_PROCESS_H

#include "context.h"
/* Initialize the simulation
 * @params:
 *   quantum: the CPU quantum to use in the situation
 * @returns:
 *   returns 1
 */
extern int process_init(int cpu_quantum, int num_nodes);

/* Admit a process into the simulation
 * @params:
 *   proc: pointer to the program context of the process to be admitted
 * @returns:
 *   returns 1
 */
extern int process_admit(context *proc);

/* Perform the simulation
 * @params:
 *   none
 * @returns:
 *   returns 1
 */
void *node_simulate(void *arg);

/* pulls the processes from finished queue in FIFO and calls Context_stats for each process in that order
 * @params:
 *   fout: FILE into which the output should be written
 * @returns:
 *   none
 */
extern void node_stats(FILE *fout);

#endif //PROSIM_PROCESS_H
