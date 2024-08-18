/* This file will contain your solution. Modify it as you wish. */
#include <types.h>
#include <lib.h>
#include <synch.h>
#include "cdrom.h"

/* Some variables shared between threads */

/*
 * cdrom_read: This is the client facing interface to the cdrom that makes
 * requests to the cdrom system.
 *
 * Args :-
 * block_num: The block number of the cdrom that the client is requesting the
 * content from.
 *
 * This function makes a request to the cdrom system using the cdrom_block_read()
 * function. cdrom_block_read() returns immediately before the request has completed.
 * cdrom_read() then puts the current thread to sleep until the request completes,
 * upon which, this thread needs to the woken up, the value from the specific block
 * obtained, and the value is return to the requesting client.
 */

struct semaphore *Available_slots;
struct lock *Lock;

typedef struct request
{
        int block_num;
        unsigned int value;
        bool completed;
        struct cv *cv;
} request;

struct request *requests[MAX_CONCURRENT_REQ]; // Array of request pointers.

unsigned int cdrom_read(int block_num)
{
        P(Available_slots);
        lock_acquire(Lock);

        unsigned int value = 0;
        int Slot_index = -1; // Initialize value.

        kprintf("Received request read block %d\n", block_num);

        // Find a slot for the new request.
        for (int i = 0; i < MAX_CONCURRENT_REQ; i++)
        {
                if (requests[i] == NULL)
                {
                        requests[i] = (request *)kmalloc(sizeof(request));
                        requests[i]->block_num = block_num;
                        requests[i]->completed = false;
                        requests[i]->cv = cv_create("req_cv");
                        Slot_index = i;
                        break;
                }
        }

        if (Slot_index == -1)
        {
                panic("cdrom_read: No available slot found");
        }

        cdrom_block_request(requests[Slot_index]->block_num);

        // Wait for the request to be completed by the handler.
        while (requests[Slot_index] != NULL && !requests[Slot_index]->completed)
        {
                cv_wait(requests[Slot_index]->cv, Lock);
        }

        value = requests[Slot_index]->value;

        // Clean up.
        cv_destroy(requests[Slot_index]->cv);
        kfree(requests[Slot_index]);
        requests[Slot_index] = NULL; // Clear the request slot.

        // Signal that there's now an available slot for new requests.
        V(Available_slots);

        lock_release(Lock);

        return value;
}

/*
 * cdrom_handler: This function is called by the system each time a cdrom block request
 * has completed.
 *
 * Args:-
 * block_num: the number of the block originally requested from the cdrom.
 * value: the content of the block, i.e. the data value retrieved by the request.
 *
 * The cdrom_handler runs in its own thread. The handler needs to deliver the value
 * to the original requestor and wake the requestor waiting on the value.
 */

void cdrom_handler(int block_num, unsigned int value)
{
        lock_acquire(Lock);

        for (int i = 0; i < MAX_CONCURRENT_REQ; i++)
        {
                if (requests[i] != NULL && requests[i]->block_num == block_num && !requests[i]->completed)
                {
                        requests[i]->value = value;
                        requests[i]->completed = true;
                        cv_signal(requests[i]->cv, Lock);
                        break;
                }
        }
        lock_release(Lock);
}

/*
 * cdrom_startup: This is called at system initilisation time,
 * before the various threads are started. Use it to initialise
 * or allocate anything you need before the system starts.
 */

void cdrom_startup(void)
{
        Lock = lock_create("Lock");
        Available_slots = sem_create("Available_slots", MAX_CONCURRENT_REQ);
        for (int i = 0; i < MAX_CONCURRENT_REQ; i++)
        {
                requests[i] = NULL;
        }
}

/*
 * cdrom_shutdown: This is called after all the threads in the system
 * have completed. Use this function to clean up and de-allocate anything
 * you set up in cdrom_startup()
 */
void cdrom_shutdown(void)
{
        lock_destroy(Lock);
        sem_destroy(Available_slots);
        for (int i = 0; i < MAX_CONCURRENT_REQ; i++)
        {
                if (requests[i] != NULL)
                {
                        cv_destroy(requests[i]->cv);
                        kfree(requests[i]);
                        requests[i] = NULL;
                }
        }
}

/* Just a sanity check to warn about including the internal header file
   It contains nothing relevant to a correct solution and any use of
   what is contained is likely to result in failure in our testing
   */

#if defined(CDROM_TESTER_H)
#error Do not include the cdrom_tester header file
#endif