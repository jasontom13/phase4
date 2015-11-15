#include "usloss.h"
#include "phase1.h"
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
static void	DiskDriver(char *);
static int TermDriver(char *);
static int TermReader(char *);
static int TermWriter(char *);
void termRead(systemArgs *args);
int termReadReal(char * address, int maxSize, int unitNum);
void termWrite(systemArgs *args);
long termWriteReal(char * address, int numChars, int unitNum);
void diskSize(systemArgs *args);
void diskSizeReal(int unitNum, int * sectorSize, int * numSectors, int * numTracks);
void diskRead(systemArgs *args);
int diskReadReal(int unit, int track, int first, int sectors, void *buffer);
void diskWrite(systemArgs *args);
int diskWriteReal(int unit, int track, int first, int sectors, void *buffer);
void diskQueue(int opr, int unit, systemArgs *args, int pid);
struct diskProc *  diskQueueHelper(int unit, int opr, systemArgs *args, int pid, struct diskProc * next);
void toUserMode();
void sleepHelper(int seconds);
void clockWaiterAdd(int pid, int seconds);
void snooze(systemArgs *args);
/* -------------------------- Globals ------------------------------------- */
struct Terminal terminals[USLOSS_MAX_UNITS];
struct ProcStruct pFourProcTable[MAXPROC];
struct clockWaiter clockWaitLine[MAXPROC];
struct clockWaiter * clockWaiterHead;
struct diskProc diskQ[MAXPROC];
struct diskProc * diskOneHead;
struct diskProc * diskTwoHead;
struct USLOSS_DeviceRequest diskOneReq;
struct diskProc * q;
struct USLOSS_DeviceRequest diskTwoReq;
int diskOneArmPos;
int diskTwoArmPos;
int diskOneMutex;
int diskOneQueue;
int diskTwoMutex;
int diskTwoQueue;
struct USLOSS_DeviceRequest diskRW;
int running;
int debugFlag = 0;
int df = 1;
char * dummyMsg;
int cleanUp=0;
/* ------------------------------------------------------------------------ */

