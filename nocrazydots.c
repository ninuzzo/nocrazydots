/*
   NoCrazyDots
   Machine and human readable polyphonic music notation
   without crazy dots.
   Supports automated playing and auto-accompainment.

   (c) 2017-2019 Antonio Bonifati aka Farmboy
   <http://farmboymusicblog.wordpress.com>

   This file is part of NoCrazyDots.

   NoCrazyDots is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   NoCrazyDots is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with NoCrazyDots.  If not, see <http://www.gnu.org/licenses/>.
*/
#define VERSION 1.1

// Seconds to wait for MIDI recording to finish
#define WAITMIDI 1

// Including the null byte used to end the command string
#define MAXCMDLEN 255

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <libgen.h>
#include <stdbool.h>
#include <unistd.h>
#include <sched.h>
#include <signal.h>
#include "parser.h"
#include "midi.h"
#include "queue.h"
#include "error.h"

char *ncd_pname;

const char *filename_ext(const char *filename) {
  const char *dot = strrchr(filename, '.');
  if (!dot || dot == filename) return "";
  return dot + 1;
}

int main(int argc, char *argv[]) {
  char *datadir = MIDIDATADIR, tag = ' ', last, *midifile = NULL;
  FILE *fp = stdin;
  bool dump_mode = false;
  struct sched_param sp;

  printf("NoCrazyDots %.1f (c) 2017-2019 Antonio Bonifati \"Farmboy\" under GNU GPL3\n",
    VERSION);

  // Argument parsing without option-switches. Ambiguous, but in rare cases...
  ncd_pname = basename(argv[0]);
  while (*++argv) {
    last = (*argv)[strlen(*argv) - 1];
    if (strncmp(*argv, "hw:", 3) == 0 || STREQ(*argv, virtual)) {
      strncpy(ncd_midi_port_name, *argv, DEVMAXLEN - 1);
    } else if (strlen(*argv) == 1) {
      tag = (*argv)[0];
    } else if (STREQ2(*argv, -dump, -d)) {
      dump_mode = true;
    } else if (last == '%') {
      ncd_percent_randomness = atoi(*argv);
    } else if ((*argv)[0] == '+' || (*argv)[0] == '-') {
      ncd_trans_semitones = atoi(*argv);
    } else if (last == '/') {
      datadir = *argv;
    } else if (STREQ(filename_ext(*argv), mid)) {
      // MIDI file generation implies outputting on a virtual MIDI port
      strcpy(ncd_midi_port_name, "virtual");
      midifile = *argv;
    } else {
      error_if((fp = fopen(*argv, "r")) == NULL);
    }
  }

  // Try to run in real-time context to reduce latency.
  sp.sched_priority = 98;
  if (sched_setscheduler(getpid(), SCHED_FIFO, &sp) == -1) {
    warning(0, "warning: cannot gain realtime privileges. See README.md");
  }

  srand(time(NULL)); // for unpredictable random numbers
  ncd_midi_init();
  
  if (dump_mode) {
    ncd_midi_dump();
  } else {
    ncd_midi_load_voices(datadir);

    ncd_parse(fp);
    #ifdef DEBUG
    ncd_queue_display();
    #endif

    if (tag == ' ') {
      if (midifile) {
        pid_t recpid;
        
        if ((recpid = fork()) == 0) {
          char reccmd[MAXCMDLEN];
          snprintf(reccmd, sizeof(reccmd),
            "arecordmidi -p $(arecordmidi -l | sed -n '$s/ .*//p') \"%s\"",
            midifile);

          // Child process
          error_if(execl("/bin/sh", "sh", "-c", reccmd, (char *) 0) == -1);
        }

        // To be practically sure the first notes have been recorded
        sleep(WAITMIDI);

        // Parent process
        ncd_play();

        // To be practically sure the last notes have been recorded
        sleep(WAITMIDI);

        kill(recpid, SIGINT); // Stop MIDI recorder
      } else {  
        ncd_play();
      }
    } else {
      ncd_auto_accompaniment(tag);
    }
  }
  
  return EXIT_SUCCESS;
}
