#ifndef NOCRAZYDOTS_ERROR_H
#define NOCRAZYDOTS_ERROR_H

void error_if(int cond);
void warning(int line_no, char *msg, ...);
void error_check(int cond, int line_no, char *msg, ...);
// https://gcc.gnu.org/onlinedocs/cpp/Variadic-Macros.html
#define trigger_error(line_no, msg, ...) \
  error_check(1, (line_no), #msg, ##__VA_ARGS__)

#endif
