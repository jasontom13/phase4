#include <usloss.h>
#include <phase1.h>
#include <phase2.h>
#include <phase3.h>
#include <phase4.h>
#include <stdlib.h> /* needed for atoi() */

semaphore 	running;

/* ------------------------- Prototypes ----------------------------------- */
static int	ClockDriver(char *);
static int	DiskDriver(char *);
static int TermDriver(char *);

/* -------------------------- Globals ------------------------------------- */
struct ProcStruct pFourProcTable[MAXPROC];

/* ------------------------------------------------------------------------ */

void
start3(void)
{
    char	name[128];
    char        termbuf[10];
    int		i;
    int		clockPID;
    int		pid;
    int		status;
    /*
     * Check kernel mode here.
     */


    /* initialize proctable */
    for(i = getpid(); i < MAXPROC; i++){
    	pFourProcTable[i].status = INACTIVE;
    	pFourProcTable[i].procMbox=-1;
    	pFourProcTable[i].parentPid=-1;
		memset(pFourProcTable[i].children, INACTIVE, sizeof(pFourProcTable[i].children));
    }

    /* add syscalls to syscallVec */
    systemCallVec[SYS_SLEEP] = sleep;
    systemCallVec[USLOSS_DISK_READ] = diskRead;
    systemCallVec[USLOSS_DISK_WRITE] = diskWrite;
    systemCallVec[USLOSS_DISK_SIZE] = diskSize;



    /*
     * Create clock device driver 
     * I am assuming a semaphore here for coordination.  A mailbox can
     * be used instead -- your choice.
     */
    running = semcreateReal(0);
    clockPID = fork1("Clock driver", ClockDriver, NULL, USLOSS_MIN_STACK, 2);
    if (clockPID < 0) {
	USLOSS_Console("start3(): Can't create clock driver\n");
	USLOSS_Halt(1);
    }
    /*
     * Wait for the clock driver to start. The idea is that ClockDriver
     * will V the semaphore "running" once it is running.
     */

    sempReal(running);

    /*
     * Create the disk device drivers here.  You may need to increase
     * the stack size depending on the complexity of your
     * driver, and perhaps do something with the pid returned.
     */

    for (i = 0; i < DISK_UNITS; i++) {
        sprintf(buf, "%d", i);
        pid = fork1(strcat("Disk Driver ", itoa(i + 1)), DiskDriver, buf, USLOSS_MIN_STACK, 2);
        if (pid < 0) {
            USLOSS_Console("start3(): Can't create disk driver %d\n", i+1);
            USLOSS_Halt(1);
        }
    }
    sempReal(running);
    sempReal(running);

    /* Create terminal device drivers. */
    for(i=0; i < TERM_UNITS; i++){
    	pid = fork1(strcat("Terminal Driver", itoa(i+1)), TermDriver, NULL, USLOSS_MIN_STACK, 2);
    	if (pid < 0) {
			USLOSS_Console("start3(): Can't create terminal driver %d\n", i+1);
			USLOSS_Halt(1);
		}
    }


    /*
     * Create first user-level process and wait for it to finish.
     * These are lower-case because they are not system calls;
     * system calls cannot be invoked from kernel mode.
     * I'm assuming kernel-mode versions of the system calls
     * with lower-case first letters.
     */
    pid = spawnReal("start4", start4, NULL, 4 * USLOSS_MIN_STACK, 3);
    pid = waitReal(&status);

    /*
     * Zap the device drivers
     */
    zap(clockPID);  // clock driver

    // eventually, at the end:
    quit(0);
}

static int
ClockDriver(char *arg)
{
    int result;
    int status;

    // Let the parent know we are running and enable interrupts.
    semvReal(running);
    USLOSS_PsrSet(USLOSS_PsrGet() | USLOSS_PSR_CURRENT_INT);

    // Infinite loop until we are zap'd
    while(! is_zapped()) {
	result = waitdevice(CLOCK_DEV, 0, &status);
	if (result != 0) {
	    return 0;
	}
	/*
	 * Compute the current time and wake up any processes
	 * whose time has come.
	 */
    }
}

static int
DiskDriver(char *arg)
{
    int unit = atoi( (char *) arg); 	// Unit is passed as arg.
    return 0;
}

static int TermDriver(char * arg){

	return 0;

}
