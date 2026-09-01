/*
 * Logging for CFC2 Access.
 *
 * Writes to CFC2Access.log next to the game exe. This is the primary evidence
 * channel for every stage: the mod runs inside a packed process where a
 * debugger may not attach, so the log has to carry the diagnosis.
 */
#ifndef CFC2_LOG_H
#define CFC2_LOG_H

void log_init(void);
void log_shutdown(void);

/* printf-style. Always append a newline; the implementation adds one. */
void log_line(const char *fmt, ...);

#endif /* CFC2_LOG_H */
