/*
   NoCrazyDots
   Machine and human readable polyphonic music notation
   without crazy dots.
   Supports automated playing and auto-accompainment.

   (c) 2017-2019 Antonio Bonifati aka Farmboy
   <http://farmboymusicblog.wordpress.com>>

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

// Build a MIDI-event queue efficiently.

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <stdbool.h>
#include "error.h"
#include "queue.h"
#include "midi.h"
#include "parser.h"
#include "timer.h"

#define min(a, b) (((a) < (b)) ? (a) : (b))

// Default number of beats per minute. Each beat is a quarter note.
#define DEFBPM 60

// Define duration of the pitch wheel ascending slope
#define PITCH_WHEEL_DUR 150000 // in us

// Each expression volume increment/decrement or pitch wheel change
// will be done in this time interval (in us). Must be a fraction
// of PITCH_WHEEL_DUR
#define EXPR_STEP 1500 // e.g. 100000 us = 0.1s

static struct {
  ncd_node *start;
  ncd_node *end;
  float start_time;
  // Rest bright at the end of the recording.
  float end_rest;
} section[MAXSEC];

// Lookup table to implement volume dynamics.
typedef struct {
  float start_time;
  /* Since the event array may get reallocated, we cannot simple store
     a pointer to an element of that array as in

     ncd_event *ev; // initialized to a NULL pointer

     because it may change.
     We thus have to store a pointer to the node containing the
     event list and the number of event in the list. */
  ncd_ev_ref ev_ref;
} ncd_hairpin_table;

static ncd_hairpin_table hairpin[MIDI_CHANNELS];

// Convertion factor from BPM to ms
#define BPM2US(bpm) (2.4E8 / (bpm)) // 2.4E8 = 1000000 ms * 60s / (1/4)
// Default max random error percentage. E.g 5 for 5%
// (to better simulate human playing)
#define DEFRAND 0

unsigned char bpm = DEFBPM,
  ncd_percent_randomness = DEFRAND;

// Number of transposition semitones
signed char ncd_trans_semitones = 0;

// returns a random number x +- ncd_percent_randomness%
#define RANDOMIZE(x)  ((x)-((x)*ncd_percent_randomness/100) \
  + rand() % (int)((x)*ncd_percent_randomness/50 + 1))
 
typedef struct {
  ncd_node *start; // score start (overall queue begin)
  ncd_node *tail; // last element of the queue

  ncd_node *head; // begin of the queue (current line)
} ncd_queue;

// Note queue to represent the score in memory.
static ncd_queue queue;

static float
  start_group_time = 0,
  current_time = 0; // this serves as a priority value

int first_group = 1;

// As a rule of thumb this should not be lower of the number of notes
// your keyboard can play at once, but also accounts for other meta-events.  
#define MAXEVENTS 64
// How many events to preallocate per node, min 1, max MAXPOLIPHONY
#define INITEVENTNO 3
// Smallest measure subdivision (relative to measure)
// https://en.wikipedia.org/wiki/Two_hundred_fifty-sixth_note
#define SMALLESTDUR 1.0/256

#define EQUALTIMES(a, b) (fabsf((a) - (b)) < SMALLESTDUR)
 
void new_group() {
  start_group_time = current_time;
  queue.head = queue.tail;
  if (queue.head) {
    first_group = 0;
  }
}

void new_line() {
  current_time = start_group_time;
}

void add_note(ncd_node *node, ncd_event note) {
  if (node->events_len >= node->events_size) {
    node->events_size *= 2;
    if (node->events_size > MAXEVENTS) {
      node->events_size = MAXEVENTS;
    }
    error_check(node->events_len >= node->events_size, 0,
      "Reached MAXEVENTS (%d)", MAXEVENTS);
    node->events = realloc(node->events, node->events_size * sizeof(ncd_event));
  }
  
  node->events[node->events_len++] = note;
}

ncd_node *new_node(float start_time) {
  ncd_node *node;
  error_if((node = malloc(sizeof(ncd_node))) == NULL);
  error_if((node->events = malloc(INITEVENTNO * sizeof(ncd_event))) == NULL);
  node->events_size = INITEVENTNO;
  node->events_len = 0;
  node->start_time = start_time;
  node->next = NULL;

  return node;
}

