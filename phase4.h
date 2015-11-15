/*
 * These are the definitions for phase 4 of the project (support level, part 2).
 */

#ifndef _PHASE4_H
#define _PHASE4_H

/*
 * Maximum line length
 */

/*
 * Maximum number of processes.
 */

#define MAXPROC      50

/*
 * Maximum length of a process name
 */

#define MAXNAME      50

#define INACTIVE		0
#define ACTIVE			1
#define MAXLINE         80

/* Quantities of Different Devices */
#define DISK_UNITS 2
#define TERM_UNITS 4

/*
 * Function prototypes for this phase.
 */

extern  int  Sleep(int seconds);

extern  int  DiskRead (void *diskBuffer, int unit, int track, int first, 
                       int sectors, int *status);
extern  int  DiskWrite(void *diskBuffer, int unit, int track, int first,
                       int sectors, int *status);
extern  void  DiskSize (int unit, int *sector, int *track, int *disk);
extern  int  TermRead (char *buffer, int bufferSize, int unitID,
                       int *numCharsRead);
extern  int  TermWrite(char *buffer, int bufferSize, int unitID,
                       int *numCharsRead);

extern  int  start4(char *);

typedef struct systemArgs
{
    int number;
    void *arg1;
    void *arg2;
    void *arg3;
    void *arg4;
    void *arg5;
} systemArgs;
//
extern void (*systemCallVec[])(systemArgs *args);

struct ProcStruct {
    int pid;
    int parentPid;
    int status;
    int children[MAXPROC];
    char name[MAXNAME];
    int procMbox;
    int procSem;
    int (*func)(char *);
    char *arg;
    long returnStatus;
} ProcStruct;

struct Terminal {
    int pid;
    int readerPid;
    int writerPid;
    int inBox;
    int outBox;
    int bufferBox;
    int writeBox;
    int mutexBox;
    int readEnabled;
    int writeSem;
    int active;
} Terminal;

struct diskProc {
	int pid;                  // pid of the requesting process
	int unit;                 // number of the unit to use
	int type;                 // request type; use usloss macros
	int track;                // specifies the track on which to read/write
	int first;                // first sector to read
	int sectors;              // number of sectors to read
    void *buffer;             // address of the buffer to which to read
    systemArgs * args;        // pointer to the argument structure
	struct diskProc * next;
} diskProc;

struct clockWaiter{
	int PID;
	int procMbox;
	int secsRemaining;
	struct clockWaiter * next;
}clockWaiter;

#define ERR_INVALID             -1
#define ERR_OK                  0

#endif /* _PHASE4_H */
