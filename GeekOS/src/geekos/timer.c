/*
 * GeekOS timer interrupt support
 * Copyright (c) 2001,2003,2004 David H. Hovemeyer <daveho@cs.umd.edu>
 * Copyright (c) 2003,2013,2014 Jeffrey K. Hollingsworth <hollings@cs.umd.edu>
 *
 * All rights reserved.
 *
 * This code may not be resdistributed without the permission of the copyright holders.
 * Any student solutions using any of this code base constitute derviced work and may
 * not be redistributed in any form.  This includes (but is not limited to) posting on
 * public forums or web sites, providing copies to (past, present, or future) students
 * enrolled in similar operating systems courses the University of Maryland's CMSC412 course.
 */

#include <geekos/kassert.h>
#include <geekos/projects.h>
#include <limits.h>
#include <geekos/io.h>
#include <geekos/int.h>
#include <geekos/irq.h>
#include <geekos/kthread.h>
#include <geekos/timer.h>
#include <geekos/smp.h>

#define MAX_TIMER_EVENTS	100

static int timerDebug = 0;
static int timeEventCount;
static int nextEventID;
static timerEvent pendingTimerEvents[MAX_TIMER_EVENTS];

/*
 * Global tick counter
 */
volatile ulong_t g_numTicks;

/*
 * Number of times the spin loop can execute during one timer tick
 */
static int s_spinCountPerTick;

/*
 * Number of ticks to wait before calibrating the delay loop.
 */
#define CALIBRATE_NUM_TICKS	3

/*
 * The default quantum; maximum number of ticks a thread can use before
 * we suspend it and choose another.
 */
#define DEFAULT_MAX_TICKS 4

/*
 * Settable quantum.
 */
unsigned int g_Quantum = DEFAULT_MAX_TICKS;

/*#define DEBUG_TIMER */
#ifdef DEBUG_TIMER
#  define Debug(args...) Print(args)
#else
#  define Debug(args...)
#endif

/* ----------------------------------------------------------------------
 * Private functions
 * ---------------------------------------------------------------------- */

void Timer_Interrupt_Handler(struct Interrupt_State *state) {
    int i;
    int id;
    struct Kernel_Thread *current = CURRENT_THREAD;

    Begin_IRQ(state);

    id = Get_CPU_ID();

    if (!id) {
        /* Update global number of ticks - only on core 0 so won't count a rate equal to number of cores */
        ++g_numTicks;
    }


    /* Update per-thread number of ticks and per core ticks */
    ++current->numTicks;
    ++current->totalTime;
    CPUs[id].ticks++;

    /*
     * If thread has been running for an entire quantum,
     * inform the interrupt return code that we want
     * to choose a new thread.
     */
    if (!id) {
        // XXXX - only core #0 is currently handling new timer events
        /* update timer events */
        for (i = 0; i < timeEventCount; i++) {
            if (pendingTimerEvents[i].ticks == 0) {
                if (timerDebug)
                    Print("timer: event %d expired (%d ticks)\n",
                          pendingTimerEvents[i].id,
                          pendingTimerEvents[i].origTicks);
                (pendingTimerEvents[i].callBack) (pendingTimerEvents[i].id);
            } else {
                pendingTimerEvents[i].ticks--;
            }
        }
    }
    if (current->numTicks >= g_Quantum) {
        g_needReschedule[id] = true;
        // TODO:
        /*
         * The current process is moved to a lower priority queue,
         * since it consumed a full quantum.
         */
    }

    End_IRQ(state);
}

/*
 * Temporary timer interrupt handler used to calibrate
 * the delay loop.
 */
static void Timer_Calibrate(struct Interrupt_State *state) {
    Begin_IRQ(state);
    if (g_numTicks < CALIBRATE_NUM_TICKS)
        ++g_numTicks;
    else {
        /*
         * Now we can look at EAX, which reflects how many times
         * the loop has executed
         */
        /*Print("Timer_Calibrate: eax==%d\n", state->eax); */
        s_spinCountPerTick = INT_MAX - state->eax;
        state->eax = 0;         /* make the loop terminate */
    }
    End_IRQ(state);
}

/*
 * Delay loop; spins for given number of iterations.
 */
static void Spin(int count) {
    /*
     * The assembly code is the logical equivalent of
     *      while (count-- > 0) { // waste some time }
     * We rely on EAX being used as the counter
     * variable.
     */

    int result;
    __asm__ __volatile__("1: decl %%eax\n\t"
                         "cmpl $0, %%eax\n\t"
                         "nop; nop; nop; nop; nop; nop\n\t"
                         "nop; nop; nop; nop; nop; nop\n\t"
                         "jg 1b":"=a"(result)
                         :"a"(count)
        );
}