ncd_node *dup_node(ncd_node *node, float start_time) {
  ncd_node *copy;
  error_if((copy = malloc(sizeof(ncd_node))) == NULL);
  copy->events = node->events; // share events to save memory
  copy->events_size = node->events_size;
  copy->events_len = node->events_len;
  copy->start_time = start_time;
  copy->next = NULL;

  return copy;
}

// Insertion sort on a queue starting from a specific point
// Returns a pointer to the event for possible later reference.
ncd_ev_ref ncd_queue_push_event(ncd_event note) {
  ncd_ev_ref ret;
  ncd_node *curr, *prev, *new;
  unsigned char status = note.msg[MIDI_STATUS] & 0xF0;
  bool meta_event = (status == MIDI_CONTROLLER && note.msg[MIDI_DATA1] == MIDI_EXPRESSION_MSB)
    || status == MIDI_PITCH_WHEEL;
  float start_time = (status == MIDI_NOTEOFF) ?
    current_time + note.duration : current_time;

  for (curr = queue.head, prev = NULL; curr;
       prev = curr, curr = curr->next) {
    if (EQUALTIMES(curr->start_time, start_time)) {
      add_note(curr, note);
      if (!meta_event) {
        current_time += note.duration;
      }
      ret.node = curr;
      ret.event_no = curr->events_len - 1;
      return ret;
    } else if (curr->start_time > start_time) {
      break;
    }
  }

  /* head and tail insertion */
  new = new_node(start_time);
  add_note(new, note);
  new->next = curr;
  if (curr == NULL) {
    queue.tail = new;
  }
  if (prev != NULL) {
    prev->next = new;
  } else {
    queue.head = new;
    if (first_group) {
      queue.start = queue.head;
    }
  }

  if (!meta_event) {
    current_time += note.duration;
  }

  ret.node = new;
  ret.event_no = 0;
  return ret;
}

void ncd_queue_push_rest(float duration) {
  // not queued, it just increments current time
  current_time += duration;
}

ncd_node *ncd_queue_pop_node() {
  ncd_node *ret = queue.start;
  if (queue.start) {
    queue.start = (queue.start)->next;
  } else {
    queue.tail = NULL;
  }
  return ret;
}

void ncd_free_node(ncd_node *node) {
  free(node->events);
  free(node);
}

// Useful for debugging
void ncd_queue_display() {
  ncd_node *node;
  ncd_event note;
  int i;
  unsigned char type, channel;
  
  puts("tag\ttype\tstart_time\tchannel\tmidi_note\tvelocity\tduration");
  for (node = queue.start; node; node = node->next) {
    for (i = 0; i < node->events_len; i++) {
      note = node->events[i];
      channel = note.msg[MIDI_STATUS] & 0x0F;
      if (note.msg[MIDI_STATUS] == MIDI_META && note.msg[MIDI_DATA1] == MIDI_SET_TEMPO) {
          printf("set tempo to %hhu bpm\n", note.msg[MIDI_DATA2]);
      } else if ((note.msg[MIDI_STATUS] & 0xF0) == MIDI_CONTROLLER
                  && note.msg[MIDI_DATA1] == MIDI_EXPRESSION_MSB) {
        printf("\t%s\t%.3f\t\t%hhu\t\t\t%hhu%%\t\t%.3f\t%p\n",
          note.msg[MIDI_DATA2] & 0x80 ? "cresc" : "decresc",
          node->start_time, channel + 1, note.msg[MIDI_DATA2] & 0x7F,
          note.duration, &(node->events[i]));
      } else {
        type = note.msg[MIDI_STATUS] & 0xF0;
        if (type == MIDI_NOTEON || type == MIDI_NOTEOFF) {
          if (channel != DRUMCHANNEL) {
            printf("%c\t%02x\t%.3f\t\t%hhu\t%hhu (%hhu%s)\t%hhu\t\t%.3f\n",
              note.tag, type, node->start_time, channel + 1,
              note.msg[MIDI_DATA1], MIDI_OCTAVE(note.msg[MIDI_DATA1]),
              midi_note_no_name[MIDI_NOTE_NO(note.msg[MIDI_DATA1])],
              note.msg[MIDI_DATA2], note.duration);
          } else {
            printf("%c\t%02x\t%.3f\t\t%hhu\t%hhu (%-3s)\t%hhu\t\t%.3f\n",
              note.tag, type, node->start_time, channel + 1,
              note.msg[MIDI_DATA1], midi_drum_name[note.msg[MIDI_DATA1]],
              note.msg[MIDI_DATA2], note.duration);
          }
        }
        // TODO
        // does not display other midi events
        // (e.g. instrument changes, volume dynamics)
      }
    }
    putchar('\n');
  }
}

