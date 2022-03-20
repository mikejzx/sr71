#ifndef SIGHANDLE_H
#define SIGHANDLE_H

void sighandle_register(void);

extern volatile bool g_sigint_caught;

#endif
