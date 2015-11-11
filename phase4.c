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
static int TermReader(char *);
static int TermWriter(char *);
void diskSize(systemArgs *args);
void termRead(systemArgs *args);
int termReadReal(char * address, int maxSize, int unitNum);
void termWrite(systemArgs *args);
long termWriteReal(char * address, int numChars, int unitNum);
void diskSizeReal(int unitNum, int * sectorSize, int * numSectors, int * numTracks);
void snooze(systemArgs *args);
void diskRead(systemArgs *args);
void diskWrite(systemArgs *args);
void toUserMode();
void sleepHelper(int seconds);
void clockWaiterAdd(int pid, int seconds);

/* -------------------------- Globals ------------------------------------- */
struct Terminal terminals[USLOSS_MAX_UNITS];
struct ProcStruct pFourProcTable[MAXPROC];
struct clockWaiter clockWaitLine[MAXPROC];
struct diskProc diskQ[MAXPROC];
struct diskProc * diskOneHead;
struct diskProc * diskTwoHead;
struct USLOSS_DeviceRequest diskOneReq;
struct USLOSS_DeviceRequest diskTwoReq;
int diskOneArmPos;
int diskTwoArmPos;
int diskOneMutex;
int diskTwoMutex;
struct clockWaiter * clockWaiterHead;
struct clockWaiter * clockWaiterTail;
struct USLOSS_DeviceRequest diskRW;
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
		pFourProcTable[i].procMbox = MboxCreate(0,50);
    }
    /* initialize clockWaitLine */
    for(i = 0; i < MAXPROC; i++){
    	clockWaitLine[i].PID = -1;
    }
    /* set the head and the tail */
    clockWaiterHead = NULL;
    clockWaiterTail = NULL;

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
    diskOneMutex = semcreateReal(0);
    diskTwoMutex = semcreateReal(0);

    for (i = 0; i < DISK_UNITS; i++) {
        sprintf(name, "Disk Driver #%d", i);
        pid = fork1(name, DiskDriver, NULL, USLOSS_MIN_STACK, 2);
        if (pid < 0) {
            USLOSS_Console("start3(): Can't create disk driver %d\n", i+1);
            USLOSS_Halt(1);
        }
    }
    sempReal(running);
    sempReal(running);

    //char unitNum[50];
    /* Create terminal device drivers. */
    for(i=0; i < USLOSS_TERM_UNITS; i++){
        sprintf(name, "Terminal Driver #%d", i+1);
        sprintf(termbuf, "%d", i+1);
        
    	pid = fork1(name, TermDriver, termbuf, USLOSS_MIN_STACK, 2);
    	if (pid < 0) {
			USLOSS_Console("start3(): Can't create terminal driver %d\n", i+1);
			USLOSS_Halt(1);
		}
        sprintf(name, "Terminal Reader #%d", i);
        pid = fork1(name, TermReader, termbuf, USLOSS_MIN_STACK, 2);
        if (pid < 0) {
            USLOSS_Console("start3(): Can't create terminal reader %d\n", i+1);
            USLOSS_Halt(1);
        }
        sprintf(name, "Terminal Writer #%d", i);
        pid = fork1(name, TermWriter, termbuf, USLOSS_MIN_STACK, 2);
        if (pid < 0) {
            USLOSS_Console("start3(): Can't create terminal writer %d\n", i+1);
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
    // zap(clockPID);  // clock driver
    // zap(); // 1st disk driver
    // zap(); //

    // eventually, at the end:
    quit(0);
}

static int ClockDriver(char *arg)
{
    int result;
    int status;
    int i;

    // Let the parent know we are running and enable interrupts.
    semvReal(running);
    USLOSS_PsrSet(USLOSS_PsrGet() | USLOSS_PSR_CURRENT_INT);

    // Infinite loop until we are zap'd
    while(! isZapped()) {
        result = waitDevice(USLOSS_CLOCK_DEV, 0, &status);
        if (result != 0)
            return 0;
        for(i = 0; i < MAXPROC; i++){
        	clockWaitLine[i].PID = -1;
        }
		/* Compute the current time and wake up any processes whose time has come. */
		int timeNow;
		gettimeofdayReal(&timeNow);
		char msg[50];
		//int temp;
		for(; clockWaiterHead != NULL && clockWaiterHead->secsRemaining <= timeNow; clockWaiterHead = clockWaiterHead->next){
			MboxCondSend(clockWaiterHead->PID, msg, 0);
			clockWaiterHead->PID = -1;
		}
	}
    // Once Zapped, call quit
    quit(0);
    return 0;
}

/* ------------------------------------------------------------------------
   Name		-	sleep
   Purpose	-   puts a process to sleep for a number of seconds specified
                by the user
   Params 	-	a struct of arguments; args[1] contains the number of
   	   	   	   	seconds the process will sleep
   Returns	-	placed into 4th position in argument struct; -1 if input
   Side Effects	-
   ----------------------------------------------------------------------- */
void snooze(systemArgs *args){
	if (debugFlag)
		USLOSS_Console("sleep(): started.\n");
	int reply = 0;
	/* verify that the specified interrupt number is correct */
	if(args->number != SYS_SLEEP){
		if (debugFlag)
			USLOSS_Console("sleep(): Attempted a \"sleep\" operation with wrong sys call number: %d.\n", args->number);
		return;
	}
	/* check to make sure that the specified number of seconds is >= 1 and is an integer */
	if(!isdigit((int)args->arg1) || (int)args->arg1 < 1){
		if (debugFlag)
			USLOSS_Console("sleep(): Invalid number of seconds specified for sleep operation: %d.\n", args->arg1);
		reply = -1;
	}
	/* call helper method and assign return value */
	else
		sleepHelper((int)args->arg1);
	args->arg4 = &reply;
	toUserMode();
	return;
}

void sleepHelper(int seconds){
	struct ProcStruct * target = &pFourProcTable[getpid() % MAXPROC];
	char msg[50];

	/* add a new entry to the clockWaiter table */
	clockWaiterAdd(getpid(), seconds);
	/* receive on the clockDriver mbox */
	MboxReceive(target->procMbox, msg, 0);
}

// a helper method which adds a clockWaiter object onto the queue
void clockWaiterAdd(int pid, int seconds){
	/* compute the wake up time for the process */
	int wakeUpTime = getTimeOfDayReal() + seconds;
	/* place the process in the wait line */
	clockWaitLine[getpid() % MAXPROC].PID = getpid();
	struct clockWaiter * position = &clockWaitLine[getpid() % MAXPROC];
	/* if there are no current waiting processes place at head of queue */
	if(clockWaiterHead == NULL){
		clockWaiterHead = position;
	}
	/* if the current process's wait time is shorter than the head proc's
	 * waiting time, place the new proc at the head */
	else if(clockWaiterHead->secsRemaining >= position->secsRemaining){
		position->next = clockWaiterHead;
		clockWaiterHead = position;
	}
	/* if there is only one current process in the queue, place the new proc
	 * at the end of the queue */
	else if(clockWaiterTail == NULL){
		clockWaiterHead->next = position;
		clockWaiterTail = position;
	}
	/* if the current proc's wait time is greater than the tail proc's wait
	 * time, place the current proc at the tail */
	else if(clockWaiterTail->secsRemaining <= position->secsRemaining){
		position->next = clockWaiterTail;
		clockWaiterTail = position;
	}
	/* base case: locate the proper position for the current process and insert */
	else{
		struct clockWaiter * counter = clockWaiterHead;
		for(; counter->next->secsRemaining < position->secsRemaining; counter = counter->next);
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
static int DiskDriver(char *arg)
{
	int result, status, unit;
	int * armPointer;
	int * diskMutex;
	struct diskProc * q;
	struct USLOSS_DeviceRequest * req;

	// create a queue for waiting
    unit = atoi( (char *) arg); 	// Unit is passed as arg.
    // set pointers to the queue, the arm, and the mutex sem
    if(unit == 1){
    	q = diskOneHead;
    	armPointer = &diskOneArmPos;
    	diskMutex = &diskOneMutex;
    	req = &diskOneReq;
    }else{
    	q = diskTwoHead;
    	armPointer = &diskTwoArmPos;
    	diskMutex = &diskTwoMutex;
    	req = &diskTwoReq;
    }

    *armPointer = USLOSS_DISK_TRACK_SIZE / 2;

    // v the semaphore so that the system knows that the diskdriver is running
    semvReal(running);

    // infinite loop until the disk proc is zapped!
    while(!isZapped()){
    	result = waitDevice(USLOSS_DISK_DEV, unit, &status);
    	if (result != 0)
    		return 0;
    	if(status & USLOSS_DEV_READY){
    		// if there are no elements on the q waitAgain on the device
    		if(q == NULL)
    			continue;
    		// enter mutex
    		sempReal(*diskMutex);
    		// if the current arm position is different from the operation track, seek to the requested track
    		if(*armPointer != q->track){
    			req->opr = USLOSS_DISK_SEEK;
    			req->reg1 = q->track;
    			USLOSS_DeviceOutput(USLOSS_DISK_DEV, unit, req);
    			*armPointer = q->track;
    		}
    		// execute the first process on the queue
    		req->opr = q->type;
    		int iter;
    		for(iter = 0; iter < q->sectors; iter++){
    			req->reg1 = q->first + iter;
    			req->reg2 = q->buffer;
    			USLOSS_DeviceOutput(USLOSS_DISK_DEV, unit, req);
    		}
    		//remove the first proc from the queue and exit mutex
    		q->pid = DISKPROCEMPTY;
    		if(unit == 1)
    			diskOneHead = diskOneHead->next;
    		else
    			diskTwoHead = diskTwoHead->next;
    		// exit mutex
    		semvReal(*diskMutex);
    	}
    }
    // once zapped, quit
    quit(0);
}

/* ------------------------------------------------------------------------
   Name		-	diskRead
   Purpose	-	reads n sectors from specified disk
   Params 	-	a struct of arguments
   Returns	-	none
   Side Effects	- none
   ----------------------------------------------------------------------- */
void diskRead(systemArgs *args){
	/* Contents of the argument object as follows:
	 * sysArg.arg1 = diskBuffer;
	 * sysArg.arg2 = sectors;
	 * sysArg.arg3 = track;
	 * sysArg.arg4 = first;
	 * sysArg.arg5 = unit;
	*/
	char *msg;
	// if any of the arguments passed have illegal values, set arg4 to -1 and return
	if(args->arg1 <= 0 || (int)args->arg2 < 1 || args->arg3 < 0 || args->arg4 < 0 || args->arg5 < 0){
		args->arg4 = (void *)-1;
		return;
	}
	// read the information from the unit
	diskQueue(USLOSS_DISK_READ, args->arg5, args, getpid());
	// wait on personal mbox until done
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
	/* Contents of the argument object as follows:
	 * sysArg.arg1 = diskBuffer;
	 * sysArg.arg2 = sectors;
	 * sysArg.arg3 = track;
	 * sysArg.arg4 = first;
	 * sysArg.arg5 = unit;
	*/
	char *msg;
	// if any of the arguments passed have illegal values, set arg4 to -1 and return
	if(args->arg1 <= 0 || args->arg2 < 1 || args->arg3 < 0 || args->arg4 < 0 || args->arg5 < 0){
		args->arg4 = -1;
		return;
	}
	// read the information from the unit
	diskQueue(USLOSS_DISK_WRITE, args->arg5, args, getpid());
	// wait on personal mbox until done
	MboxReceive(pFourProcTable[getpid() % MAXPROC].procMbox, msg, 0);
}

// sorts a process onto the appropriate disk queue in circular scan ordering
void diskQueue(int opr, int unit, systemArgs *args, int pid){
	struct diskProc * target;

	// select the target queue
	if(unit == 1)
		target = diskOneHead;
	else
		target = diskTwoHead;
	/* traverse the queue; if requests are found for similar track numbers, insert request */
	// if there are no items currently on the head, insert
	if(target == NULL)
		diskOneHead = diskQueueInsert(unit,opr,args,pid,NULL);
	// otherwise, find the position where the new node will fit
	else{
		for(;target->next != NULL && target->next->track <= args.arg3 && target->next->first <= args.arg4; target = target->next);
		if(target->next == NULL)
			target->next = diskQueueInsert(unit,opr,args,pid,NULL);
		else
			target->next = diskQueueInsert(unit,opr,args,pid,target->next);
	}
}

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

void diskSizeReal(int unitNum, int * sectorSize, int * numSectors, int * numTracks){
    if (debugFlag){
        USLOSS_Console("diskSizeReal(): started.\n");
    }
    /* Getting numTracks */
    USLOSS_DeviceRequest request;
    request.opr = USLOSS_DISK_TRACKS;
    request.reg1 = numTracks;
    USLOSS_DeviceOutput(USLOSS_DISK_DEV, unitNum, &request);
    
    /* Getting sectorSize */
    *sectorSize = 512;
    
    /* Getting numSectors */
    *numSectors = 16;
    
    if (debugFlag){
        USLOSS_Console("diskSizeReal(): ended.\n");
    }
    
}

static int TermDriver(char * arg){
    int status;
    int unit = atoi( (char *) arg);
    int inBox;
    int outBox;
    int writeBox;
    int mutexBox;
    int result;
    int recv;
    int xmit;
    mutexBox = MboxCreate(1, MAXLINE);
    inBox = MboxCreate(1, MAX_MESSAGE);
    outBox = MboxCreate(1, MAX_MESSAGE);
    writeBox = MboxCreate(1, MAXLINE);
    terminals[unit].pid = getpid();
    terminals[unit].inBox = inBox;
    terminals[unit].outBox = outBox;
    terminals[unit].mutexBox = mutexBox;
    terminals[unit].readEnabled = 0;
    
    // Finished initialization
    semvReal(running);
    
    while(!isZapped()){
        result = waitDevice(USLOSS_TERM_DEV, unit ,&status);
        if(result!=0){
            if(debugFlag){
                USLOSS_Console("TermDriver(): The result was not equal to zero, quitting..\n");
            }
            quit(0);
        }
        // If received char, send to char in Box
        recv = USLOSS_TERM_STAT_RECV(status);
        if(recv == USLOSS_DEV_BUSY){
            MboxSend(terminals[unit].inBox, (void *)(long)USLOSS_TERM_STAT_CHAR(status), 1);
        }
        xmit = USLOSS_TERM_STAT_XMIT(status);
        // If sent char, send result to char out Box
        if(xmit == USLOSS_DEV_READY){
            MboxSend(terminals[unit].outBox, (void *)(long)USLOSS_TERM_STAT_CHAR(status), 1);
        }
    }
    
    return 0;
    
}

static int TermReader(char * arg){
    int unit = atoi( (char *) arg);
    char msg[MAXLINE];
    int i=0;
    int charsRead = 0;
    char * temp;
    int bufferBox;
    bufferBox = MboxCreate(10, MAX_MESSAGE);
    terminals[unit].bufferBox = bufferBox;
    
    // Finished initialization
    semvReal(running);
    
    // Get the full line from mailBox
    while(!isZapped()){
        MboxReceive(terminals[unit].inBox, temp, 1);
        if(charsRead >= MAXLINE || *temp == '\n'){
            if(debugFlag){
                USLOSS_Console("termReadReal(): finished reading line: ");
                int j = 0;
                for(j=0;j<strlen(msg);j++){
                    USLOSS_Console("%c", msg[j]);
                }
                USLOSS_Console("\n");
            }
            MboxCondSend(terminals[unit].bufferBox, msg, charsRead);
            // Clearing out the msg buffer after send
            memset(msg, '\0', MAXLINE);
            charsRead = 0;
            i=0;
        }
        else{
            msg[i] = *temp;
            i++;
            charsRead++;
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
    if(unitNum > USLOSS_MAX_UNITS-1 || maxSize > MAXLINE){
        args->arg4 = (void *)(long)-1;
    }
    
    int charsRead;
    charsRead = termReadReal(address, maxSize, unitNum);
    args -> arg2 = (void *)(long)charsRead;
    
}

int termReadReal(char * address, int maxSize, int unitNum){
    
    if(unitNum>USLOSS_MAX_UNITS ){
        if (debugFlag){
            USLOSS_Console("termReadReal(): Tried to use terminal outside of max units. # %d\n", unitNum);
        }
        return -1;
    }
    
    int i=0;
    int charsRead = 0;
    
    char * msg;
    char * temp;
    int control;
    int result;
    
    if(!terminals[unitNum].readEnabled){
        // Turning read interrupts on if it is not already.
        for(i=0;i<USLOSS_TERM_UNITS;i++){
            control = 0;
            control = USLOSS_TERM_CTRL_RECV_INT(control);
            result = USLOSS_DeviceOutput(USLOSS_TERM_DEV, i+1, control);
        }
        terminals[unitNum].readEnabled = 1;
    }
    
    MboxReceive(terminals[unitNum].bufferBox, msg, maxSize);
    temp = msg;
    
    for(i=0;i<maxSize; i++){
        if(*temp!='\n'){
            charsRead++;
        }
        else{
            break;
        }
        temp++;
    }
    
    //strcat("\n", msg);
    strcpy(address, msg);
    
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
    
    int unit = atoi( (char *) arg);
    char * line;
    long charsWritten = 0;
    
    semvReal(running);
    
    while(!isZapped()){
        MboxReceive(terminals[unit].writeBox, line, MAXLINE);
        // Setting the xmit int enable bit
        int control = 0;
        control = USLOSS_TERM_CTRL_XMIT_INT(control);
        USLOSS_DeviceOutput(USLOSS_TERM_DEV, unit, control);
        char * temp;
        while(1){
            MboxReceive(terminals[unit].outBox, temp, MAXLINE);
            control = USLOSS_TERM_CTRL_CHAR(control, *line);
            control = USLOSS_TERM_CTRL_XMIT_CHAR(control);
            USLOSS_DeviceOutput(USLOSS_TERM_DEV, unit, control);
            if( *line=='\n' || *line == '\0'){
                if(debugFlag){
                    USLOSS_Console("TermWriter(): Reached end of string for write.\n");
                }
                break;
            }
            line++;
            charsWritten++;
        }
        // Disabling CTRL XMIT
        control = USLOSS_TERM_CTRL_XMIT_INT_DISABLE(control);
        USLOSS_DeviceOutput(USLOSS_TERM_DEV, unit, control);
        
        MboxSend(terminals[unit].mutexBox, (void *)charsWritten, 1);
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
    
    char * address;
    int numChars;
    int unitNum;
    address = (char *)args->arg1;
    numChars = (int)args->arg2;
    unitNum = (int)args->arg3;
    long numWritten;
    
    numWritten = termWriteReal(address, numChars, unitNum);
    
    args->arg2 = (void *)numWritten;
}

long termWriteReal(char * address, int numChars, int unitNum){
    
    if(unitNum>USLOSS_MAX_UNITS ){
        if (debugFlag){
            USLOSS_Console("termWriteReal(): Tried to use terminal outside of max units. # %d\n", unitNum);
        }
        return -1;
    }
    MboxSend(terminals[unitNum].writeBox, address, numChars);
    long charsWritten;
    MboxReceive(terminals[unitNum].mutexBox, (void *)charsWritten, 1);
    return charsWritten;
    
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
