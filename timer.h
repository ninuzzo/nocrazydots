#ifndef NOCRAZYDOTS_TIMER_H
#define NOCRAZYDOTS_TIMER_H

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>

struct timeval ncd_timer_start = (struct timeval){0},
  ncd_timer_stop = (struct timeval){0};

#define LATENCY_WARN_THRESHOLD -10 // In us

#define MICROSEC(s) (s.tv_sec * 1000000 + s.tv_usec)
#define STOPWATCH_RESET() (ncd_timer_start = ncd_timer_stop = (struct timeval){0})
#define STOPWATCH_START() gettimeofday(&ncd_timer_start, NULL)
#define STOPWATCH_STOP() gettimeofday(&ncd_timer_stop, NULL)
#define STOPWATCH_READ() (MICROSEC(ncd_timer_stop) - MICROSEC(ncd_timer_start))

// Truncate wait_time, do not round it, so that we make up for latency
// between starting and stopping the timer.
#define CHRONOSLEEP(us) { \
  float wait_time; \
  STOPWATCH_STOP(); \
  wait_time=(us) - STOPWATCH_READ(); \
  if (wait_time < 0) { \
    if (wait_time < LATENCY_WARN_THRESHOLD) { \
      fprintf(stderr, "Warning: %d us latency\n", (int)wait_time); \
    } \
  } else { \
    usleep(wait_time); \
  } \
  STOPWATCH_START(); \
}

#endif
