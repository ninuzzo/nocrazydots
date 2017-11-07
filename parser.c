/*
   NoCrazyDots
   Machine and human readable polyphonic music notation
   without crazy dots.
   Supports automated playing and auto-accompainment.

   (c) 2017 Antonio Bonifati aka Farmboy <http://farmboy.tk>

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
#include <stdarg.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdbool.h>
#include "parser.h"
#include "error.h"
#include "midi.h"
#include "queue.h"

// Default starting note for relative pitch numbers.
// MIDI_NOTE(5, 0) is central do (central C)
#define DEFNOTE MIDI_NOTE(5, 0)

// These macros form a sort of lexer, although
// parser and lexer are not really fully separated.
#define NEXTC() { \
  if ((c) == '\n') ncd_parser_line_no++; \
  (c) = getc(fp); \
  error_if(ferror(fp)); \
}

#define SKIPBLANKS() while (isblank(c)) NEXTC()

// Skip note-component separator or note-span indicator at the end of note
#define SKIPSEP() while (c == SEP) NEXTC()

// this must be called when you are sure c contains a digit
#define READNUM(type, var) { \
  ungetc(c, fp); \
  fscanf(fp, "%" #type, &(var)); \
  NEXTC(); \
}

#define READCHANNEL(channel) { \
  READNUM(hhu, (channel)); \
  error_check(channel > MIDI_CHANNELS, ncd_parser_line_no, \
    "Invalid channel number %hhu. There are only %u channels available", \
    (channel), MIDI_CHANNELS); \
  (channel)--; \
}

#define PUSHNOTE() { \
  /* push the previous note or tied notes as one */ \
  ncd_queue_push_event(note); \
  /* push the respective noteoff event */ \
  note.msg[MIDI_STATUS] = (note.msg[MIDI_STATUS] & 0x0F) | MIDI_NOTEOFF; \
  /* release velocity is not used, set it to zero */ \
  note.msg[MIDI_DATA2] = 0; \
  note.duration = 0; \
  ncd_queue_push_event(note); \
}

#define ADVANCE() { NEXTC(); SKIPBLANKS(); }
#define BAR '|'
#define BEAT ':' // optional beat separator 
#define TIE '^'
#define QUOTE '"'
#define DOT '.'
#define SEP '_' // note name - duration/velocity separator (sometimes optional)
#define PER 'x'
#define CRESCENDO '<'
#define DIMINUENDO '>'
#define HAIRPIN_END '='

const char *midi_note_no_name[12] = {
  "do", "di", "re", "ri", "mi", "fa", "fi", "so", "si", "la", "li", "ti"
};

extern char *ncd_pname;
static int c,  /* look-ahead character */
  no_notes = true; /* Is the score empty? Pessimism */
static char id[MAXIDLEN+1], tag;
static unsigned char channel,
  velocity = DEFVELOCITY, // current velocity
  start_note = DEFNOTE; // current start note for relative pitch notation
static bool tie, // whether the next note is tied to the previous
  note_stored; // is there a note stored in note?
static ncd_event note;
static int octave = DEFOCTAVE; // current octave
static float duration = DEFDURATION; // current duration

int ncd_parser_line_no;

void parse_directives(FILE *fp) {
  register unsigned int i;
  unsigned char volume, bpm, section, repeats;
  bool quote;

  do {
    if ((quote = (c == QUOTE))) {
      NEXTC();
    }
    id[i = 0] = c;
    NEXTC();
    while ((quote && c != QUOTE) || (!quote && !isdigit(c))) {
      error_check(c == '\n', ncd_parser_line_no, "Unterminated directive");
      error_check(++i == MAXIDLEN, ncd_parser_line_no, "Identifier too long");
      id[i] = c;
      NEXTC();
    }
    if ((quote && (c == QUOTE))) {
      NEXTC();
    } else {
      // trim right
      while (id[i] == ' ') i--;
    }
    id[i+1] = '\0';

    if (STREQ(id, bpm)) {
      READNUM(hhu, bpm);
      ncd_midi_set_tempo(bpm);
    } else if (STREQ2(id, r, rec) || STREQ2(id, s, stop)
        || STREQ2(id, p, play)) { // pattern recording and playback
      SKIPBLANKS();
      error_check(!isdigit(c), ncd_parser_line_no,
        "Section recording directive needs a section number, found `%c'", c);
      READNUM(hhu, section);
      // section numbers start at 1, but internally are 0-based
      --section;
      switch (id[0]) {
        case 'r':
          ncd_section_rec(section);
        break;
        
        case 's':
          ncd_section_stop(section);
        break;
        
        default:
          SKIPBLANKS();
      if (c == PER) {
        ADVANCE();
      }
          if (isdigit(c)) {
            READNUM(hhu, repeats);
          } else {
            repeats = 1;
          }
          for (i = 0; i < repeats; i++) {
            ncd_section_play(section);
          }
        break;
      }    
    } else {
      READCHANNEL(channel);
      SKIPBLANKS();
      error_check(!isdigit(c), ncd_parser_line_no,
        "Volume must follow channel number for voice %s, found `%c'", id, c);
      READNUM(hhu, volume);
      ncd_midi_set_voice(id, channel, volume, true);
    }
    SKIPBLANKS();
    if (c == BAR) {
      ADVANCE();
    }
  } while (c != '\n');
  NEXTC();
}