void start3(void){
    char	name[128];
    char        termbuf[10];
    int		i;
    int		clockPID;
    int     diskPID[DISK_UNITS];
    int		pid;
    int		status;
    /* Check kernel mode here. */

    /* initialize proctable */
    for(i = getpid(); i < MAXPROC; i++){
    	pFourProcTable[i].status = INACTIVE;
    	pFourProcTable[i].procMbox=-1;
    	pFourProcTable[i].parentPid=-1;
		memset(pFourProcTable[i].children, INACTIVE, sizeof(pFourProcTable[i].children));
		pFourProcTable[i].procMbox = MboxCreate(0,50);
    }
    /* initialize clockWaitLine */
    for(i = 0; i < MAXPROC; i++){
    	clockWaitLine[i].PID = -1;
    }
    /* set the head and the tail */
    clockWaiterHead = NULL;

    /* add syscalls to syscallVec */
    systemCallVec[SYS_SLEEP] = snooze;
    systemCallVec[SYS_DISKREAD] = diskRead;
    systemCallVec[SYS_DISKWRITE] = diskWrite;
    systemCallVec[SYS_DISKSIZE] = diskSize;
    systemCallVec[SYS_TERMREAD] = termRead;
    systemCallVec[SYS_TERMWRITE] = termWrite;

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

    // create the disk mutex semaphores
    diskOneMutex = semcreateReal(1);
    diskOneQueue = semcreateReal(0);
    diskTwoMutex = semcreateReal(1);
    diskTwoQueue = semcreateReal(0);


//    for (i = 0; i < DISK_UNITS; i++) {
//        sprintf(name, "Disk Driver #%d", i);
//        if (debugFlag)
//			USLOSS_Console("start3(): Forking disk driver\t%d\tunder name %s\n",i,name);
//        pid = fork1(name, DiskDriver, NULL, USLOSS_MIN_STACK, 2);
//        if (pid < 0) {
//            USLOSS_Console("start3(): Can't create disk driver %d\n", i+1);
//            USLOSS_Halt(1);
//        }
//    }
//    sempReal(running);
//    sempReal(running);

    //char unitNum[50];
    /* Create terminal device drivers. */
    for(i=0; i < USLOSS_TERM_UNITS; i++){

        sprintf(name, "Terminal Driver #%d", i);
        sprintf(termbuf, "%d", i);
        
    	pid = fork1(name, TermDriver, termbuf, USLOSS_MIN_STACK, 2);
    	if (pid < 0) {
			USLOSS_Console("start3(): Can't create terminal driver %d\n", i);
			USLOSS_Halt(1);
		}
        terminals[i].pid = pid;
        sprintf(name, "Terminal Reader #%d", i);
        pid = fork1(name, TermReader, termbuf, USLOSS_MIN_STACK, 2);
        if (pid < 0) {
            USLOSS_Console("start3(): Can't create terminal reader %d\n", i);
            USLOSS_Halt(1);
        }
        terminals[i].readerPid = pid;
        sprintf(name, "Terminal Writer #%d", i);
        pid = fork1(name, TermWriter, termbuf, USLOSS_MIN_STACK, 2);
        if (pid < 0) {
            USLOSS_Console("start3(): Can't create terminal writer %d\n", i);
            USLOSS_Halt(1);
        }
        terminals[i].writerPid = pid;
        sempReal(running);
        sempReal(running);
        sempReal(running);
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

    if (debugFlag)
    		USLOSS_Console("start3(): returned from wait\n");

    cleanUp=1;
    /*
     * Zap the device drivers
     */
    
    char data[MAXLINE];
    int len;
    // Terminals
    if(debugFlag){
        USLOSS_Console("start3(): Beginning to quit Terminal Drivers.\n");
    }
    for (i=0; i< USLOSS_TERM_UNITS; i++){
        MboxSend(terminals[i].writeBox, "", 0);
        //zap(terminals[i].writerPid);
        MboxSend(terminals[i].inBox, "", 0);
        //zap(terminals[i].readerPid);
        char fileName[9];
        sprintf(fileName, "term%d.in",i);
        FILE * fp;
        fp = fopen (fileName, "a+");
        fprintf(fp, "%s %s %s %d", "We", "are", "in", 2015);
        fflush(fp);
        fclose(fp);
        int control;
        int result;
        control = 0;
        control = USLOSS_TERM_CTRL_RECV_INT(control);
        result = USLOSS_DeviceOutput(USLOSS_TERM_DEV, i, control);
        if(debugFlag){
            USLOSS_Console("We're here\n");
        }
        zap(terminals[i].pid);
        

        //zap(terminals[i].pid);
        if (debugFlag)
            		USLOSS_Console("start3(): coming back from first Mbox send.\n");
        //zap(terminals[i+1].writerPid);
        if (debugFlag)
            		USLOSS_Console("start3(): finished zapping first terminal\n");
//        MboxSend(terminals[i+1].inBox, "", 0);
//        zap(terminals[i+1].readerPid);
//        
//        zap(terminals[i+1].pid);

//        if (debugFlag)
//            		USLOSS_Console("start3(): doing three joins.\n");
//        join(&status);
//        join(&status);
//        join(&status);
    }
    if (debugFlag)
    		USLOSS_Console("start3(): zapping clock driver.\n");
    zap(clockPID);  // clock driver
    if (debugFlag)
        		USLOSS_Console("start3(): zapping disk drivers.\n");
    struct USLOSS_DeviceRequest * req;
    for(i = 1; i <= USLOSS_DISK_UNITS; i++){
    	if(i == 0)
    			req = &diskOneReq;
    		else
    			req = &diskTwoReq;
    	req->opr = USLOSS_DISK_SEEK;
		req->reg1 = 1;
		USLOSS_DeviceOutput(USLOSS_DISK_DEV, i, req);
    	//zap(diskPID[i-1]); // 1st disk driver
    }
    // eventually, at the end:
    quit(0);
}

/* ------------------------------------------------------------------------
   Name		-	ClockDriver
   Purpose	-	awakens sleeping processes once their time has come
   Params 	-	a pointer to an array of characters
   Returns	-	integer (not sure what this integer is)
   Side Effects	- none
   ----------------------------------------------------------------------- */
static int ClockDriver(char *arg)
{
	if (debugFlag)
		USLOSS_Console("ClockDriver(): started. \n");
    int result;
    int status;
    int i;

    // Let the parent know we are running and enable interrupts.
    semvReal(running);
    USLOSS_PsrSet(USLOSS_PsrGet() | USLOSS_PSR_CURRENT_INT);

    // Infinite loop until we are zap'd
    if (debugFlag)
    	USLOSS_Console("ClockDriver(): waiting on USLOSS_CLOCK_DEV\n");
    while(! isZapped()) {
        result = waitDevice(USLOSS_CLOCK_DEV, 0, &status);
//        if (debugFlag)
//        	USLOSS_Console("ClockDriver(): signal from clock dev received, starting loop\n");
        if (result != 0)
            return 0;
		/* Compute the current time and wake up any processes whose time has come. */
		int timeNow;
		gettimeofdayReal(&timeNow);
//		if (debugFlag)
//			USLOSS_Console("ClockDriver(): calculating current time: %d\n",timeNow);
		char msg[50];
		//int temp;
		for(; clockWaiterHead != NULL && clockWaiterHead->secsRemaining <= timeNow; clockWaiterHead = clockWaiterHead->next){
			if (debugFlag)
				USLOSS_Console("ClockDriver():\tclockWaiterHead time: %d\n\tsystem time: %d\n\ttotal waited time: %d\n", clockWaiterHead->secsRemaining, timeNow, timeNow - clockWaiterHead->secsRemaining);
			// if the clockWaiterHead wait time is
			MboxCondSend(clockWaiterHead->procMbox, msg, 0);
			if (debugFlag)
						USLOSS_Console("ClockDriver(): msg sent to process %d\n", clockWaiterHead->PID);
		}
//		if (debugFlag)
//			USLOSS_Console("ClockDriver(): returning to the top of the loop.\n");
	}
    // Once Zapped, call quit
    if (debugFlag)
        		USLOSS_Console("ClockDriver(): zapped; quitting.\n");
    quit(0);
    return 0;
}

/* ------------------------------------------------------------------------
   Name		-	snooze
   Purpose	-   puts a process to sleep for a number of seconds specified
                by the userint diskReadReal(int unit, int track, int first, int sectors, void *buffer){
   Params 	-	a struct of arguments; args[1] contains the number of
   	   	   	   	seconds the process will sleep
   Returns	-	placed into 4th position in argument struct; -1 if input
   Side Effects	-
   ----------------------------------------------------------------------- */
void snooze(systemArgs *args){
	if (debugFlag)
		USLOSS_Console("snooze(): started.\n");
	int reply = 0;
	/* verify that the specified interrupt number is correct */
	if(args->number != SYS_SLEEP){
		if (debugFlag)
			USLOSS_Console("snooze(): Attempted a \"sleep\" operation with wrong sys call number: %d.\n", args->number);
		return;
	}
	/* check to make sure that the specified number of seconds is >= 1 and is an integer */
	if (debugFlag){
		USLOSS_Console("snooze(): checking digit: %d\n",args->arg1);
	}
	if((int)args->arg1 < 1){
		if (debugFlag)
			USLOSS_Console("snooze(): Invalid number of seconds specified for sleep operation: %d. %d %d\n", args->arg1, !isdigit((int)args->arg1), (int)args->arg1 < 1);
		reply = -1;
	}
	/* call helper method and assign return value */
	else
		sleepHelper((int)args->arg1);
	if (debugFlag)
		USLOSS_Console("snooze(): returned from sleep with value %d\n", reply);
	args->arg4 = &reply;
	toUserMode();
	return;
}

void sleepHelper(int seconds){
	if (debugFlag)
			USLOSS_Console("sleepHelper(): started.\n");
	struct ProcStruct * target = &pFourProcTable[getpid() % MAXPROC];
	char msg[50];

	/* add a new entry to the clockWaiter table */
	if (debugFlag)
			USLOSS_Console("sleepHelper(): calling helper method.\n");
	clockWaiterAdd(getpid(), seconds);
	/* receive on the clockDriver mbox */
	if (debugFlag)
				USLOSS_Console("sleepHelper(): executing receive on process mbox %d (%d).\n", target->procMbox, getpid());
	MboxReceive(target->procMbox, msg, 0);
	if (debugFlag)
		USLOSS_Console("sleepHelper(): process %d returned from sleep; returning to parent.\n", getpid());
}

// a helper method which adds a clockWaiter object onto the queue
void clockWaiterAdd(int pid, int seconds){
	if (debugFlag)
		USLOSS_Console("clockWaiterAdd(): started.\n\tpid: %d\n\tseconds: %d\n",pid,seconds);
	/* compute the wake up time for the process */
	int wakeUpTime;
	gettimeofdayReal(&wakeUpTime);
	if (debugFlag)
				USLOSS_Console("clockWaiterAdd(): current time = %d seconds\n",wakeUpTime);
	wakeUpTime += (seconds*1000000);
	if (debugFlag)
			USLOSS_Console("clockWaiterAdd(): computed wait time = %d seconds\n",wakeUpTime);
	/* place the process in the wait line */
	clockWaitLine[getpid() % MAXPROC].PID = getpid();
	clockWaitLine[getpid() % MAXPROC].procMbox = pFourProcTable[getpid() % MAXPROC].procMbox;
	clockWaitLine[getpid() % MAXPROC].secsRemaining = wakeUpTime;
	struct clockWaiter * position = &clockWaitLine[getpid() % MAXPROC];
	/* if there are no current waiting processes place at head of queue */
	if(clockWaiterHead == NULL){
		if (debugFlag)
			USLOSS_Console("clockWaiterAdd(): placing at head of empty queue\n");
		clockWaiterHead = position;
	}
	/* if the current process's wait time is shorter than the head proc's
	 * waiting time, place the new proc at the head */
	else if(clockWaiterHead->secsRemaining >= position->secsRemaining){
		if (debugFlag)
			USLOSS_Console("clockWaiterAdd(): replacing head of queue\n");
		position->next = clockWaiterHead;
		clockWaiterHead = position;
	}
	/* else locate the proper position for the current process and insert */
	else{
		if (debugFlag)
			USLOSS_Console("clockWaiterAdd(): placing in queue\n");
		struct clockWaiter * counter = clockWaiterHead;
		for(; counter->next != NULL && counter->next->secsRemaining < position->secsRemaining; counter = counter->next);
		position->next = counter->next;
		counter->next = position;
	}
}

/* ------------------------------------------------------------------------
   Name		-	DiskDriver
   Purpose	-	coordinates read/write activities for a particular disk unit
   Params 	-	a pointer to an array of characters containing the unit
   Returns	-	integer (not sure what this integer is)
   Side Effects	- none
   ----------------------------------------------------------------------- */
void DiskDriver(char *arg)
{
	int result, status, unit, procPID;
	int armPointer;
	int diskMutex;
	int diskWaitSem;
	struct diskProc * q;
	struct USLOSS_DeviceRequest * req;

	// create a queue for waiting
    unit = atoi( (char *) arg); 	// Unit is passed as arg.
    // set pointers to the queue, the arm, and the mutex sem
    if(unit == 0){
    	q = diskOneHead;
    	diskMutex = diskOneMutex;
    	diskWaitSem = diskOneQueue;
    	req = &diskOneReq;
    }else{
    	q = diskTwoHead;
    	diskMutex = diskTwoMutex;
    	diskWaitSem = diskTwoQueue;
    	req = &diskTwoReq;
    }

    armPointer = USLOSS_DISK_TRACK_SIZE / 2;

    // v the semaphore so that the system knows that the diskdriver is running
    if (debugFlag)
		USLOSS_Console("DiskDriver(): diskDriver %d started\n", unit);
    semvReal(running);

    // infinite loop until the disk proc is zapped!
    while(!isZapped()){
    	if (debugFlag)
			USLOSS_Console("DiskDriver(): waiting on semaphore\n");
    	// perform p operation on semaphore to wait for uploads
    	sempReal(diskWaitSem);
    	if (debugFlag)
			USLOSS_Console("DiskDriver(): Checking for cleanup\n");
    	// check if process should quit
    	if(cleanUp)
    		quit(0);
    	if (debugFlag)
			USLOSS_Console("DiskDriver(): entering mutex\n");
    	// enter mutex
		sempReal(diskMutex);
		// if the current arm position is different from the operation track, seek to the requested track
		if(armPointer != q->track){
			req->opr = USLOSS_DISK_SEEK;
			req->reg1 = q->track;
			USLOSS_DeviceOutput(USLOSS_DISK_DEV, unit, req);
			result = waitDevice(USLOSS_DISK_DEV, unit, &status);
			if (result != 0)
				return;
			armPointer = q->track;
		}
		// execute the first process on the queue
		switch(q->type){
			case USLOSS_DISK_READ:
				result = diskReadReal(q->unit, q->track, q->first, q->sectors, q->buffer);
				break;
			case USLOSS_DISK_WRITE:
				result = diskWriteReal(q->unit, q->track, q->first, q->sectors, q->buffer);
				break;
			default:
				break;
		}
		// if something went wrong with the process

		//remove the first proc from the queue and exit mutex
		procPID = q->pid;
		q->pid = DISKPROCEMPTY;
		if(unit == 0)
			diskOneHead = diskOneHead->next;
		else
			diskTwoHead = diskTwoHead->next;
		// exit mutex
		semvReal(diskMutex);
		// awaken the process
		MboxCondSend(procPID, dummyMsg, NULL);
    }
    // once zapped, quit
    quit(0);
}

/* ------------------------------------------------------------------------
   Name		-   diskReadReal
   Purpose	-   Reads sectors sectors from the disk indicated by unit,
                starting at track track and sector first . The sectors are
                copied into buffer.
   Params 	-   int unit, int track, int first sector, int number of sectors,
                void * buffer to read into
   Returns	-   -1: invalid parameters; 0: sectors were read successfully;
                >0: disk’s status register
   Side Effects	- none
   ----------------------------------------------------------------------- */
int diskReadReal(int unit, int track, int first, int sectors, void *buffer){
	struct USLOSS_DeviceRequest * req;
	char *fetch[USLOSS_DISK_SECTOR_SIZE];
	int result, status;

	// if any of the arguments passed have illegal values, set arg4 to -1 and return
	if(first < 0 || first > USLOSS_DISK_TRACK_SIZE || sectors <= 0 || sectors > USLOSS_DISK_TRACKS)
		return(-1);

	// select the correct global request object
	if(unit == 0)
		req = &diskOneReq;
	else
		req = &diskTwoReq;
	/* if the current arm position is different from the operation track, seek to the requested track
	if(*armPointer != q->track){
		req->opr = USLOSS_DISK_SEEK;
		req->reg1 = q->track;
		USLOSS_DeviceOutput(USLOSS_DISK_DEV, unit, req);
		*armPointer = q->track;
	}*/
	req->opr = USLOSS_DISK_READ;
	int iter;
	for(iter = 0; iter < sectors; iter++){
		req->reg1 = first + iter;
		req->reg2 = fetch;
		USLOSS_DeviceOutput(USLOSS_DISK_DEV, unit, req);
		result = waitDevice(USLOSS_DISK_DEV, unit, &status);
		if (result != 0)
			return status;
		if(status & USLOSS_DEV_READY){
			memcpy(buffer + iter*512, fetch,512);
		}
	}
}

/* ------------------------------------------------------------------------
   Name		-   diskWriteReal
   Purpose	-   Writes sectors to the disk indicated by unit, starting at
                track track and sector first . The sectors are copied into
                buffer.
   Params 	-   int unit, int track, int first sector, int number of sectors,
                void * buffer to read into
   Returns	-   -1: invalid parameters; 0: sectors were written successfully;
                >0: disk’s status register
   Side Effects	- none
   ----------------------------------------------------------------------- */
int diskWriteReal(int unit, int track, int first, int sectors, void *buffer){
	struct USLOSS_DeviceRequest * req;
	char *fetch[USLOSS_DISK_SECTOR_SIZE];
	int result, status;

	// if any of the arguments passed have illegal values, set arg4 to -1 and return
	if(first < 0 || first > USLOSS_DISK_TRACK_SIZE || sectors <= 0 || sectors > USLOSS_DISK_TRACKS)
		return(-1);
	// select the correct global request object
	if(unit == 0)
			req = &diskOneReq;
		else
			req = &diskTwoReq;
	int iter;
	for(iter = 0; iter < sectors; iter++){
		req->reg1 = first + iter;
		memcpy(fetch, buffer + iter*512 ,512);
		req->reg2 = fetch;
		USLOSS_DeviceOutput(USLOSS_DISK_DEV, unit, req);
		result = waitDevice(USLOSS_DISK_DEV, unit, &status);
		if (result != 0){
			return(status);
		}

	}
}

void diskSizeReal(int unitNum, int * sectorSize, int * numSectors, int * numTracks){
    if (debugFlag){
        USLOSS_Console("diskSizeReal(): started.\n");
    }
    /* Getting numTracks */
    USLOSS_DeviceRequest * request;
    request->opr = USLOSS_DISK_TRACKS;
    request->reg1 = numTracks;
    USLOSS_DeviceOutput(USLOSS_DISK_DEV, unitNum, request);
//    USLOSS_DeviceRequest * request;
//    request = unitNum==0 ? &diskOneReq : &diskTwoReq;
//    request->opr = USLOSS_DISK_TRACKS;
//    request->reg1 = numTracks;

    /* Getting sectorSize */
    *sectorSize = 512;

    /* Getting numSectors */
    *numSectors = 16;

    if (debugFlag){
        USLOSS_Console("diskSizeReal(): ended.\n");
    }

}

/* ------------------------------------------------------------------------
   Name		-	diskRead
   Purpose	-	reads n sectors from specified disk
   Params 	-	a struct of arguments
   Returns	-	none
   Side Effects	- none
   ----------------------------------------------------------------------- */
void diskRead(systemArgs *args){
	if (debugFlag)
		USLOSS_Console("diskRead(): started\n");
	/* Contents of the argument object as follows:
	 * sysArg.arg1 = diskBuffer;
	 * sysArg.arg2 = sectors;
	 * sysArg.arg3 = track;
	 * sysArg.arg4 = first;
	 * sysArg.arg5 = unit;
	*/
	char *msg;
	// read the information from the unit
	if (debugFlag)
		USLOSS_Console("diskRead(): queueing the disk process object\n");
	diskQueue(USLOSS_DISK_READ, args->arg5, args, getpid());
	if (debugFlag)
			USLOSS_Console("diskRead():performing sem operation ");
	// perform a v operation on the appropriate semaphore
	if(args->arg5 == 0){
		if (debugFlag)
				USLOSS_Console("on sem One\n");
		semvReal(diskOneQueue);
	}else{
		if (debugFlag)
			USLOSS_Console("on sem Two\n");
		semvReal(diskTwoQueue);
	}
	if (debugFlag)
		USLOSS_Console("diskRead(): queueing finished, performing receive on proc mbox\n");
	// block on the process mbox until it returns
	MboxReceive(pFourProcTable[getpid() % MAXPROC].procMbox, msg, 0);
}

/* ------------------------------------------------------------------------
   Name		-   DiskWrite
   Purpose	-   Interrupt Handler for disk write signals
   Params 	-	a struct of arguments; contents in the method
   Returns	-   none
   Side Effects	- none
   ----------------------------------------------------------------------- */
void diskWrite(systemArgs *args){
	if (debugFlag)
		USLOSS_Console("diskWrite(): started\n");
	/* Contents of the argument object as follows:
	 * sysArg.arg1 = diskBuffer;
	 * sysArg.arg2 = sectors;
	 * sysArg.arg3 = track;
	 * sysArg.arg4 = first;
	 * sysArg.arg5 = unit;
	*/
	char *msg;
	// read the information from the unit
	diskQueue(USLOSS_DISK_WRITE, args->arg5, args, getpid());
	if (debugFlag)
			USLOSS_Console("diskWrite(): queueing finished, performing receive on proc mbox\n");
	// perform a v operation on the appropriate semaphore
	if(args->arg5 == 0)
		semvReal(diskOneQueue);
	else
		semvReal(diskTwoQueue);
	// wait on personal mbox until done
	MboxReceive(pFourProcTable[getpid() % MAXPROC].procMbox, msg, 0);
	// place important values in the respective registers
}

// sorts a process onto the appropriate disk queue in circular scan ordering
void diskQueue(int opr, int unit, systemArgs *args, int pid){
	struct diskProc * target;
	int * armPos;
	// select the target queue
	if(unit == 1){
		target = diskOneHead;
		armPos = &diskOneArmPos;
	}else{
		target = diskTwoHead;
		armPos = &diskTwoArmPos;
	}
	/* insert disk opr request based on position of arm and other process requests */
	// if there are no items currently on the head, insert
	if(target == NULL)
		diskOneHead = diskQueueHelper(unit,opr,args,pid,NULL);
	// otherwise, find the position where the new node will fit
	else{
		// if the requested disk proc is less than the current arm position, place the new proc after all those that are after it
		if(args->arg3 < *armPos)
			// first track past the processes that are located at or after the arm position
			for(;target->next != NULL && target->next->track <= USLOSS_DISK_TRACK_SIZE; target = target->next);
		// then track to the correct position amongst the remaining processes
		for(;target->next != NULL && target->next->track <= args->arg3 && target->next->first <= args->arg4; target = target->next);
		// place the new disk request in the appropriate position in line
		if(target->next == NULL)
			target->next = diskQueueHelper(unit,opr,args,pid,NULL);
		else
			target->next = diskQueueHelper(unit,opr,args,pid,target->next);
	}
}

// helper method places information in appropriate object in disk queue
struct diskProc *  diskQueueHelper(int unit, int opr, systemArgs *args, int pid, struct diskProc * next){
	/* Contents of the argument object as follows:
	 * sysArg.arg1 = diskBuffer;
	 * sysArg.arg2 = sectors;
	 * sysArg.arg3 = track;
	 * sysArg.arg4 = first;
	 * sysArg.arg5 = unit;
	*/
	// select available position in the queue
	struct diskProc * target = &diskQ[pid%MAXPROC];
	target->pid = pid;
	target->unit = unit;
	target->type = opr;
	target->track = args->arg3;
	target->sectors = args->arg2;
	target->first = args->arg4;
	target->next = next;
	return target;
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
    int unitNum = (int)args->arg1;
    int sectorSize;
    int numTracks;
    int numSectors;
    
    diskSizeReal(unitNum, &sectorSize, &numSectors, &numTracks);
    
    args->arg1 = (void *)(long)sectorSize;
    args->arg2 = (void *)(long)numSectors;
    args->arg3 = (void *)(long)numTracks;
}

static int TermDriver(char * arg){
    if (debugFlag){
        USLOSS_Console("TermDriver(): process %d starting up!\n", getpid());
    }
    int status;
    int unit = atoi( (char *) arg);
    int inBox;
    int outBox;
    int writeBox;
    int mutexBox;
    int result;
    int recv;
    int xmit;
    int writeSem;
//    int control;
//    control = 0;
//    control = USLOSS_TERM_CTRL_RECV_INT(control);
//    if (debugFlag){
//        USLOSS_Console("TermDriver(): control %d!\n", control);
//    }
//    result = USLOSS_DeviceOutput(USLOSS_TERM_DEV, unit, control);
//    if (debugFlag){
//        USLOSS_Console("TermDriver(): result %d!\n", result);
//    }
//    terminals[unit].readEnabled = 1;
//    if (debugFlag){
//        USLOSS_Console("TermDriver(): read enabled for unit %d is %d!\n", unit, terminals[unit].readEnabled);
//    }
    writeSem = semcreateReal(1);
    mutexBox = MboxCreate(1, MAXLINE);
    inBox = MboxCreate(1, MAXLINE);
    outBox = MboxCreate(1, MAXLINE);
    writeBox = MboxCreate(1, MAXLINE);
    terminals[unit].inBox = inBox;
    terminals[unit].outBox = outBox;
    terminals[unit].mutexBox = mutexBox;
    terminals[unit].writeBox = writeBox;
    terminals[unit].readEnabled = 0;
    terminals[unit].writeSem = writeSem;
    
    
    if (debugFlag){
        USLOSS_Console("TermDriver(): finished set up for unit %d!\n", unit);
    }
    // Finished initialization
    semvReal(running);
    
    while(!isZapped()){
        if(debugFlag){
            USLOSS_Console("TermDriver(): Before waitDevice\n");
        }
        result = waitDevice(USLOSS_TERM_DEV, unit ,&status);
        if(debugFlag){
            USLOSS_Console("TermDriver(): After waitDevice\n");
        }
        if(result!=0){
            if(debugFlag){
                USLOSS_Console("TermDriver(): The result was not equal to zero, quitting..\n");
            }
            quit(0);
        }
        if(cleanUp){
            if(debugFlag){
                USLOSS_Console("TermDriver(): cleaning up unit %d.\n", unit);
            }
            quit(0);
        }

        // If received char, send to char in Box
        recv = USLOSS_TERM_STAT_RECV(status);

        if(recv == USLOSS_DEV_BUSY){
            if (debugFlag){
                USLOSS_Console("TermDriver(): recv == USLOSS_DEV_BUSY\n");
            }
            char x;
            x = USLOSS_TERM_STAT_CHAR(status);
            if (debugFlag){
                USLOSS_Console("TermDriver(): char read is \'%c\'\n", x);
            }
    
            MboxSend(terminals[unit].inBox, &status, sizeof(int));
        }
        xmit = USLOSS_TERM_STAT_XMIT(status);
        // If sent char, send result to char out Box
        if(xmit == USLOSS_DEV_READY){
            if (debugFlag){
                USLOSS_Console("TermDriver(): xmit == USLOSS_DEV_READY\n");
            }
            MboxCondSend(terminals[unit].outBox, &status, sizeof(int));
        }
        if(debugFlag){
            USLOSS_Console("TermDriver(): After sends\n");
        }
    }
    
    return 0;
    
}

static int TermReader(char * arg){
    if (debugFlag){
        USLOSS_Console("TermReader(): process %d starting up!\n", getpid());
    }
    int unit = atoi( (char *) arg);
    char msg[MAXLINE];
    int i=0;
    int charsRead = 0;
    char temp;
    void * status;
    int bufferBox;
    bufferBox = MboxCreate(10, MAXLINE);
    terminals[unit].bufferBox = bufferBox;
    
    if (debugFlag){
        USLOSS_Console("TermReader(): finished setup!\n");
    }
    
    // Finished initialization
    semvReal(running);
    
    // Get the full line from mailBox
    while(!isZapped()){
        if (debugFlag){
            USLOSS_Console("TermReader(): pid: %d blocking on inBox\n", getpid());
        }
        //dumpProcesses();
        MboxReceive(terminals[unit].inBox, &status, sizeof(int));
        if(cleanUp){
            if(debugFlag){
                USLOSS_Console("TermReader(): cleaning up\n");
            }
            //dumpProcesses();
            quit(0);
        }
        temp = USLOSS_TERM_STAT_CHAR((int)status);
        if (debugFlag){
            USLOSS_Console("TermReader(): after receive on inBox, got \'%c\'\n", temp);
        }
        if(charsRead >= MAXLINE || temp == '\n'){
            if(debugFlag){
                USLOSS_Console("TermReader(): finished reading line: ");
                int j = 0;
                for(j=0;j<strlen(msg);j++){
                    USLOSS_Console("%c", msg[j]);
                }
                USLOSS_Console("\n");
            }
            if(debugFlag){
                USLOSS_Console("TermReader(): Sending to bufferBox %d, charsRead: %d\n", terminals[unit].bufferBox,charsRead);
            }
            msg[i]=temp;
            i++;
            charsRead++;
            MboxCondSend(terminals[unit].bufferBox, &msg, charsRead);
            // Clearing out the msg buffer after send
            memset(msg, '\0', MAXLINE);
            charsRead = 0;
            i=0;
        }
        else{
            if(debugFlag){
                USLOSS_Console("TermReader(): adding next char\n");
            }
            msg[i] = temp;
            i++;
            charsRead++;
            if (debugFlag){
                int j = 0;
                for(j=0;j<strlen(msg);j++){
                    USLOSS_Console("%c", msg[j]);
                }
                USLOSS_Console("\n");
            }
        }
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
    
    // Check unit number validity and line length validity
    if(unitNum > USLOSS_MAX_UNITS-1 || unitNum<0 ||maxSize > MAXLINE || maxSize<0){
        args->arg4 = (void *)(long)-1;
        return;
    }
    
    int charsRead;
    charsRead = termReadReal(address, maxSize, unitNum);
    args -> arg2 = (void *)(long)charsRead;
    args->arg4 = (void *)(long)0;
    
}

int termReadReal(char * address, int maxSize, int unitNum){
    
    if (debugFlag){
        USLOSS_Console("TermReadReal(): started\n");
    }
    
    if(unitNum>USLOSS_MAX_UNITS ){
        if (debugFlag){
            USLOSS_Console("termReadReal(): Tried to use terminal outside of max units. # %d\n", unitNum);
        }
        return -1;
    }
    
    int i=0;
    int charsRead = 0;
    
    char msg[MAXLINE];
    char * temp;
    int control;
    int result;
    
    if(debugFlag){
        USLOSS_Console("termReadReal(): for unit %d readEnabled = %d\n", unitNum, terminals[unitNum].readEnabled);
    }
    if(!terminals[unitNum].readEnabled){
        // Turning read interrupts on if it is not already.
        control = 0;
        control = USLOSS_TERM_CTRL_RECV_INT(control);
        result = USLOSS_DeviceOutput(USLOSS_TERM_DEV, unitNum, control);
        terminals[unitNum].readEnabled = 1;
        if (debugFlag){
            USLOSS_Console("termReadReal(): Terminal %d enabled read\n", unitNum);
        }
    }
    
    if (debugFlag){
        USLOSS_Console("TermReadReal(): Blocking on bufferBox Receive.\n");
    }
    MboxReceive(terminals[unitNum].bufferBox, msg, MAXLINE);
    if (debugFlag){
        USLOSS_Console("TermReadReal(): After bufferBox receive. Msg: ");
        for(i=0;i<maxSize && msg[i]!='\n';i++){
            USLOSS_Console("%c",msg[i]);
        }
        

        USLOSS_Console("\n");
    }
    
    
    
    temp = msg;
    
    for(i=0;i<maxSize; i++){
        if(*temp!='\n'){
            charsRead++;
        }
        else{
            charsRead++;
            break;
        }
        temp++;
    }
    
    //strcat("\n", msg);
    strncpy(address, msg, charsRead);
    
    return charsRead;
    
    
//    // Get the full line from terminal
//    while(1){
//        if (charsRead >= maxSize){
//            if(debugFlag){
//                USLOSS_Console("termReadReal(): The line is longer than the maximum allowed characters.\n");
//            }
//            break;
//        }
//        USLOSS_DeviceInput(USLOSS_TERM_DEV, unitNum, &status);
//        // If there is a character waiting
//        if(USLOSS_TERM_STAT_RECV(status) == USLOSS_DEV_BUSY){
//            msg[i] = USLOSS_TERM_STAT_CHAR(status);
//            i++;
//            if (USLOSS_TERM_STAT_CHAR(status) == '\n' || USLOSS_TERM_STAT_CHAR(status) == '\0'){
//                break;
//            }
//            else{
//                charsRead++;
//            }
//        }
//        else if (USLOSS_TERM_STAT_RECV(status) == USLOSS_DEV_READY){
//            continue;
//        }
//        else{
//            if(debugFlag){
//                USLOSS_Console("termReadReal(): Error in USLOSS_DeviceInput getting character.\n");
//            }
//        }
//    }
//    if(debugFlag){
//        USLOSS_Console("termReadReal(): finished reading line: ");
//        int j = 0;
//        for(j=0;j<strlen(msg);j++){
//            USLOSS_Console("%c", msg[j]);
//        }
//        USLOSS_Console("\n");
//        
//    }
//    msg[i] = '\0';
//    strcpy(address, msg);
    
    
}

static int TermWriter(char * arg){
    
    if (debugFlag){
        USLOSS_Console("TermWriter(): process %d starting up!\n",getpid());
    }
    
    int unit = atoi( (char *) arg);
    char line[MAXLINE];
    int charsWritten = 0;
    int i=0;
    int result = 0;
    int control = 0;
    
    if (debugFlag){
        USLOSS_Console("TermWriter(): finished set up!\n");
    }
    semvReal(running);
    
    while(!isZapped()){
        MboxReceive(terminals[unit].writeBox, line, MAXLINE);
        if (debugFlag){
            USLOSS_Console("TermWriter(): after receive!\n");
        }
        if(cleanUp){
            if(debugFlag){
                USLOSS_Console("TermWriter(): cleaning up.\n");
            }
            quit(0);
        }
        charsWritten = 0;
        i=0;
        // Setting the xmit int enable bit
        control=0;
        control = USLOSS_TERM_CTRL_XMIT_INT(control);
        control = USLOSS_TERM_CTRL_RECV_INT(control);
        USLOSS_DeviceOutput(USLOSS_TERM_DEV, unit, control);
        char temp;
        void * status;
        while(!isZapped()){
            MboxReceive(terminals[unit].outBox, &status, sizeof(int));
            if(debugFlag){
                USLOSS_Console("TermWriter(): char being written : %c\n\n\n", line[i]);
            }
//            if(df){
//                USLOSS_Console("TermWriter(%d): char being written: %c\n\n\n", unit,line[i]);
//            }
            control=0;
            control = USLOSS_TERM_CTRL_RECV_INT(control);
            control = USLOSS_TERM_CTRL_CHAR(control, line[i]);
            control = USLOSS_TERM_CTRL_XMIT_CHAR(control);
            control = USLOSS_TERM_CTRL_XMIT_INT(control);
            //USLOSS_Console("CONTROL VALUE: %d & UNIT: %d\n\n", control, unit);
            result = USLOSS_DeviceOutput(USLOSS_TERM_DEV, unit, (void *)(long)control);
            if(debugFlag){
                USLOSS_Console("TermWriter(): Control: %d Result : %d, GOOD value: %d\n\n\n", control, result, USLOSS_DEV_OK);
            }
            if( line[i]=='\n' || line[i] == '\0'){
                charsWritten++;
                if(debugFlag){
                    USLOSS_Console("TermWriter(): Reached end of string for write.\n");
                }
                break;
            }
            i++;
            charsWritten++;
        }
        if(debugFlag){
            USLOSS_Console("TermWriter(): After writing full string.");
        }
        
        // Disabling CTRL XMIT
        control = 0;
        control = USLOSS_TERM_CTRL_RECV_INT(control);
        result = USLOSS_DeviceOutput(USLOSS_TERM_DEV, unit, (void *) (long)control);
        terminals[unit].readEnabled = 1;
        //control = USLOSS_TERM_CTRL_XMIT_INT_DISABLE(control);
        if(debugFlag){
            USLOSS_Console("TermWriter(): Before MboxSend.\n");
        }
        semvReal(terminals[unit].writeSem);
        MboxSend(terminals[unit].mutexBox, &charsWritten, sizeof(int));
    }
//    // Get the full line from mailBox
//    while(1){
//        while(1){
//            MboxReceive(terminals[unit].writeBox, temp, 1);
//            if (charsRead >= MAXLINE){
//                break;
//            }
//            else if(temp == '\n'){
//                msg[i] = temp;
//                i++;
//                break;
//            }
//            msg[i] = temp;
//            i++;
//            charsRead++;
//        }
//        msg[i]='\0';
//        if(debugFlag){
//            USLOSS_Console("termReadReal(): finished reading line: ");
//            int j = 0;
//            for(j=0;j<strlen(msg);j++){
//                USLOSS_Console("%c", msg[j]);
//            }
//            USLOSS_Console("\n");
//        }
    
    
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
    char * temp;
    char * address;
    int numChars;
    int unitNum;
    address = (char *)args->arg1;
    numChars = (int)args->arg2;
    unitNum = (int)args->arg3;
    long numWritten;
    temp = address;
    
    
    
    // Check unit number validity and line length validity
    if(unitNum > USLOSS_TERM_UNITS-1 || unitNum<0 ||numChars > MAXLINE || numChars<0){
        args->arg4 = (void *)(long)-1;
        return;
    }
    
//    if (debugFlag){
//        USLOSS_Console("termWrite(): before termWriteReal. The message is: ");
//        for(;*address!='\n';address++){
//            USLOSS_Console("%c",*address);
//        }
//        USLOSS_Console("\n");
//    }
    
    numWritten = termWriteReal(address, numChars, unitNum);
    
//    if(debugFlag){
//        int i;
//        int num=0;
//        USLOSS_Console("termWrite(): Num chars given: %d, Num chars written: %d\n", numChars, numWritten);
//        for(;*temp!='\n';temp++){
//            USLOSS_Console("%c", *temp);
//            num++;
//        }
//        USLOSS_Console("\nNum I Counted: %d\n",num);
//    }
    
    args->arg2 = (void *)numWritten;
    args->arg4 = (void *)(long)0;
}

long termWriteReal(char * address, int numChars, int unitNum){
    
    char * temp;
    temp = address;
    if(unitNum>USLOSS_MAX_UNITS ){
        if (debugFlag){
            USLOSS_Console("termWriteReal(): Tried to use terminal outside of max units. # %d\n", unitNum);
        }
        return -1;
    }
//    if (debugFlag){
//        USLOSS_Console("termWriteReal(): The message is: ");
//        for(;*address!='\n';address++){
//            USLOSS_Console("%c",*address);
//        }
//        USLOSS_Console("\n");
//    }
    
    if (debugFlag){
        USLOSS_Console("termWriteReal(): writebox %d numChars %d unitNum %d\n", terminals[unitNum].writeBox, numChars, unitNum);
        USLOSS_Console("termWriteReal(): before mBoxSend to writebox.\n");
    }

    int i;
    for(i=0;*temp!='\n' && i<numChars ;temp++, i++){
        //USLOSS_Console("%c", *temp);
    }
    //USLOSS_Console("\nNum I Counted: %d\n",i);
    sempReal(terminals[unitNum].writeSem);
    MboxSend(terminals[unitNum].writeBox, address, numChars);
    int charsWritten;
    MboxReceive(terminals[unitNum].mutexBox, &charsWritten, sizeof(int));
    return i+1;
    
}

void toUserMode(){
    
    if(debugFlag){
        USLOSS_Console("toUserMode(): switching to User Mode.\n");
    }
    USLOSS_PsrSet(USLOSS_PsrGet() & ~USLOSS_PSR_CURRENT_MODE);
    //    unsigned int psr = USLOSS_PsrGet();
    //    USLOSS_Console("toUserMode(): PSR before: %d\n", psr & USLOSS_PSR_CURRENT_MODE);
    //    psr = (psr & ~1);
    //    //psr &= ~(0 << USLOSS_PSR_CURRENT_MODE);
    //    USLOSS_PsrSet(psr);
    //    USLOSS_Console("toUserMode(): PSR after: %d\n", psr & USLOSS_PSR_CURRENT_MODE);
}