void ncd_play() {
  ncd_node *node;
  ncd_midi_event msg;
  float prev_start_time = 0, conv_unit = BPM2US(DEFBPM),
    final_volume, volume_delta, curr_volume, internote_delay,
    new_curr_value; // for both volume and pitch wheel value
  unsigned char status, channel, i;
  signed char semitones; // for sliding
  
  STOPWATCH_START();
  error_check(queue.start == NULL, 0, "Playing empty score");
  for (node = queue.start; node; node = node->next) {
    internote_delay = (node->start_time - prev_start_time) * conv_unit;
 
    while (internote_delay >= EXPR_STEP) {
      CHRONOSLEEP(EXPR_STEP);
      internote_delay -= EXPR_STEP;

      for (channel = 0; channel < MIDI_CHANNELS; channel++) {
        if (ncd_expression[channel].left_duration) {
          new_curr_value = ncd_expression[channel].current + ncd_expression[channel].volume_step;
          if (new_curr_value > 127 || new_curr_value < 0) {
            // no use to keep increasing/decreasing volume on this channel
            ncd_expression[channel].left_duration = 0;
            continue;
          }

          if ((ncd_expression[channel].left_duration -= EXPR_STEP / conv_unit) < 0) {
            ncd_expression[channel].left_duration = 0;
          }

          // Spare bandwidth... only send a volume change message
          // if the new volume is actually different than the current one
          if ((int)new_curr_value != (int)ncd_expression[channel].current) {
            ncd_midi_set_volume((unsigned char)new_curr_value, channel);
          }
          ncd_expression[channel].current = new_curr_value;
        }

        // Pitch wheel manipulation for sliding is akin to volume
        // change for expression
        if (ncd_pitch_wheel[channel].left_duration) {
          new_curr_value = ncd_pitch_wheel[channel].current
            + ncd_pitch_wheel[channel].value_step;

          if (new_curr_value > 0x3FFF || new_curr_value < 0) {
            // no use to keep increasing/decreasing pitch on this channel
            ncd_pitch_wheel[channel].left_duration = 0;
            continue;
          }

          if ((ncd_expression[channel].left_duration -= EXPR_STEP / conv_unit) < 0) {
            ncd_expression[channel].left_duration = 0;
          }

          // Spare bandwidth... only send a pitch wheel change message
          // if the new pitch is actually different than the current one
          if ((int)new_curr_value != (int)ncd_pitch_wheel[channel].current) {
            ncd_midi_pitch_wheel((unsigned short)new_curr_value, channel);
          }
          ncd_pitch_wheel[channel].current = new_curr_value;
        }
      }
    }
    CHRONOSLEEP(internote_delay);

    // Reset pitch wheel to center position at the end of bent note
    // for all channels.
    for (channel = 0; channel < MIDI_CHANNELS; channel++) {
      // Spare bandwidth... only send a pitch wheel change message
      // if the pitch wheel is not already centered
      if (ncd_pitch_wheel[channel].current != NOBENDING) {
        ncd_pitch_wheel[channel].current = NOBENDING;
        ncd_midi_pitch_wheel(NOBENDING, channel);
      }
    }

    for (i = 0; i < node->events_len; i++) {
      memcpy(msg, node->events[i].msg, sizeof(ncd_midi_event));
      status = msg[MIDI_STATUS] & 0xF0;
      channel = msg[MIDI_STATUS] & 0x0F;

      // Warning: non standard but works
      if (msg[MIDI_STATUS] == MIDI_META && msg[MIDI_DATA1] == MIDI_SET_TEMPO) {
        conv_unit = BPM2US(msg[MIDI_DATA2]);
      } else if (status == MIDI_CONTROLLER
             && msg[MIDI_DATA1] == MIDI_EXPRESSION_MSB) {
          curr_volume = ncd_expression[channel].current;
          if ((msg[MIDI_DATA2] & 0x80)) { // crescendo
            final_volume = ncd_expression[channel].reference
              * (100.0 + (msg[MIDI_DATA2] & 0x7F)) / 100;
          } else { // decrescendo
            final_volume = ncd_expression[channel].reference
              * (100.0 - (msg[MIDI_DATA2] & 0x7F)) / 100;
          }

          // Volume limiter
          if (final_volume > 127) {
            final_volume = 127;
            warning(ncd_parser_line_no,
              "warning: expression hairpin on channel %hhu increased volume to a value >127."
              " Clipped to 127.\nConsider user a smaller percentage.\n",
              channel + 1);
          } else if (final_volume < 0) {
            final_volume = 0;              
            warning(ncd_parser_line_no,
              "warning: expression hairpin on channel %hhu decreased volume to a value <0."
              " Clipped to 0.\nConsider user a smaller percentage.\n",
              channel + 1);
          }

          volume_delta = final_volume - curr_volume;
          if ((msg[MIDI_DATA2] & 0x80)) {
            if (volume_delta < 0) {
              warning(ncd_parser_line_no,
                "warning: current volume is greater than final crescendo volume. Did you mean a decrescendo?");
            }
          } else if (volume_delta > 0) {
            warning(ncd_parser_line_no,
              "warning: current volume is less than final decrescendo volume. Did you mean a crescendo?");
          }

          /* From proportion:

                 volume_delta           volume_step
            ------------------------ = -------------
              duration * conv_unit      EXPR_STEP
          */
          ncd_expression[channel].volume_step = EXPR_STEP * volume_delta
            / ( (ncd_expression[channel].left_duration = node->events[i].duration)
                * conv_unit );
          if (fabsf(ncd_expression[channel].volume_step) > fabsf(volume_delta)) {
            ncd_expression[channel].volume_step = volume_delta;
            warning(ncd_parser_line_no,
              "warning: expression hairpin does not apply: duration too short\n");
          }
      } else if (status == MIDI_PITCH_WHEEL) {
        // TODO: this code assumes the pitch wheel range is only a tone.
        // see "Errata" at http://midi.teragonaudio.com/tech/midispec/wheel.htm

        semitones = ncd_pitch_wheel[channel].semitones = msg[MIDI_DATA1];
        if (abs(semitones) > 2) {
          warning(ncd_parser_line_no,
            "warning: sliding more than one tone is currently not supported");
          semitones = semitones > 0 ? 2 : -2;
        }

        ncd_pitch_wheel[channel].current = NOBENDING;

	/* Change pitch linearly for
             slope_duration = min(PITCH_WHEEL_DUR,node->events[i].duration)
	   us and if time is left (note is longer than that), keep it
	   constant. The descending slope due to the pitch wheel
	   spring is not implemented. This it is usually faster than a
           single EXPR_STEP.

           From proportion:

            semitones * 0x1000           value_step
          --------------------------- = ------------
           slope_duration * conv_unit     EXPR_STEP
        */
        ncd_pitch_wheel[channel].value_step = EXPR_STEP * semitones * 0x1000
          / ( (ncd_pitch_wheel[channel].left_duration =
                 min(PITCH_WHEEL_DUR / conv_unit, node->events[i].duration))
              * conv_unit );
      } else {
	if (status == MIDI_NOTEON) {
          msg[MIDI_DATA2] = RANDOMIZE(msg[MIDI_DATA2]);
        }
        if (channel != DRUMCHANNEL
             && (status == MIDI_NOTEON  || status == MIDI_NOTEOFF)) {
          msg[MIDI_DATA1] += ncd_trans_semitones;
        }
        NCD_MIDI_EVENT(msg);
      }
    }
    prev_start_time = node->start_time;
  }
}

