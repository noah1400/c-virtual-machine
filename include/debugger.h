#ifndef _DEBUGGER_H_
#define _DEBUGGER_H_

#include "vm_types.h"

// Runs the debugger on commands from the given stream until the user quits or the commands run out;
// commands that do not come from stdin are echoed. Returns the VM's last error.
int debugger_run(VM *vm, FILE *commands);

#endif // _DEBUGGER_H_
