/*   
 *   File: test_simple.c
 *   Author: Vasileios Trigonakis <vasileios.trigonakis@epfl.ch>
 *   Description: 
 *   test_simple.c is part of ASCYLIB
 *
 * Copyright (c) 2014 Vasileios Trigonakis <vasileios.trigonakis@epfl.ch>,
 * 	     	      Tudor David <tudor.david@epfl.ch>
 *	      	      Distributed Programming Lab (LPD), EPFL
 *
 * ASCYLIB is free software: you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation, version 2
 * of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <assert.h>
#include <getopt.h>
#include <limits.h>
#include <pthread.h>
#include <signal.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/time.h>
#include <time.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <sched.h>
#include <inttypes.h>
#include <sys/time.h>
#include <unistd.h>
#include <malloc.h>
#include "utils.h"
#include "common.h"
#include "ssmem.h"
#include "atomic_ops.h"
#include "rapl_read.h"
#ifdef __sparc__
#  include <sys/types.h>
#  include <sys/processor.h>
#  include <sys/procset.h>
#endif

#include "optik.h"

/* ################################################################### *
 * Definition of macros: per data structure
 * ################################################################### */

#define DS_SIZE(s)        initial
#define DS_NEW()          calloc(initial, sizeof(optik_t))

#define DS_TYPE           optik_t
#define DS_NODE           optik_t

/* ################################################################### *
 * GLOBALS
 * ################################################################### */

size_t initial = 1;
size_t range = 1;
size_t load_factor;
double update = 0;
size_t num_threads = 1;
size_t duration = 1000;
int test_verbose = 0;

size_t print_vals_num = 100; 
size_t pf_vals_num = 1023;
size_t put, put_explicit = false;
double update_rate, put_rate, get_rate;

size_t size_after = 0;
int seed = 0;
__thread unsigned long * seeds;
__thread ssmem_allocator_t* alloc;
uint32_t rand_max;
#define rand_min 1

static volatile int stop;
TEST_VARS_GLOBAL;

volatile ticks *putting_succ;
volatile ticks *putting_fail;
volatile ticks *getting_succ;
volatile ticks *getting_fail;
volatile ticks *removing_succ;
volatile ticks *removing_fail;
volatile ticks *putting_count;
volatile ticks *putting_count_succ;
volatile ticks *getting_count;
volatile ticks *getting_count_succ;
volatile ticks *removing_count;
volatile ticks *removing_count_succ;
volatile ticks *consecutive;
volatile ticks *total;


/* ################################################################### *
 * LOCALS
 * ################################################################### */

#ifdef DEBUG
extern __thread uint32_t put_num_restarts;
extern __thread uint32_t put_num_failed_expand;
extern __thread uint32_t put_num_failed_on_new;
#endif

barrier_t barrier, barrier_global;

typedef struct thread_data
{
  uint32_t id;
  DS_TYPE* set;
} thread_data_t;


static volatile size_t* global_counter;
OPTIK_STATS_VARS_DEFINITION();

