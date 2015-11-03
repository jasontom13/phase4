//
//  libuser2.c
//  OS
//
//  Created by Jason on 11/2/15.
//  Copyright (c) 2015 Jason. All rights reserved.
//

#include "libuser2.h"
#include <phase1.h>
#include <phase2.h>
#include <libuser.h>
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