void parse_note(FILE *fp) {
  unsigned char note_no, midi_note;
  bool is_note, // is it a note or a rest?
    // whether a number or numerator has been read at the beginning of a note/rest token
    num_read,
    // whether an id has been read after a possible number
    id_read,
    // whether the number was separated from the fraction to indicate
    // it is not a numerator but can be a relative note number
    number_separated;
  int
    denom, // denominator of a duration fraction
    dots_power; // power of two to compute dotted notes duration
  char drum_id[MAXIDLEN];
  float num;
  register int i;

  // Read a number and/or (following) id in advance.
  
  if (isdigit(c) || (c == '-' && channel != DRUMCHANNEL)) {
    num_read = true;
    /* we still do not know, whether it is an octave number of a note
     or duration or numerator of a duration fraction of a rest */
    READNUM(f, num);
    if ((number_separated = (c == SEP))) {
      SKIPSEP();
    }
  } else {
    num_read = number_separated = false;
  }
  
  if (isalpha(c)) {
    id_read = true;
    id[i = 0] = c;
    NEXTC();
    while (isalpha(c)) {
      error_check(++i == MAXIDLEN, ncd_parser_line_no, "Identifier too long");
      id[i] = c;
      NEXTC();
    }
    id[++i] = '\0';
    SKIPSEP();
  } else {
    id_read = false;
  }
  
  error_check(!num_read && !id_read && c != '/', ncd_parser_line_no,
    "Unexpected char `%c'", c);
  
  is_note = false; // unless you find a note, it is a rest
  if (id_read) {
    if (channel != DRUMCHANNEL) {
      if (STREQ(id, do)) { // absolute note names
        note_no = 0;
        is_note = true;
      } else if (STREQ2(id, di, ra)) {
        note_no = 1;
        is_note = true;
      } else if (STREQ(id, re)) {
        note_no = 2;
        is_note = true;
      } else if (STREQ2(id, ri, me)) {
        note_no = 3;
        is_note = true;
      } else if (STREQ(id, mi)) {
        note_no = 4;
        is_note = true;
      } else if (STREQ(id, fa)) {
        note_no = 5;
        is_note = true;
      } else if (STREQ2(id, fi, se)) {
        note_no = 6;
        is_note = true;
      } else if (STREQ2(id, so, sol)) {
        note_no = 7;
        is_note = true;
      } else if (STREQ2(id, si, le)) {
        note_no = 8;
        is_note = true;
      } else if (STREQ(id, la)) {
        note_no = 9;
        is_note = true;
      } else if (STREQ2(id, li, te)) {
        note_no = 10;
        is_note = true;
      } else if (STREQ(id, ti)) {
        note_no = 11;                            
        is_note = true;
      }
      
      if (is_note) {
        if (num_read) {
          // num is thus an octave number
          octave = (int)num;
          error_check(num < 0 || num > 10 || num != octave,
            ncd_parser_line_no,
            "invalid octave no %f, must be integer from 0 to 10", num);
          num_read = false;
        }
        error_check(octave == 10 && note_no > 7, ncd_parser_line_no,
          "MIDI note out of range");
        midi_note = start_note = MIDI_NOTE(octave, note_no);
  
        id_read = (no_notes = false); // there is at least one note in the score
      }
    } else if (!num_read || (num>=0 && num<=9 && num == (int)num)) {
      if (num_read) {
        // num could be a drum effect number
        drum_id[0] = num + '0';
        drum_id[1] = '\0';
        strncat(drum_id, id, MAXIDLEN-2);
        num_read = false;
      } else {
        strcpy(drum_id, id);
      }
      if ((note_no = ncd_midi_drum_no(drum_id))) {
        is_note = true;
        midi_note = note_no;
        // There is at least one note in the score.
        id_read = (no_notes = false);
      }
    }
  }
  
  if (num_read && channel != DRUMCHANNEL
       && (number_separated || c != '/')) {
    // Relative pitch number, relative to last absolute note.
    error_check(start_note + num < 0 || start_note + num > 127,
      ncd_parser_line_no, "MIDI note out of range");
    midi_note = start_note + num;
    is_note = true;
    num_read = (no_notes = false);
    SKIPSEP();      
  }
  
  if (!num_read && isdigit(c)) {
    num_read = true;
    READNUM(f, num);
  }
  
  if (c == '/') { // duration fraction
    NEXTC();
    
    if (isdigit(c)) {
      READNUM(u, denom);
    } else {
      denom = 1;
    }
  
    if (!num_read) {
      num = 1;
    }
  
    duration = num / denom;
    
    if (c == DOT) {
      // https://en.wikipedia.org/wiki/Dotted_note
      dots_power = 1;
      do {
        dots_power *= 2; 
        NEXTC();
      } while (c == DOT);
      duration *= 2 - 1.0 / dots_power;
    }
  
    SKIPSEP();        
  }
  
  if (is_note) { // rests do not have velocity
    if (isdigit(c)) {
      READNUM(hhu, velocity); // numeric velocity spec
    } else {
      if (!id_read && (c == 'm' || c == 'f' || c == 'p')) {
        id[i = 0] = c;
        NEXTC();
        while (i < 3 && (c == 'm' || c == 'f' || c == 'p')) {
          id[++i] = c;
          NEXTC();
        }
        id[++i] = '\0';
        id_read = true;
      }
  
      if (id_read) {
        if (STREQ(id, pppp)) {
          velocity = PPPP;
        } else if (STREQ(id, ppp)) {
          velocity = PPP;
        } else if (STREQ(id, pp)) {
          velocity = PP;            
        } else if (STREQ(id, p)) {
          velocity = P;            
        } else if (STREQ(id, mp)) {
          velocity = MP;            
        } else if (STREQ(id, mf)) {
          velocity = MF;            
        } else if (STREQ(id, f)) {
          velocity = F;            
        } else if (STREQ(id, ff)) {
          velocity = FF;            
        } else if (STREQ(id, fff)) {
          velocity = FFF;            
        } else if (STREQ(id, ffff)) {
          velocity = FFFF;            
        } else {
          trigger_error(ncd_parser_line_no,
            "Unknown velocity nuance %s", id);
        }
      }
    }
  }
  SKIPSEP();
  
  if (is_note) {
    if (tie) {
      error_check(!note_stored, ncd_parser_line_no,
        "Tie without a note on the left-hand side");
      error_check(note.msg[MIDI_DATA1] != midi_note,
        ncd_parser_line_no, "Tied notes must be the same note");
      error_check(note.msg[MIDI_DATA2] != velocity,
        ncd_parser_line_no, "Tied notes must be the same velocity");
      note.duration += duration;
    } else {
      if (note_stored) {
        PUSHNOTE();
      }
  
      /* Save the current note, but do not push it
         it may be followed by a tie!
         We are sure values are in the correct range
         because of previous interactive error checking. */
      note.msg[MIDI_STATUS] = MIDI_NOTEON | channel;
      note.msg[MIDI_DATA1] = midi_note;
      note.msg[MIDI_DATA2] = velocity;
  
      note.duration = duration;
      note.tag = tag;
      note_stored = true;
    }
  } else { // rest
    if (note_stored) {
      PUSHNOTE();
      note_stored = false;
    }
  
    ncd_queue_push_rest(duration);
  }
}

