//
//  libuser2.h
//  OS
//
//  Created by Jason on 11/2/15.
//  Copyright (c) 2015 Jason. All rights reserved.
//

#ifndef __OS__libuser2__
#define __OS__libuser2__

#include <stdio.h>

extern int TermRead(char * buffer, int maxLen, int unitNum, int *length);
extern int TermWrite(char * buffer, int strlen, int unitNum, int *length);
extern int DiskRead(void *diskBuffer, int unit, int track, int first, int sectors, int *status);
extern int DiskWrite(void *diskBuffer, int unit, int track, int first, int sectors, int *status);
extern int  Spawn(char *name, int (*func)(char *), char *arg, int stack_size,
                  int priority, int *pid);
extern int  Wait(int *pid, int *status);
extern void Terminate(int status);
extern void GetTimeofDay(int *tod);
extern void CPUTime(int *cpu);
extern void GetPID(int *pid);
extern int  SemCreate(int value, int *semaphore);
extern int  SemP(int semaphore);
extern int  SemV(int semaphore);
extern int  SemFree(int semaphore);
extern int Sleep(int delay);


//extern int  Spawn(char *name, int (*func)(char *), char *arg, int stack_size,
//                  int priority, int *pid);
//extern int  Wait(int *pid, int *status);
//extern void Terminate(int status);
//extern void GetTimeofDay(int *tod);
//extern void CPUTime(int *cpu);
//extern void GetPID(int *pid);
//extern int  SemCreate(int value, int *semaphore);
//extern int  SemP(int semaphore);
//extern int  SemV(int semaphore);
//extern int  SemFree(int semaphore);

#endif /* defined(__OS__libuser2__) */