void*
test(void* thread) 
{
  thread_data_t* td = (thread_data_t*) thread;
  uint32_t ID = td->id;
  int phys_id = the_cores[ID];
  set_cpu(phys_id);
  ssalloc_init();

  DS_TYPE* set = td->set;

  PF_INIT(3, SSPFD_NUM_ENTRIES, ID);

#if defined(COMPUTE_LATENCY)
  volatile ticks my_putting_succ = 0;
  volatile ticks my_putting_fail = 0;
  volatile ticks my_getting_succ = 0;
  volatile ticks my_getting_fail = 0;
  volatile ticks my_removing_succ = 0;
  volatile ticks my_removing_fail = 0;
#endif
  uint64_t my_putting_count = 0;
  uint64_t my_getting_count = 0;
  uint64_t my_removing_count = 0;

  uint64_t my_putting_count_succ = 0;
  uint64_t my_getting_count_succ = 0;
  uint64_t my_removing_count_succ = 0;
    
#if defined(COMPUTE_LATENCY) && PFD_TYPE == 0
  volatile ticks start_acq, end_acq;
  volatile ticks correction = getticks_correction_calc();
#endif
    
  seeds = seed_rand();
#if GC == 1
  alloc = (ssmem_allocator_t*) malloc(sizeof(ssmem_allocator_t));
  assert(alloc != NULL);
  ssmem_alloc_init_fs_size(alloc, SSMEM_DEFAULT_MEM_SIZE, SSMEM_GC_FREE_SET_SIZE, ID);
#endif
    
  size_t previous[initial];
  size_t my_consecutive = 0;


  RR_INIT(phys_id);
  barrier_cross(&barrier);

  int c = 0;
  uint32_t scale_rem = (uint32_t) (update_rate * UINT_MAX);
  uint32_t scale_put = (uint32_t) (put_rate * UINT_MAX);

  MEM_BARRIER;

  size_t fair_delay = 0;
  if (num_threads > 1)
    {
      fair_delay = ((num_threads - 1) * 128) + ((num_threads-1)<<5);
      if (ID == 0)
	{
	  printf(" /// fair delay: %zu\n", fair_delay);
	}
    }

  barrier_cross(&barrier_global);

  RR_START_SIMPLE();

  while (stop == 0) 
    {
      c = (uint32_t)(my_random(&(seeds[0]),&(seeds[1]),&(seeds[2])));
      int key = (c & rand_max);

      optik_t* cur = set + key;
      COMPILER_NO_REORDER(optik_t version = *cur;);

      if (unlikely(c <= scale_put))
	{
	  int res;
	  START_TS(1);
	  res = optik_lock_version(cur, version);
	  END_TS(1, my_putting_count);
	  if(res)
	    {
	      ADD_DUR(my_putting_succ);
	      if (test_verbose)
		{
		  size_t c = global_counter[key]++;
		  if (c == (previous[key] + 1))
		    {
		      my_consecutive++;
		    }
		  previous[key] = c;
		}
	      my_putting_count_succ++;
	    }
	  ADD_DUR_FAIL(my_putting_fail);
	  my_putting_count++;
	  optik_unlock(cur);
	} 
      else if(unlikely(c <= scale_rem))
	{
	  START_TS(2);
	  optik_lock(cur);
	  END_TS(2, my_removing_count);
	  ADD_DUR(my_removing_succ);
	  my_removing_count_succ++;
	  if (test_verbose)
	    {
	      size_t c = global_counter[key]++;
	      if (c == (previous[key] + 1))
		{
		  my_consecutive++;
		}
	      previous[key] = c;
	    }
	  /* } */
	  //	  ADD_DUR_FAIL(my_removing_fail);
	  my_removing_count++;
	  optik_unlock(cur);
	}
      else
	{ 
	  int res;
	  START_TS(0);
	  res = optik_trylock_version(cur, version);
	  END_TS(0, my_getting_count);
	  if(res != 0) 
	    {
	      if (test_verbose)
		{
		  size_t c = global_counter[key]++;
		  if (c == (previous[key] + 1))
		    {
		      my_consecutive++;
		    }
		  previous[key] = c;
		}
	      optik_unlock(cur);
	      ADD_DUR(my_getting_succ);
	      my_getting_count_succ++;

	    }
	  ADD_DUR_FAIL(my_getting_fail);
	  my_getting_count++;
	}

      cdelay(fair_delay);
    }

  barrier_cross(&barrier);
  RR_STOP_SIMPLE();

  barrier_cross(&barrier);

#if defined(COMPUTE_LATENCY)
  putting_succ[ID] += my_putting_succ;
  putting_fail[ID] += my_putting_fail;
  getting_succ[ID] += my_getting_succ;
  getting_fail[ID] += my_getting_fail;
  removing_succ[ID] += my_removing_succ;
  removing_fail[ID] += my_removing_fail;
#endif
  putting_count[ID] += my_putting_count;
  getting_count[ID] += my_getting_count;
  removing_count[ID]+= my_removing_count;

  putting_count_succ[ID] += my_putting_count_succ;
  getting_count_succ[ID] += my_getting_count_succ;
  removing_count_succ[ID]+= my_removing_count_succ;
  consecutive[ID] += my_consecutive;

  OPTIK_STATS_PUBLISH();

  EXEC_IN_DEC_ID_ORDER(ID, num_threads)
    {
      print_latency_stats(ID, SSPFD_NUM_ENTRIES, print_vals_num);
    }
  EXEC_IN_DEC_ID_ORDER_END(&barrier);

  SSPFDTERM();
#if GC == 1
  ssmem_term();
  free(alloc);
#endif

  pthread_exit(NULL);
}

