#ifndef COUNTER_H
#define COUNTER_H
#include <types.h> //adding types.h to counter.h to tackle bool
#include <synch.h> // Ensure you include the header for synchronization primitives.

/* A data structure representing a synchronised counter */

struct sync_counter
{
   int counter;
   /* You can add any further data types/structures here
      that may be required to synchronise your counters */
   struct lock *lock; // Add a lock to the structure.
};

/* The function interface to the synchronised counter.
   See counter.c for a description of the function behaviour. */

extern void counter_increment(struct sync_counter *);
extern void counter_decrement(struct sync_counter *);
extern struct sync_counter *counter_initialise(int val);
extern int counter_read_and_destroy(struct sync_counter *);

#endif
