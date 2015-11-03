#include <usloss.h>
#include <phase1.h>
#include <phase2.h>
#include <phase3.h>
#include <phase4.h>
#include <stdlib.h> /* needed for atoi() */
#include <usyscall.h>
#include <stdio.h>
#include <string.h>
#include <provided_prototypes.h>
#include <ctype.h>


/* ------------------------- Prototypes ----------------------------------- */
static int	ClockDriver(char *);
static int	DiskDriver(char *);
static int TermDriver(char *);
void diskSize(systemArgs *args);
void termRead(systemArgs *args);
void termWrite(systemArgs *args);
void diskSizeReal(int unitNum, int * sectorSize, int * numSectors, int * numTracks);
void sleep(systemArgs *args);
void diskRead(systemArgs *args);
void diskWrite(systemArgs *args);

/* -------------------------- Globals ------------------------------------- */
struct ProcStruct pFourProcTable[MAXPROC];
struct clockWaiter clockWaitLine[MAXPROC];
int running;
int debugFlag = 0;
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
    /* Check kernel mode here. */


    /* initialize proctable */
    for(i = getpid(); i < MAXPROC; i++){
    	pFourProcTable[i].status = INACTIVE;
    	pFourProcTable[i].procMbox=-1;
    	pFourProcTable[i].parentPid=-1;
		memset(pFourProcTable[i].children, INACTIVE, sizeof(pFourProcTable[i].children));
    }

    /* add syscalls to syscallVec */
    systemCallVec[SYS_SLEEP] = sleep;
    systemCallVec[SYS_DISKREAD] = diskRead;
    systemCallVec[SYS_DISKWRITE] = diskWrite;
    systemCallVec[SYS_DISKSIZE] = diskSize;
    systemCallVec[SYS_TERMREAD] = diskSize;
    systemCallVec[SYS_TERMWRITE] = diskSize;

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

    char buf[50];
    for (i = 0; i < DISK_UNITS; i++) {
        sprintf(buf, "Disk Driver #%d", i);
        pid = fork1(buf, DiskDriver, NULL, USLOSS_MIN_STACK, 2);
        if (pid < 0) {
            USLOSS_Console("start3(): Can't create disk driver %d\n", i+1);
            USLOSS_Halt(1);
        }
    }
    sempReal(running);
    sempReal(running);

    /* Create terminal device drivers. */
    for(i=0; i < TERM_UNITS; i++){
        sprintf(buf, "Terminal Driver #%d", i);
    	pid = fork1(buf, TermDriver, NULL, USLOSS_MIN_STACK, 2);
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

static int ClockDriver(char *arg)
{
    int result;
    int status;

    // Let the parent know we are running and enable interrupts.
    semVReal(running);
    USLOSS_PsrSet(USLOSS_PsrGet() | USLOSS_PSR_CURRENT_INT);

    // Infinite loop until we are zap'd
    while(! isZapped()) {
        result = waitdevice(USLOSS_CLOCK_DEV, 0, &status);
        if (result != 0) {
            return 0;
            /* Compute the current time and wake up any processes whose time has come. */
        }
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

void diskSize(systemArgs *args){
    if (debugFlag){
        USLOSS_Console("diskSize(): started.\n");
    }
    if(args->number != SYS_DISKSIZE){
        if (debugFlag){
            USLOSS_Console("diskSize(): Attempted to call diskSize with wrong sys call number: %d.\n", args->number);
        }
        toUserMode();
        return;
    }
    int unitNum = args->arg1;
    int sectorSize;
    int numTracks;
    int numSectors;
    
    diskSizeReal(unitNum, &sectorSize, &numSectors, &numTracks);
    
    args->arg1 = sectorSize;
    args->arg2 = numSectors;
    args->arg3 = numTracks;
}

void diskSizeReal(int unitNum, int * sectorSize, int * numSectors, int * numTracks){
    if (debugFlag){
        USLOSS_Console("diskSizeReal(): started.\n");
    }
    /* Getting numTracks */
    USLOSS_DeviceRequest request;
    request.opr = USLOSS_DISK_TRACKS;
    request.reg1 = numTracks;
    USLOSS_Device_output(USLOSS_DISK_DEV, unitNum, request);
    
    /* Getting sectorSize */
    *sectorSize = 512;
    
    /* Getting numSectors */
    *numSectors = 16;
    
    if (debugFlag){
        USLOSS_Console("diskSizeReal(): ended.\n");
    }
    
}
    /*
     Read a line from a terminal (termRead).
     Input
     arg1: address of the user’s line buffer.
     arg2: maximum size of the buffer.
     arg3: the unit number of the terminal from which to read.
     Output
     arg2: number of characters read.
     arg4: -1 if illegal values are given as input; 0 otherwise.
     */
     
void termRead(systemArgs *args){
    if (debugFlag){
        USLOSS_Console("termRead(): started.\n");
    }
    if(args->number != SYS_TERMREAD){
        if (debugFlag){
            USLOSS_Console("termRead(): Attempted to call termRead with wrong sys call number: %d.\n", args->number);
        }
        toUserMode();
        return;
    }
    char * address;
    int maxSize;
    int unitNum;
    address = (char *)args->arg1;
    maxSize = (int)args->arg2;
    unitNum = (int)args->arg3;
    int charsRead;
    charsRead = termReadReal(address, maxSize, unitNum);
    args -> arg2 = charsRead;
    
}

int termReadReal(char * address, int maxSize, int unitNum){
    int status;
    char msg[maxSize];
    int i=0;
    int charsRead = 0;
    // Get the full line from terminal
    while(1){
        USLOSS_DeviceInput(USLOSS_TERM_DEV, unitNum, &status);
        // If there is a character waiting
        if(USLOSS_TERM_STAT_RECV(status) == USLOSS_DEV_BUSY){
            msg[i] = USLOSS_TERM_STAT_CHAR(status);
            i++;
            if (USLOSS_TERM_STAT_CHAR(status) == '\n' || USLOSS_TERM_STAT_CHAR(status) == '\0'){
                break;
            }
            else{
                charsRead++;
            }
        }
        else if (USLOSS_TERM_STAT_RECV(status) == USLOSS_DEV_READY){
            continue;
        }
        else{
            if(debugFlag){
                USLOSS_Console("termReadReal(): Error in USLOSS_DeviceInput getting character.\n");
            }
        }
    }
    if(debugFlag){
        USLOSS_Console("termReadReal(): finished reading line: ");
        int j = 0;
        for(j=0;j<strlen(msg);j++){
            USLOSS_Console("%c", msg[j]);
        }
        USLOSS_Console("\n");
        
    }
    msg[i] = '\0';
    strcpy(address, msg);
    return charsRead;
    
}
/*
 Write a line to a terminal (termWrite).
 Input
 arg1: address of the user’s line buffer.
 arg2: number of characters to write.
 arg3: the unit number of the terminal to which to write.
 Output
 arg2: number of characters written.
 arg4: -1 if illegal values are given as input; 0 otherwise
 */
void termWrite(systemArgs *args){
    if (debugFlag){
        USLOSS_Console("termWrite(): started.\n");
    }
    if(args->number != SYS_TERMWRITE){
        if (debugFlag){
            USLOSS_Console("termWrite(): Attempted to call termWrite with wrong sys call number: %d.\n", args->number);
        }
        toUserMode();
        return;
    }
    
    char * address;
    int numChars;
    int unitNum;
    address = (char *)args->arg1;
    numChars = (int)args->arg2;
    unitNum = (int)args->arg3;
    int numWritten;
    
    numWritten = termWriteReal(address, numChars, unitNum);
    
    args->arg2 = numWritten;
}

int termWriteReal(char * address, int numChars, int unitNum){
    
}




/* ------------------------------------------------------------------------
   Name		-	sleep
   Purpose	-
   Params 	-	a struct of arguments; args[1] contains the number of
   	   	   	   	seconds the process will sleep
   Returns	-	placed into 4th position in argument struct; -1 if input
   Side Effects	-
   ----------------------------------------------------------------------- */
void sleep(systemArgs *args){
	if (debugFlag)
		USLOSS_Console("sleep(): started.\n");
	/* verify that the specified interrupt number is correct */
	if(args->number != SYS_SLEEP){
		if (debugFlag)
			USLOSS_Console("sleep(): Attempted a \"sleep\" operation with wrong sys call number: %d.\n", args->number);
		toUserMode();
		return;
	}
	/* check to make sure that the specified number of seconds is >= 1 and is an integer */
	if(!isdigit(args->arg1) || args->arg1 < 1){
		if (debugFlag)
			USLOSS_Console("sleep(): Invalid number of seconds specified for sleep operation: %d.\n", args->arg1);
		args->arg4 = -1;
		toUserMode();
		return;
	}
	/* call helper method and assign return value */
	args->arg4 = sleepHelper(args->arg1);
}

int sleepHelper(int seconds){
	procStruct * target = pFourProcTable[getPID() % MAXPROC];
	/* add a new entry to the clockWaiter table */
	clockWaiterAdd(getPID(), seconds);
	/* receive on the clockDriver mbox */
}

/* ------------------------------------------------------------------------
   Name		-
   Purpose	-
   Params 	-	a struct of arguments; args[1] contains
   Returns	-
   Side Effects	-
   ----------------------------------------------------------------------- */
void diskRead(systemArgs *args);

/* ------------------------------------------------------------------------
   Name		-
   Purpose	-
   Params 	-	a struct of arguments; args[1] contains
   Returns	-
   Side Effects	-
   ----------------------------------------------------------------------- */
void diskWrite(systemArgs *args);
