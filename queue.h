#ifndef NOCRAZYDOTS_QUEUE_H
#define NOCRAZYDOTS_QUEUE_H

#include "midi.h"

// Maximum number of sections that can be recorded
#define MAXSEC 128

// Percent to randomize velocities in order to avoid to sound too mechanical
extern unsigned char ncd_percent_randomness;

extern signed char ncd_trans_semitones;

typedef struct {
  ncd_midi_event msg;
  char tag; // space for note-unrelated events
  float duration; // 0 for note-unrelated events
} ncd_event;

typedef struct ncd_node { // Struct name needed for defining the next field
  ncd_event *events;
  unsigned char events_size;
  unsigned char events_len;
  float start_time;
  struct ncd_node *next;
} ncd_node;

typedef struct {
  ncd_node* node; // initialized to a NULL pointer
  unsigned char event_no; // zero based array index
} ncd_ev_ref;

ncd_ev_ref ncd_queue_push_event(ncd_event event);
void ncd_queue_push_rest(float duration);
ncd_node* ncd_queue_pop_node();
void ncd_queue_display();
void new_line();
void new_group();
void ncd_play();
void ncd_auto_accompaniment(char tag);
void ncd_section_rec(unsigned char sec_no);
void ncd_section_stop(unsigned char sec_no);
void ncd_section_play(unsigned char sec_no);
void ncd_start_hairpin(bool crescendo, unsigned char percent,
  unsigned char channel, float last_note_dur);
void ncd_stop_hairpin(unsigned char channel, float last_note_dur);

#endif
