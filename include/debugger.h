#ifndef _DEBUGGER_H_
#define _DEBUGGER_H_

#include "vm_types.h"

// Runs the interactive debugger on stdin until the user quits; returns the VM's last error
int debugger_run(VM *vm);

#endif // _DEBUGGER_H_
