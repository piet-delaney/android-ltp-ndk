/******************************************************************************
 *
 *   Copyright © International Business Machines  Corp., 2006, 2008
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
 *     async_handler_tsc.c
 *
 * DESCRIPTION
 *     This test mimics an async event handler in a real-time JVM
 *     An async event server thread is created that goes to sleep waiting
 *     to be woken up to do some work.
 *
 *     A user thread is created that simulates the firing of an event by
 *     signalling the async handler thread to do some work.
 *
 * USAGE:
 *     Use run_auto.sh script in current directory to build and run test.
 *     Use "-j" to enable jvm simulator.
 *
 *     Compilation: gcc -O2 -g -D_GNU_SOURCE -I/usr/include/nptl -I../../include
 *     		    -L/usr/lib/nptl -lpthread -lrt -lm async_handler_tsc.c
 *		    -o async_handler_tsc
 *
 * AUTHOR
 *      Darren Hart <dvhltc@us.ibm.com> 
 *
 * HISTORY
 *    2006-Oct-20: Initial version by Darren Hart <dvhltc@us.ibm.com>
 *
 *****************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <stdint.h>
#include <pthread.h>
#include <librttest.h>
#include <libjvmsim.h>
#include <libstats.h>

#define HANDLER_PRIO 98
#define SIGNAL_PRIO 99
#define ITERATIONS 10000000
#define HIST_BUCKETS 100
#define PASS_US 100

nsec_t start;
nsec_t end;
unsigned long long tsc_period; /* in picoseconds */
int over_20 = 0;
int over_25 = 0;
int over_30 = 0;

#define CHILD_START   0
#define CHILD_WAIT    1
#define CHILD_HANDLED 2
#define CHILD_QUIT    3
atomic_t step;

pthread_cond_t cond = PTHREAD_COND_INITIALIZER;
pthread_mutex_t mutex;

#define ULL_MAX 18446744073709551615ULL // (1 << 64) - 1
#if defined(__i386__)
#define rdtscll(val) __asm__ __volatile__("rdtsc" : "=A" (val))
#elif defined(__x86_64__)
#define rdtscll(val)					\
	do {						\
		uint32_t low, high;			\
		__asm__ __volatile__ ("rdtsc" : "=a" (low), "=d" (high)); \
		val = (uint64_t)high << 32 | low;	\
	} while(0)
#elif defined(__powerpc__)	/* 32bit version */
#define rdtscll(val)							\
	 do {								\
		uint32_t tbhi, tblo ;					\
		__asm__ __volatile__ ("mftbu %0" : "=r" (tbhi));	\
		__asm__ __volatile__ ("mftbl %0" : "=r" (tblo));	\
		val = 1000 * ((uint64_t) tbhi << 32) | tblo;		\
	} while(0)
#else
#error
#endif

static int run_jvmsim=0;

void usage(void)
{
        rt_help();
        printf("async_handler_tsc specific options:\n");
        printf("  -j            enable jvmsim\n");
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
                default:
                        handled = 0;
                        break;
        }
        return handled;
}

/* return difference in microseconds */
unsigned long long tsc_minus(unsigned long long tsc_start, unsigned long long tsc_end)
{
	unsigned long long delta;
	if (tsc_start < tsc_end)
		delta = tsc_end - tsc_start;
	else {
		delta = ULL_MAX - (tsc_end - tsc_start) + 1;
		printf("TSC wrapped, delta=%llu\n", delta);
	}
	return delta;
}

/* calculate the tsc period */
unsigned long long tsc_period_ps(void)
{
	nsec_t ns_start;
	nsec_t ns_end;
	unsigned long long tsc_start, tsc_end;

	rdtscll(tsc_start);
	ns_start = rt_gettime();
	sleep(1);
	rdtscll(tsc_end);
	ns_end = rt_gettime();

	return (1000*(ns_end - ns_start)) / tsc_minus(tsc_start, tsc_end);
}

