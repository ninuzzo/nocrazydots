#ifndef NOCRAZYDOTS_TIMER_H
#define NOCRAZYDOTS_TIMER_H

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>

/* An implementation based on MIDI ticks rather than this simple
   stopwatch may allow synchronization with other MIDI devices. But I
   couldn't find any standard way to set the BPM on the keyboard with
   a MIDI message. It looks like this feature is not supported neither
   by the MIDI standard, nor by my keyboard implementation of it. */

struct timeval ncd_timer_start = (struct timeval){0},
  ncd_timer_stop = (struct timeval){0};
float ncd_time_elapsed = 0;

// 5ms (5000us) latency is the smallest a human being can detect.
#define LATENCY_WARN_THRESHOLD 5000 // In us

/* Latency correction addend. Needed because the latency correction
   algorithm below (see macro CHRONOSLEEP) introduces some latency too.
   Best value must be found by experiment. Faster computers will require
   smaller value. Record a long piece and compare effective with
   theoretical length. If the former is significantly higher, try to
   increment this value until they match. I advice to make use of the
   binary search algorithm to find out a sufficient approximation
   of the right correction with the least number of tries.
   Must be less than LATENCY_WARN_THRESHOLD to make sense. */
#define LATENCY_CORRECTION 2.75 // In us

// Estimate of latency piled up so far.
float ncd_latency = 0;

#define MICROSEC(s) (s.tv_sec * 1000000 + s.tv_usec)
#define STOPWATCH_RESET() (ncd_timer_start = ncd_timer_stop = (struct timeval){0}, ncd_time_elapsed = 0)
#define STOPWATCH_START() gettimeofday(&ncd_timer_start, NULL)
#define STOPWATCH_STOP() gettimeofday(&ncd_timer_stop, NULL)
#define STOPWATCH_READ() (MICROSEC(ncd_timer_stop) - MICROSEC(ncd_timer_start))

#define CHRONOSLEEP(us) { \
  float drift, wait_time; \
  STOPWATCH_STOP(); \
  drift = STOPWATCH_READ() - ncd_time_elapsed + (ncd_latency += LATENCY_CORRECTION); \
  if (drift > LATENCY_WARN_THRESHOLD) { \
    fprintf(stderr, "Warning: %d us latency\n", (int)drift); \
  } \
  ncd_time_elapsed += us; \
  /* Auto-correct most of the delay (latency due to computation)
     by shortening notes and other events to make up */ \
  wait_time = (us) - drift; \
  if (wait_time > 0) { \
    /* Truncate wait_time, do not round it. It is only up to half of
       microsec difference, probably not worthing the effort. */ \
    usleep(wait_time); \
  } \
}

#endif
