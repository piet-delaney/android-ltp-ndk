/******************************************************************************
 *
 *   Copyright © International Business Machines  Corp., 2006,  2008
 *
 *   This program is free software;  you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY;  without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See
 *   the GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program;  if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 * NAME
 *      prio-wake.c
 *
 * DESCRIPTION
 *      Test priority ordered wakeup with pthread_cond_*
 * * Steps:
 *      - Creates a number of worker threads with increasing FIFO priorities
 *        (by default, num worker threads = num cpus)
 *      - Create a master thread
 *      - The time the worker thread starts running is noted. Each of the
 *	  worker threads then waits on the same _condvar_. The time it 
 *	  wakes up also noted.
 *      - Once all the threads finish execution, the start and wakeup times
 *        of all the threads is displayed.
 *      - The output must indicate that the thread wakeup happened in a 
 *	  priority order.
 *
 * USAGE:
 *
 *     Compilation: gcc -O2 -g -Wall -D_GNU_SOURCE -I/usr/include/nptl
 *                   -I../../include -L/usr/lib/nptl -lpthread -lrt -lm
 *		     -c -o prio-wake.o prio-wake.c
 *
 * AUTHOR
 *      Darren Hart <dvhltc@us.ibm.com>
 *
 * HISTORY
 *      2006-Apr-26: Initial version by Darren Hart
 *      2006-May-25: Updated to use new librt.h features
 *
 *****************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <time.h>
#include <pthread.h>
#include <sched.h>
#include <errno.h>
#include <sys/syscall.h>
#include <librttest.h>
#include <libjvmsim.h>

volatile int running_threads = 0;
static int rt_threads = 0;
static pthread_cond_t cond = PTHREAD_COND_INITIALIZER;
static pthread_mutex_t mutex;
static volatile nsec_t beginrun;

static int ret = 0;
static int run_jvmsim=0;

void usage(void)
{
        rt_help();
        printf("prio-wake specific options:\n");
        printf("  -j            #: enable jvmsim\n");
        printf("  -n#           #: number of worker threads\n");
}

int parse_args(int c, char *v)
{

        int handled = 1;
        switch (c) {
                case 'j':
                        run_jvmsim = 1;
                        break;
                case 'h':
                        usage();
                        exit(0);
                case 'n':
                        rt_threads = atoi(v);
                        break;
                default:
                        handled = 0;
                        break;
        }
        return handled;
}

struct array {
	int *arr;
	int counter;
};
struct array wakeup = { NULL , 0 };

void *master_thread(void* arg)
{
	int rc;
	nsec_t start;

	/* make sure children are started */
	while (running_threads < rt_threads)
		sleep(1);

	start = rt_gettime() - beginrun;

	/* start the children threads */
	rc = pthread_mutex_lock(&mutex);
	rc = pthread_cond_broadcast(&cond);
	rc = pthread_mutex_unlock(&mutex);

	while (running_threads > 0)
		sleep(1);
	printf("%08lld us: Master thread about to wake the workers\n", start/NS_PER_US);
	return NULL;
}

void *worker_thread(void* arg)
{
	struct sched_param sched_param;
	int policy;
	int rc;
	int mypri;
	int j;
	nsec_t start, wake;
	j = (intptr_t)arg;


	if (pthread_getschedparam(pthread_self(), &policy, &sched_param) != 0)  {
		printf("ERR: Couldn't get pthread info. Priority value wrong\n");
		mypri = -1;
	} else {
		mypri = sched_param.sched_priority;
	}

	start = rt_gettime() - beginrun;

	rc = pthread_mutex_lock(&mutex);
	running_threads++;
	rc = pthread_cond_wait(&cond, &mutex);

	wake = rt_gettime() - beginrun;
	rc = pthread_mutex_unlock(&mutex);

	rc = pthread_mutex_lock(&mutex);
	running_threads--;
	wakeup.arr[wakeup.counter++] = mypri;
	rc = pthread_mutex_unlock(&mutex);

	/* wait for all threads to quit */
	while (running_threads > 0)
		sleep(1);
	printf("%08lld us: RealtimeThread-%03d pri %03d started\n", start/NS_PER_US, j, mypri);
	printf("%08lld us: RealtimeThread-%03d pri %03d awake\n", wake/NS_PER_US, j, mypri);
	return NULL;
}


int main(int argc, char* argv[])
{
	int pri_boost;
        int numcpus;
	int i;
	setup();

        rt_init("jhn:", parse_args, argc, argv);

        if (run_jvmsim) {
                  printf("jvmsim enabled\n");
                  jvmsim_init();  // Start the JVM simulation
        } else {
                  printf("jvmsim disabled\n");
        }

	if (rt_threads == 0) {
		numcpus = sysconf(_SC_NPROCESSORS_ONLN);
		rt_threads = numcpus;
	}
	wakeup.arr = (int *)malloc(rt_threads * sizeof(int));
	wakeup.counter = 0;
	printf("\n-----------------------\n");
	printf("Priority Ordered Wakeup\n");
	printf("-----------------------\n");
	printf("Worker Threads: %d\n\n", rt_threads);

	pri_boost = 3;

	beginrun = rt_gettime();

	init_pi_mutex(&mutex);

	/* start the worker threads */
	for (i = rt_threads; i > 0; i--) {
		create_fifo_thread(worker_thread, (void*)(intptr_t)i, sched_get_priority_min(SCHED_FIFO) + pri_boost++);
	}

	/* start the master thread */
	create_fifo_thread(master_thread, (void*)(intptr_t)i, sched_get_priority_min(SCHED_FIFO) + pri_boost);

	/* wait for threads to complete */
	join_threads();

	pthread_mutex_destroy(&mutex);

	printf("\nCriteria: Threads should be woken up in priority order\n");

	for (i = 0; i < wakeup.counter; i++) {
		if (wakeup.arr[i] < wakeup.arr[i+1]) {
			printf("FAIL: Thread %d woken before %d\n", wakeup.arr[i], wakeup.arr[i+1]);
			ret++;
		}
	}
        printf("Result: %s\n", ret ? "FAIL" : "PASS");
	return ret;
}