void *handler_thread(void *arg)
{
	while (atomic_get(&step) != CHILD_QUIT) {
		pthread_mutex_lock(&mutex);
		atomic_set(CHILD_WAIT, &step);
		if (pthread_cond_wait(&cond, &mutex) != 0) {
			perror("pthead_cond_wait");
			break;
		}
		rdtscll(end);
		atomic_set(CHILD_HANDLED, &step);
		pthread_mutex_unlock(&mutex);
		while (atomic_get(&step) == CHILD_HANDLED)
			usleep(10);
	}
	printf("handler thread exiting\n");
	return NULL;
}

void *signal_thread(void *arg)
{
	int i;
	long delta, max, min;
	stats_container_t dat;
	stats_container_t hist;

	stats_container_init(&dat, ITERATIONS);
	stats_container_init(&hist, HIST_BUCKETS);

	min = max = 0;
	for (i = 0; i < ITERATIONS; i++) {
		/* wait for child to wait on cond, then signal the event */
		while (atomic_get(&step) != CHILD_WAIT)
			usleep(10);
		pthread_mutex_lock(&mutex);
		rdtscll(start);
		if (pthread_cond_signal(&cond) != 0) {
			perror("pthread_cond_signal");
			atomic_set(CHILD_QUIT, &step);
			break;
		}
		pthread_mutex_unlock(&mutex);

		/* wait for the event handler to schedule */
		while (atomic_get(&step) != CHILD_HANDLED)
			usleep(10);
		delta = (long)(tsc_period * (end - start) / 1000000);
		if (delta > 30) {
			over_30++;
		} else if (delta > 25) {
			over_25++;
		} else if (delta > 20) {
			over_20++;
		}
		dat.records[i].x = i;
		dat.records[i].y = delta;
		if (i == 0)
			min = max = delta;
		else {
			min = MIN(min, delta);
			max = MAX(max, delta);
		}
		atomic_set((i == ITERATIONS-1) ? CHILD_QUIT : CHILD_START, &step);
	}
	printf("recording statistics...\n");
	printf("Minimum: %ld\n", min);
	printf("Maximum: %ld\n", max);
	printf("Average: %f\n", stats_avg(&dat));
	printf("Standard Deviation: %f\n", stats_stddev(&dat));
	stats_hist(&hist, &dat);
	stats_container_save("samples", "Asynchronous Event Handling Latency (TSC) Scatter Plot",\
			     "Iteration", "Latency (us)", &dat, "points");
	stats_container_save("hist", "Asynchronous Event Handling Latency (TSC) Histogram",\
			     "Latency (us)", "Samples", &hist, "steps");
	printf("signal thread exiting\n");

	return NULL;
}

int main(int argc, char *argv[])
{
	int signal_id, handler_id;
	setup();

	rt_init("jh", parse_args, argc, argv);

	printf("-------------------------------\n");
	printf("Asynchronous Event Handling Latency\n");
	printf("-------------------------------\n\n");
	printf("Running %d iterations\n", ITERATIONS);
	printf("Calculating tsc period...");
	fflush(stdout);
	tsc_period = tsc_period_ps();
	printf("%llu ps\n", tsc_period);

	init_pi_mutex(&mutex);

	atomic_set(CHILD_START, &step);
	if (run_jvmsim) {
                printf("jvmsim enabled\n");
                jvmsim_init();  // Start the JVM simulation
        } else {
                printf("jvmsim disabled\n");
        }

	handler_id = create_fifo_thread(handler_thread, (void*)0, HANDLER_PRIO);
	signal_id = create_fifo_thread(signal_thread, (void*)0, SIGNAL_PRIO);

	join_threads();

	printf("%d samples over 20 us latency\n", over_20);
	printf("%d samples over 25 us latency\n", over_25);
	printf("%d samples over 30 us latency\n", over_30);

	return 0;
}