int
main(int argc, char **argv) 
{
  printf("# using: %s\n", optik_get_type_name());

  set_cpu(0);
  ssalloc_init();
  seeds = seed_rand();

  struct option long_options[] = {
    // These options don't set a flag
    {"help",                      no_argument,       NULL, 'h'},
    {"duration",                  required_argument, NULL, 'd'},
    {"initial-size",              required_argument, NULL, 'i'},
    {"num-threads",               required_argument, NULL, 'n'},
    {"range",                     required_argument, NULL, 'r'},
    {"update-rate",               required_argument, NULL, 'u'},
    {"num-buckets",               required_argument, NULL, 'b'},
    {"vals-pf",                   required_argument, NULL, 'f'},
    {"verbose",                   no_argument,       NULL, 'v'},
    {NULL, 0, NULL, 0}
  };

  int i, c;
  while(1) 
    {
      i = 0;
      c = getopt_long(argc, argv, "hAf:d:i:n:r:s:u:m:a:l:p:b:vf:", long_options, &i);
		
      if(c == -1)
	break;
		
      if(c == 0 && long_options[i].flag == 0)
	c = long_options[i].val;
		
      switch(c) 
	{
	case 0:
	  /* Flag is automatically set */
	  break;
	case 'h':
	  printf("ASCYLIB -- stress test "
		 "\n"
		 "\n"
		 "Usage:\n"
		 "  %s [options...]\n"
		 "\n"
		 "Options:\n"
		 "  -h, --help\n"
		 "        Print this message\n"
		 "  -d, --duration <int>\n"
		 "        Test duration in milliseconds\n"
		 "  -i, --initial-size <int>\n"
		 "        Number of elements to insert before test\n"
		 "  -n, --num-threads <int>\n"
		 "        Number of threads\n"
		 "  -r, --range <int>\n"
		 "        Range of integer values inserted in set\n"
		 "  -u, --update-rate <int>\n"
		 "        Percentage of update transactions\n"
		 "  -p, --put-rate <int>\n"
		 "        Percentage of put update transactions (should be less than percentage of updates)\n"
		 "  -b, --num-buckets <int>\n"
		 "        Number of initial buckets (stronger than -l)\n"
		 "  -f, --val-pf <int>\n"
		 "        When using detailed profiling, how many values to keep track of.\n"
		 "  -v, --verbose\n"
		 "        Print more detailed output.\n"
		 , argv[0]);
	  exit(0);
	case 'd':
	  duration = atoi(optarg);
	  break;
	case 'i':
	  initial = atoi(optarg);
	  break;
	case 'n':
	  num_threads = atoi(optarg);
	  break;
	case 'r':
	  range = atol(optarg);
	  break;
	case 'u':
	  update = atof(optarg);
	  break;
	case 'p':
	  put_explicit = 1;
	  put = atof(optarg);
	  break;
	case 'v':
	  test_verbose = 1;
	  break;
	case 'f':
	  pf_vals_num = pow2roundup(atoi(optarg)) - 1;
	  break;
	case '?':
	default:
	  printf("Use -h or --help for help\n");
	  exit(1);
	}
    }


  if (!is_power_of_two(initial))
    {
      size_t initial_pow2 = pow2roundup(initial);
      printf("** rounding up initial (to make it power of 2): old: %zu / new: %zu\n", initial, initial_pow2);
      initial = initial_pow2;
    }

  if (range < initial)
    {
      range = 2 * initial;
    }

  printf("## Initial: %zu / Range: %zu\n", initial, range);

  double kb = initial * sizeof(DS_NODE) / 1024.0;
  double mb = kb / 1024.0;
  if (test_verbose)
    {
      printf("Sizeof initial: %.2f KB = %.2f MB\n", kb, mb);
    }

  if (!is_power_of_two(range))
    {
      size_t range_pow2 = pow2roundup(range);
      printf("** rounding up range (to make it power of 2): old: %zu / new: %zu\n", range, range_pow2);
      range = range_pow2;
    }

  if (put > update)
    {
      put = update;
    }

  update_rate = update / 100.0;

  if (put_explicit)
    {
      put_rate = put / 100.0;
    }
  else
    {
      put_rate = update_rate / 2;
    }

  get_rate = 1 - update_rate;

  /* printf("num_threads = %u\n", num_threads); */
  /* printf("cap: = %u\n", num_buckets); */
  /* printf("num elem = %u\n", num_elements); */
  /* printf("filing rate= %f\n", filling_rate); */
  /* printf("update = %f (putting = %f)\n", update_rate, put_rate); */


  rand_max = initial - 1;
    
  struct timeval start, end;
  struct timespec timeout;
  timeout.tv_sec = duration / 1000;
  timeout.tv_nsec = (duration % 1000) * 1000000;
    
  stop = 0;
    
  DS_TYPE* set = DS_NEW();
  assert(set != NULL);

  global_counter = calloc(initial, sizeof(size_t));

  /* Initializes the local data */
  putting_succ = (ticks *) calloc(num_threads , sizeof(ticks));
  putting_fail = (ticks *) calloc(num_threads , sizeof(ticks));
  getting_succ = (ticks *) calloc(num_threads , sizeof(ticks));
  getting_fail = (ticks *) calloc(num_threads , sizeof(ticks));
  removing_succ = (ticks *) calloc(num_threads , sizeof(ticks));
  removing_fail = (ticks *) calloc(num_threads , sizeof(ticks));
  putting_count = (ticks *) calloc(num_threads , sizeof(ticks));
  putting_count_succ = (ticks *) calloc(num_threads , sizeof(ticks));
  getting_count = (ticks *) calloc(num_threads , sizeof(ticks));
  getting_count_succ = (ticks *) calloc(num_threads , sizeof(ticks));
  removing_count = (ticks *) calloc(num_threads , sizeof(ticks));
  removing_count_succ = (ticks *) calloc(num_threads , sizeof(ticks));
  consecutive = (ticks *) calloc(num_threads , sizeof(ticks));
    
  pthread_t threads[num_threads];
  pthread_attr_t attr;
  int rc;
  void *status;
    
  barrier_init(&barrier_global, num_threads + 1);
  barrier_init(&barrier, num_threads);
    
  /* Initialize and set thread detached attribute */
  pthread_attr_init(&attr);
  pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);
    
  thread_data_t* tds = (thread_data_t*) malloc(num_threads * sizeof(thread_data_t));

  long t;
  for(t = 0; t < num_threads; t++)
    {
      tds[t].id = t;
      tds[t].set = set;
      rc = pthread_create(&threads[t], &attr, test, tds + t);
      if (rc)
	{
	  printf("ERROR; return code from pthread_create() is %d\n", rc);
	  exit(-1);
	}
        
    }
    
  /* Free attribute and wait for the other threads */
  pthread_attr_destroy(&attr);
    
  barrier_cross(&barrier_global);
  gettimeofday(&start, NULL);
  nanosleep(&timeout, NULL);

  stop = 1;
  gettimeofday(&end, NULL);
  duration = (end.tv_sec * 1000 + end.tv_usec / 1000) - (start.tv_sec * 1000 + start.tv_usec / 1000);
    
  for(t = 0; t < num_threads; t++) 
    {
      rc = pthread_join(threads[t], &status);
      if (rc) 
	{
	  printf("ERROR; return code from pthread_join() is %d\n", rc);
	  exit(-1);
	}
    }

  free(tds);
    
  ticks putting_suc_total = 0;
  ticks putting_fal_total = 0;
  ticks getting_suc_total = 0;
  ticks getting_fal_total = 0;
  ticks removing_suc_total = 0;
  ticks removing_fal_total = 0;
  uint64_t putting_count_total = 0;
  uint64_t putting_count_total_succ = 0;
  uint64_t getting_count_total = 0;
  uint64_t getting_count_total_succ = 0;
  uint64_t removing_count_total = 0;
  uint64_t removing_count_total_succ = 0;
  uint64_t consecutive_total = 0;
    
  for (t = 0; t < num_threads; t++) 
    {
      if (test_verbose)
	{
	  printf("t%-2ld %-10zu %-10zu %-10zu %-10zu %-10zu %-10zu -- %zu\n", t,
		 getting_count[t], getting_count_succ[t],
		 putting_count[t], putting_count_succ[t],
		 removing_count[t], removing_count_succ[t],
		 consecutive[t]
		 );
	  consecutive_total += consecutive[t];
	}
      putting_suc_total += putting_succ[t];
      putting_fal_total += putting_fail[t];
      getting_suc_total += getting_succ[t];
      getting_fal_total += getting_fail[t];
      removing_suc_total += removing_succ[t];
      removing_fal_total += removing_fail[t];
      putting_count_total += putting_count[t];
      putting_count_total_succ += putting_count_succ[t];
      getting_count_total += getting_count[t];
      getting_count_total_succ += getting_count_succ[t];
      removing_count_total += removing_count[t];
      removing_count_total_succ += removing_count_succ[t];
    }