// the human player will play notes tagged with tag
// Note: it does support neither dynamics (crescendo/diminuendo) nor slides
void ncd_auto_accompaniment(char tag) {
  ncd_node *node;
  ncd_midi_event *note;
  register ncd_event *event;
  register ncd_midi_event *msg;
  int i, ev_to_wait;
  float prev_start_time = 0, duration,
    conv_unit = BPM2US(DEFBPM);
  unsigned char status, channel;

  error_check(queue.start == NULL, 0, "Playing empty score");
  STOPWATCH_RESET();
  for (node = queue.start; node; node = node->next) {
    // count the number of events that should be played by the human
    ev_to_wait = 0;
    for (i = 0; i < node->events_len; i++) {
      if (node->events[i].tag == tag) {
        ev_to_wait++;
      }
    }

    #ifdef DEBUG
    printf("%d events to wait\n", ev_to_wait);
    #endif

    if (ev_to_wait == 0) {
      if ((duration = node->start_time - prev_start_time)) {
        STOPWATCH_STOP();
        usleep(duration * conv_unit - STOPWATCH_READ());
      }
    } else {
      // wait until all events happened and take them out of the event list
      #ifdef DEBUG
      next:
      #endif
      while (ev_to_wait) {
        note = ncd_midi_wait_note();

        // find event in list
        for (i = 0; i < node->events_len; i++) {
          event = &(node->events[i]);
          if (event->tag == tag) {
            msg = &(event->msg);
          
            if (ncd_midi_same_event(*msg, *note)) {
              #ifdef DEBUG
              printf("matched %02hhx %hhu%s %02hhx\n", (*msg)[MIDI_STATUS],
                MIDI_OCTAVE((*msg)[MIDI_DATA1]), midi_note_no_name[MIDI_NOTE_NO((*msg)[MIDI_DATA1])],
                (*msg)[MIDI_DATA2]);
              #endif

              ev_to_wait--;
              // remove node->events[i] from array
              node->events_len--;
              while (i < node->events_len) {
                 node->events[i] = node->events[i+1];
                 i++;
              }
              #ifdef DEBUG
              goto next;
              #endif
              break;
            }
          }
        }
        #ifdef DEBUG
        printf(" unmatched\n");
        #endif
      }
    }

    STOPWATCH_START();

    #ifdef DEBUG
    printf("%d events to send\n", node->events_len);
    #endif

    // play the remaining event list
    for (i = 0; i < node->events_len; i++) {
      event = &(node->events[i]);
      msg = &(event->msg);
      // Warning: non standard but works
      if ((*msg)[MIDI_STATUS] == MIDI_META && (*msg)[MIDI_DATA1] == MIDI_SET_TEMPO) {
        conv_unit = BPM2US((*msg)[MIDI_DATA2]);
      } else if (((*msg)[MIDI_STATUS] & 0xF0) != MIDI_CONTROLLER) {
        status = (*msg)[MIDI_STATUS] & 0xF0;
        channel = (*msg)[MIDI_STATUS] & 0x0F;
		if (status == MIDI_NOTEON) {
          (*msg)[MIDI_DATA2] = RANDOMIZE((*msg)[MIDI_DATA2]);
        }
        if (channel != DRUMCHANNEL
             && (status == MIDI_NOTEON  || status == MIDI_NOTEOFF)) {
          (*msg)[MIDI_DATA1] += ncd_trans_semitones;
	    }
        NCD_MIDI_EVENT(*msg);
      }
    }

    prev_start_time = node->start_time;
  }
}