/*
 * Calibrate the delay loop.
 * This will initialize s_spinCountPerTick, which indicates
 * how many iterations of the loop are executed per timer tick.
 */
static void Calibrate_Delay(void) {
    Disable_Interrupts();

    /* Install temporarily interrupt handler */
    Install_IRQ(TIMER_IRQ, &Timer_Calibrate);
    Enable_IRQ(TIMER_IRQ);

    Enable_Interrupts();

    /* Wait a few ticks */
    while (g_numTicks < CALIBRATE_NUM_TICKS);

    /*
     * Execute the spin loop.
     * The temporary interrupt handler will overwrite the
     * loop counter when the next tick occurs.
     */
    Spin(INT_MAX);

    Disable_Interrupts();

    /*
     * Mask out the timer IRQ again,
     * since we will be installing a real timer interrupt handler.
     */
    Disable_IRQ(TIMER_IRQ);
    Enable_Interrupts();
}

/* ----------------------------------------------------------------------
 * Public functions
 * ---------------------------------------------------------------------- */

void Init_Timer_Interrupt(void) {
    // Enable_IRQ(TIMER_IRQ);
    Enable_IRQ(32);
}

void Init_Timer(void) {
    /*
     * TODO: reprogram the timer to set the frequency.
     * In bochs, it defaults to 18Hz, which is actually pretty
     * reasonable.
     */

    Print("Initializing timer...\n");

    /* configure for default clock */
    // Out_Byte(0x43, 0x36);
    // Out_Byte(0x40, 0x00);
    // Out_Byte(0x40, 0x00);

    /* Calibrate for delay loop */
    // Calibrate_Delay();
    // Print("Delay loop: %d iterations per tick\n", s_spinCountPerTick);

    /* Install an interrupt handler for the timer IRQ */
    // Install_IRQ(TIMER_IRQ, &Timer_Interrupt_Handler);

    // apic timer interrupt
    Install_IRQ(32, &Timer_Interrupt_Handler);

    Init_Timer_Interrupt();
}

int Start_Timer(int ticks, timerCallback cb) {
    int returned_timer_id;
		KASSERT(!Interrupts_Enabled());
		
    if (timeEventCount == MAX_TIMER_EVENTS) {
        Print
            ("timeEventCount == %d == MAX_TIMER_EVENTS; cannot start a new timer",
             MAX_TIMER_EVENTS);
        int i;
        for (i = 0; i < MAX_TIMER_EVENTS; i++) {
            Print("%d: cb 0x%p in %d/%d ticks", i,
                  pendingTimerEvents[i].callBack,
                  pendingTimerEvents[i].ticks,
                  pendingTimerEvents[i].origTicks);
        }
        return -1;
    } else {
        returned_timer_id = nextEventID++;
        pendingTimerEvents[timeEventCount].id = returned_timer_id;
        pendingTimerEvents[timeEventCount].callBack = cb;
        pendingTimerEvents[timeEventCount].ticks = ticks;
        pendingTimerEvents[timeEventCount].origTicks = ticks;
        timeEventCount++;

        return returned_timer_id;
    }
}

int Get_Remaing_Timer_Ticks(int id) {
    int i;

    KASSERT(!Interrupts_Enabled());
    for (i = 0; i < timeEventCount; i++) {
        if (pendingTimerEvents[i].id == id) {
            return pendingTimerEvents[i].ticks;
        }
    }

    return -1;
}

int Cancel_Timer(int id) {
    int i;
    KASSERT(!Interrupts_Enabled());
    for (i = 0; i < timeEventCount; i++) {
        if (pendingTimerEvents[i].id == id) {
            pendingTimerEvents[i] = pendingTimerEvents[timeEventCount - 1];
            timeEventCount--;
            return 0;
        }
    }

    Print("timer: unable to find timer id %d to cancel it\n", id);
    return -1;
}

#define US_PER_TICK (TICKS_PER_SEC * 1000000)

/*
 * Spin for at least given number of microseconds.
 * FIXME: I'm sure this implementation leaves a lot to
 * be desired.
 */
void Micro_Delay(int us) {
    int num = us * s_spinCountPerTick;
    int denom = US_PER_TICK;

    int numSpins = num / denom;
    int rem = num % denom;

    if (rem > 0)
        ++numSpins;

    Debug("Micro_Delay(): num=%d, denom=%d, spin count = %d\n", num, denom,
          numSpins);

    Spin(numSpins);
}
