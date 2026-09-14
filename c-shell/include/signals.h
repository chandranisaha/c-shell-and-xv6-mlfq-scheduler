#ifndef CSHELL_SIGNALS_H
#define CSHELL_SIGNALS_H

void signals_install(void);
int signals_pending(void);
void signals_clear(void);

void signals_ignore_terminal(void);

int signals_interrupt_pending(void);
void signals_clear_interrupt(void);

void signals_restore_terminal_defaults(void);

void signals_install_alarm(void);
int signals_alarm_fired(void);
void signals_clear_alarm(void);

#endif