#if defined(COMPUTE_LATENCY)
  printf("#thread srch_suc srch_fal insr_suc insr_fal remv_suc remv_fal   ## latency (in cycles) \n"); fflush(stdout);
  long unsigned get_suc = (getting_count_total_succ) ? getting_suc_total / getting_count_total_succ : 0;
  long unsigned get_fal = (getting_count_total - getting_count_total_succ) ? getting_fal_total / (getting_count_total - getting_count_total_succ) : 0;
  long unsigned put_suc = putting_count_total_succ ? putting_suc_total / putting_count_total_succ : 0;
  long unsigned put_fal = (putting_count_total - putting_count_total_succ) ? putting_fal_total / (putting_count_total - putting_count_total_succ) : 0;
  long unsigned rem_suc = removing_count_total_succ ? removing_suc_total / removing_count_total_succ : 0;
  long unsigned rem_fal = (removing_count_total - removing_count_total_succ) ? removing_fal_total / (removing_count_total - removing_count_total_succ) : 0;
  printf("%-7zu %-8lu %-8lu %-8lu %-8lu %-8lu %-8lu\n", num_threads, get_suc, get_fal, put_suc, put_fal, rem_suc, rem_fal);
#endif
    
#define LLU long long unsigned int

  printf("    : %-10s | %-10s | %-11s | %-13s | %s\n", "total", "success", "succ %", "total %", "effective %");
  uint64_t total = putting_count_total + getting_count_total + removing_count_total;
  size_t total_succ = getting_count_total_succ + putting_count_total_succ + removing_count_total_succ;
  double putting_perc = 100.0 * (1 - ((double)(total - putting_count_total) / total));
  double putting_perc_succ = (1 - (double) (putting_count_total - putting_count_total_succ) / putting_count_total) * 100;
  double getting_perc = 100.0 * (1 - ((double)(total - getting_count_total) / total));
  double getting_perc_succ = (1 - (double) (getting_count_total - getting_count_total_succ) / getting_count_total) * 100;
  double removing_perc = 100.0 * (1 - ((double)(total - removing_count_total) / total));
  double removing_perc_succ = (1 - (double) (removing_count_total - removing_count_total_succ) / removing_count_total) * 100;
  printf("tryv: %-10llu | %-10llu | %10.1f%% | %12.3f%% | \n", (LLU) getting_count_total, 
	 (LLU) getting_count_total_succ,  getting_perc_succ, getting_perc);
  printf("locv: %-10llu | %-10llu | %10.1f%% | %12.3f%% | %10.1f%%\n", (LLU) putting_count_total, 
	 (LLU) putting_count_total_succ, putting_perc_succ, putting_perc, (putting_perc * putting_perc_succ) / 100);
  printf("lock: %-10llu | %-10llu | %10.1f%% | %12.3f%% | %10.1f%%\n", (LLU) removing_count_total, 
	 (LLU) removing_count_total_succ, removing_perc_succ, removing_perc, (removing_perc * removing_perc_succ) / 100);

  double throughput = (putting_count_total + getting_count_total + removing_count_total) * 1000.0 / duration;
  double throughput_succ = total_succ * 1000.0 / duration;

  size_t gc_tot = 0;
  for (i = 0; i <= rand_max; i++)
    {
      gc_tot += global_counter[i];
    }

  printf("#counter    %10zu\n"
	 "   vs. succ %10zu = %-3s\n",
	 gc_tot, total_succ, (gc_tot == total_succ) ? "OK" : "NO");
  printf("#txs       %zu\t(%-10.0f\n", num_threads, throughput);
  printf("#txs succ  %zu\t(%-10.0f\n", num_threads, throughput_succ);
  printf("#Mops      %.3f\n", throughput / 1e6);
  if (test_verbose)
    {
      printf("#Mops succ %-8.3f / consecutive %.2f%%\n", throughput_succ / 1e6, 100.0 * consecutive_total / total_succ);
    }
  else
    {
      printf("#Mops succ %-8.3f\n", throughput_succ / 1e6);
    }
  RR_PRINT_UNPROTECTED(RAPL_PRINT_POW);
  RR_PRINT_CORRECTED();

  OPTIK_STATS_PRINT_DUR(duration);

  pthread_exit(NULL);
    
  return 0;
}
