//
//  libuser2.c
//  OS
//
//  Created by Jason on 11/2/15.
//  Copyright (c) 2015 Jason. All rights reserved.
//

#include "libuser.h"
#include <phase1.h>
#include <phase2.h>
#include <usyscall.h>
#include <usloss.h>

int TermRead(char * buffer, int maxLen, int unitNum, int *length){
    systemArgs sysArg;
    sysArg.number = SYS_TERMREAD;
    sysArg.arg1 = buffer;
    sysArg.arg2 = maxLen;
    sysArg.arg3 = unitNum;
    USLOSS_Syscall(&sysArg);
    *length = (int) sysArg.arg2;
    return (int) sysArg.arg4;
}

int TermWrite(char * buffer, int strlen, int unitNum, int *length){
    systemArgs sysArg;
    sysArg.number = SYS_TERMWRITE;
    sysArg.arg1 = buffer;
    sysArg.arg2 = strlen;
    sysArg.arg3 = unitNum;
    USLOSS_Syscall(&sysArg);
    *length = (int) sysArg.arg2;
    return (int) sysArg.arg4;
}

int DiskRead(void *diskBuffer, int unit, int track, int first, int sectors, int *status){
	// populate the sysargs struct;
	systemArgs sysArg;
	sysArg.number = SYS_DISKREAD;
	sysArg.arg1 = diskBuffer;
	sysArg.arg2 = sectors;
	sysArg.arg3 = track;
	sysArg.arg4 = first;
	sysArg.arg5 = unit;
	// execute the syscall
	USLOSS_Syscall(&sysArg);
	*status = (int)sysArg.arg1;
	return (int) sysArg.arg4;
}

int DiskWrite(void *diskBuffer, int unit, int track, int first, int sectors, int *status){
	// populate the sysargs struct;
	systemArgs sysArg;
	sysArg.number = SYS_DISKWRITE;
	sysArg.arg1 = diskBuffer;
	sysArg.arg2 = sectors;
	sysArg.arg3 = track;
	sysArg.arg4 = first;
	sysArg.arg5 = unit;
	// execute the syscall
	USLOSS_Syscall(&sysArg);
	*status = (int)sysArg.arg1;
	return (int) sysArg.arg4;
}

int Sleep(int delay){
	// populate the sysargs struct;
	systemArgs sysArg;
	sysArg.number = SYS_SLEEP;
	sysArg.arg1 = delay;
	// execute the syscall
	USLOSS_Syscall(&sysArg);
	return (int) sysArg.arg4;
}
