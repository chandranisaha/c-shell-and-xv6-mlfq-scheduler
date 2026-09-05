#ifndef CSHELL_SIGNALS_H
#define CSHELL_SIGNALS_H

void signals_install(void);
int signals_pending(void);
void signals_clear(void);

#endif
