#ifndef DCC_EMAILLOG_H
#define DCC_EMAILLOG_H

#define DCC_EMAILLOG_WHOM_TO_BLAME "distcc-pump-errors@google.com"
void dcc_please_send_email(void);
void dcc_setup_log_email(void);
void dcc_maybe_send_email(void);
int dcc_add_file_to_log_email(const char *description, const char *fname);

#endif /* EMAILLOG_H */