void ncd_section_rec(unsigned char sec_no) {
  section[sec_no].start = queue.tail;

  // this can be later than queue.tail->start_time
  // for they may rests before a recording section
  section[sec_no].start_time = current_time;
}

void ncd_section_stop(unsigned char sec_no) {
  section[sec_no].end_rest = current_time
    - (section[sec_no].end = queue.tail)->start_time;

  // fix the section start if needed
  if (section[sec_no].start == NULL) {
    // set start of section as the queue start if recording directive
    // is omitted or comes before the very first note
    section[sec_no].start = queue.start;
  } else if (section[sec_no].start_time > section[sec_no].start->start_time) {
    // there was a rest before the recording
    section[sec_no].start = section[sec_no].start->next;
  }
}

void ncd_section_play(unsigned char sec_no) {
  ncd_node *p;
  float prev_start_time = section[sec_no].start_time;
  unsigned char i;
  register ncd_event *event;

  error_check((p = section[sec_no].start) == NULL, ncd_parser_line_no,
    "Trying to playing section no %hhu not previously recorded",
    sec_no + 1);

  // append a copy of the section to the MIDI-event queue.
  do {
	current_time += p->start_time - prev_start_time;
	if (EQUALTIMES(current_time, queue.tail->start_time)) {
	  // leave off all note off events in the first node of the section
	  // since they belong to notes coming right before the section
	  for (i = 0; i < p->events_len; i++) {
		event = &(p->events[i]);
		if ((event->msg[MIDI_STATUS] & 0xF0) != MIDI_NOTEOFF) {
	      add_note(queue.tail, *event);
	    }
	  }
    } else if (p == section[sec_no].end) {
      queue.tail->next = new_node(current_time);
      queue.tail = queue.tail->next;
      // leave off all note on events in the last node of the section
      // since they belong to notes coming right after the section
      for (i = 0; i < p->events_len; i++) {
		event = &(p->events[i]);
		if ((event->msg[MIDI_STATUS] & 0xF0) != MIDI_NOTEON) {
		  add_note(queue.tail, *event);
	    }
	  }
	  
	  // add a possible final rest
      current_time += section[sec_no].end_rest;

	  break;
    } else {
      queue.tail->next = dup_node(p, current_time);
      queue.tail = queue.tail->next;
    }
    
    prev_start_time = p->start_time;
    p = p->next;
  } while (1);
}

