/*
 *  linkedlist.h
 *  
 *
 *  Created by Vincent Gramoli on 1/12/09.
 *  Copyright 2009 __MyCompanyName__. All rights reserved.
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
#include <stdint.h>

#include <atomic_ops.h>
#include "atomic_ops_if.h"


#include "tm.h"
#include "measurements.h"
#include "ssalloc.h"


#ifdef DEBUG
#define IO_FLUSH                        fflush(NULL)
/* Note: stdio is thread-safe */
#endif

#define DEFAULT_DURATION                1000
#define DEFAULT_INITIAL                 1024
#define DEFAULT_NB_THREADS              1
#define DEFAULT_RANGE                   (2 * DEFAULT_INITIAL)
#define DEFAULT_SEED                    0
#define DEFAULT_UPDATE                  20
#define DEFAULT_ELASTICITY		4
#define DEFAULT_ALTERNATE		0
#define DEFAULT_EFFECTIVE		1

#define XSTR(s)                         STR(s)
#define STR(s)                          #s

static volatile AO_t stop;

#define TRANSACTIONAL                   d->unit_tx

typedef intptr_t val_t;
#define VAL_MIN                         INT_MIN
#define VAL_MAX                         INT_MAX

typedef ALIGNED(64) struct node 
{
  val_t val;
  struct node *next;
#if defined(DO_PAD)
  uint8_t padding[64 - 16];
#endif
} node_t;

typedef ALIGNED(64) struct intset 
{
  node_t *head;
} intset_t;

node_t *new_node(val_t val, node_t *next, int transactional);
intset_t *set_new();
void set_delete(intset_t *set);
int set_size(intset_t *set);