void parse_score_row(FILE *fp) {
  unsigned char percent;
  bool hairpin_type;

  error_check(!isdigit(c), ncd_parser_line_no, "Expected MIDI channel no");
  READCHANNEL(channel);
  SKIPBLANKS();

  error_check(c == BAR, ncd_parser_line_no, "Expected one-character tag, found a bar");
  tag = c;

  ADVANCE();
  if (c == BAR) {
    ADVANCE();
  }
  
  error_check(c == '\n', ncd_parser_line_no,
    "Empty score line, it needs at least one note or rest");

  // read all notes and rests on this line
  new_line();
  note_stored = false;
  do {
    if ((tie = (c == TIE))) {
      ADVANCE();
    }
    
    if ((hairpin_type = (c == CRESCENDO)) || c == DIMINUENDO) {
      NEXTC();
      READNUM(hhu, percent);
      error_check(percent > 127, ncd_parser_line_no,
        "Hairpin percentage must be lower than 127");

      ncd_start_hairpin(hairpin_type, percent, channel, note.duration);
    } else if (c == HAIRPIN_END) {
      NEXTC();
      ncd_stop_hairpin(channel, note.duration);
    } else {
      parse_note(fp);
    }

    SKIPBLANKS();
    if (c == BAR || c == BEAT) {
      ADVANCE();
    }    
  } while (c != '\n');
  if (note_stored) {
    // push last note on line -- ties cannot cross lines
    PUSHNOTE();
  }
  NEXTC();
}

void ncd_parse(FILE *fp) {
  ncd_parser_line_no = 1; // Lines are numbered starting from 1

  NEXTC(); // prime the pump by reading the first character
  while (c != EOF) {
    SKIPBLANKS();
    /* only lines starting with BAR belong to the score, ignore the rest
       (normal text, lyrics, etc). So you can comment a score line by
       prepending it with a non-blank and non-bar char, e.g. # or // */
    if (c != BAR) {
      // line not beginning with a BAR: skip the whole line
      SKIPBLANKS();
      if (c == '\n') {
        // empty line, new polyphonic group begins
        new_group();
      } else {
        do {
          NEXTC();
        } while (c != '\n'); // EOF can only happen after a newline
      }
      NEXTC();
      continue;
    }
    ADVANCE(); // skip BAR and blanks

    if (isalpha(c) || c == QUOTE) {
      parse_directives(fp);
      continue;
    }
    
    parse_score_row(fp);
  }
  if (no_notes) {
    trigger_error(ncd_parser_line_no, "empty score, no notes found");
  }
}