void ncd_start_hairpin(bool crescendo, unsigned char percent,
  unsigned char channel, float last_note_dur) {
  ncd_event ev;
  ncd_hairpin_table *hp = &(hairpin[channel]);

  if (hp->ev_ref.node) {
    // This hairpin starts where previous ended, close the latter.
    ncd_stop_hairpin(channel, last_note_dur);
  }

  // Queue up a MIDI expression event (non-standard but it works)
  ev.msg[MIDI_STATUS] = MIDI_CONTROLLER | channel;
  ev.msg[MIDI_DATA1] = MIDI_EXPRESSION_MSB;
  ev.msg[MIDI_DATA2] = (crescendo << 7) | percent;
  ev.tag = ' ';
  // We will add duration to this event later,
  // when we read another hairpin or find HAIRPIN_END.

  hp->ev_ref = ncd_queue_push_event(ev);
  hp->start_time = current_time + last_note_dur;
}

void ncd_stop_hairpin(unsigned char channel, float last_note_dur) {
  ncd_hairpin_table *hp = &(hairpin[channel]);
  
  error_check(hp->ev_ref.node == NULL, ncd_parser_line_no, "No hairping to close");

  // Compute hairpin length.
  hp->ev_ref.node->events[hp->ev_ref.event_no].duration
    = current_time + last_note_dur - hp->start_time;

  hp->ev_ref.node = NULL; // There is no more a hairpin to end.
}

void ncd_slide(signed char semitones, unsigned char channel, float next_note_dur) {
  ncd_event ev;
  
  // Queue up a MIDI pitch wheel event (non-standard but it works):
  ev.msg[MIDI_STATUS] = MIDI_PITCH_WHEEL | channel;
  ev.msg[MIDI_DATA1] = semitones;
  ev.msg[MIDI_DATA2] = 0;
  ev.tag = ' ';
  ev.duration = next_note_dur;
  
  ncd_queue_push_event(ev);
}

