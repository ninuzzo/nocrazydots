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

/*
Ref.
https://ccrma.stanford.edu/~craig/articles/linuxmidi/alsa-1.0/
http://www.alsa-project.org/alsa-doc/alsa-lib/_2test_2rawmidi_8c-example.html
*/

/*
 Feature-test macros such as _GNU_SOURCE must go at the top of the file,
 before any #include directives, to ensure that the preprocessor sees them
 before it sees any code that uses them. That generally means they have
 to be processed before any system headers. */

#define _GNU_SOURCE // must go before 
#include <stdlib.h>
#include <signal.h>
#include <search.h>
#include <string.h>
#include <ctype.h>
#include "midi.h"
#include "parser.h"
#include "queue.h"
#include "error.h"

/* https://en.wikipedia.org/wiki/MIDI_beat_clock */
#define MIDI_REAL_TIME_CLOCK 0xF8
/* Active Sensing. This message is intended to be sent repeatedly to
   tell the receiver that a connection is alive. Use of this message
   is optional. When initially received, the receiver will expect to
   receive another Active Sensing message each 300ms (max), and if it
   does not then it will assume that the connection has been
   terminated. At termination, the receiver will turn off all voices and
   return to normal (non- active sensing) operation.
*/
#define MIDI_SENSING 0xFE
#define MIDI_PROGRAM_CHANGE 0xC0
#define MIDI_ALL_NOTES_OFF 0x7B
#define MIDI_SNDBANK_MSB 0x00
#define MIDI_SNDBANK_LSB 0x20

#define MAXVOICELEN 50
#define MAXVOICES 1024
#define MAXLINELEN 128

#define REMCHAR '#'

#define MAXPATHLEN 256
#define DRUMFILEEXT ".txt"

#define DEFVOLUME 100 // default MIDI volume [0..127]

extern char *ncd_pname;

char ncd_midi_port_name[DEVMAXLEN] = "";

char *midi_drum_name[128];

// float and not unsigned char to compensate rounding errors
// during crescendo and diminuendo or slides
ncd_volume ncd_expression[MIDI_CHANNELS];
ncd_pitch ncd_pitch_wheel[MIDI_CHANNELS];

snd_rawmidi_t *midiin = NULL, // structure to access MIDI input
  *midiout = NULL; // structure to access MIDI output
int ncd_midi_err_code;

struct voice_data {
  unsigned char msb;  // Bank select msb
  unsigned char lsb;  // Bank select lsb
  unsigned char mpcn; // MIDI program change number
};

struct hsearch_data ncd_midi_voice_table, ncd_midi_drum_table;
bool ncd_midi_drumkit_not_loaded = true;

// Beware, this changes s (side effect).
char *lowercase(char *s) {
 char *p;
 for (p = s; *p; p++) *p = tolower(*p);
 return s;
}

void ncd_midi_load_voices(char *datadir) {
  char line[MAXLINELEN];
  char *tok;
  struct voice_data *vd;
  FILE *fp;
  ENTRY e, *ep;
  char voicefile[MAXPATHLEN];
  int max_datadir_len;

  max_datadir_len = MAXPATHLEN - strlen(VOICEFILE) - 1; // -1 for the ending NULL
  error_check(strlen(datadir) > max_datadir_len, 0,
    "Data dir name `%s' too long. Bump up MAXPATHLEN in source code\n",
    datadir);
  strcat(strcpy(voicefile, datadir), VOICEFILE);

  error_if((fp = fopen(voicefile, "r")) == NULL);
  error_if(hcreate_r(MAXVOICES, &ncd_midi_voice_table) == 0);  
  while (fgets(line, MAXLINELEN, fp)) {
    if (line[0] == REMCHAR) {
      continue;
    }
    error_if((tok = strtok(line, ",")) == NULL);
    e.key = lowercase(strdup(tok));
    
    vd = malloc(sizeof *vd);
    error_if((tok = strtok(NULL, ",")) == NULL);
    vd->msb = atoi(tok);
    error_if((tok = strtok(NULL, ",")) == NULL);
    vd->lsb = atoi(tok);
    error_if((tok = strtok(NULL, ",")) == NULL);
    vd->mpcn = atoi(tok) - 1;
    e.data = vd;
 
    error_if(hsearch_r(e, ENTER, &ep, &ncd_midi_voice_table) == 0);
  }

  fclose(fp);
}

void ncd_midi_load_drumkit(char *name) {
  char line[MAXLINELEN];
  char *tok;
  FILE *fp;
  ENTRY e, *ep;
  char drumfile[MAXPATHLEN];
  size_t max_file_name_len;
  long note_no;

  strncpy(drumfile, MIDIDATADIR, // -1 for the ending NULL
    (max_file_name_len = MAXPATHLEN - strlen(MIDIDATADIR) - strlen(DRUMFILEEXT) - 1));
  error_check(strlen(name) > max_file_name_len, ncd_parser_line_no,
    "File name `%s' too long. Bump up MAXPATHLEN in source code\n",
    name);
  strcat(strcat(drumfile, name), DRUMFILEEXT);
  
  error_if((fp = fopen(drumfile, "r")) == NULL);
  error_if(hcreate_r(MAXVOICES, &ncd_midi_drum_table) == 0);  
  while (fgets(line, MAXLINELEN, fp)) {
    if (line[0] == REMCHAR) {
      continue;
    }
    error_if(strtok(line, ",") == NULL); // ignore Effect name
    error_if((tok = strtok(NULL, ",")) == NULL); 
    e.key = lowercase(strdup(tok));
    error_if((tok = strtok(NULL, ",")) == NULL);
    note_no = atol(tok);
    error_check(note_no < 0 || note_no > 128, ncd_parser_line_no,
      "Drum effect number %ld out of range", note_no);
    e.data = (void *)note_no;

    midi_drum_name[(unsigned char)note_no] = e.key; 
    error_if(hsearch_r(e, ENTER, &ep, &ncd_midi_drum_table) == 0);
  }

  fclose(fp);  
}

void ncd_midi_set_tempo(unsigned char bpm) {
  ncd_event ev;
 
  // Queue up a MIDI tempo change event
  ev.tag = ' ';
  ev.duration = 0;
  ev.msg[MIDI_STATUS] = MIDI_META;
  ev.msg[MIDI_DATA1] = MIDI_SET_TEMPO;
  /* NON-STANDARD, instead a byte stating the number of bytes
     and then the data bytes stating the number of milliseconds per
     quarter beat in variable-length format, we just have one byte
     in normal format expressing the bpm value directly, but it does
     not matter, since nocrazydots interprets this kind of message
     and does not send it out on the MIDI wire. See:
     http://www.fileformat.info/format/midi/corion.htm */
  ev.msg[MIDI_DATA2] = bpm;
  ncd_queue_push_event(ev);
}

// Return 0 if not found
unsigned char ncd_midi_drum_no(char *effect_acronym) {
  ENTRY e, *ep;
  
  e.key = lowercase(effect_acronym);

  return hsearch_r(e, FIND, &ep, &ncd_midi_drum_table) == 0 ? 0
    : (unsigned char)(long)(ep->data);
}
  
void ncd_midi_set_voice(const char *voice, unsigned char channel,
  unsigned char volume, bool queue) {
  ENTRY e, *ep;
  struct voice_data *vd;
  ncd_event ev;
  // Needed because we cannot modify constant strings.
  static char lv[MAXIDLEN];

  e.key = lowercase(strncpy(lv, voice, MAXIDLEN - 1));
  error_check(hsearch_r(e, FIND, &ep, &ncd_midi_voice_table) == 0,
    ncd_parser_line_no, "Unexistant or incorrect voice name %s", voice);
  
  vd = (struct voice_data*)(ep->data);
  channel = channel & 0xF;

  ev.tag = ' ';
  ev.duration = 0;
  
  ev.msg[MIDI_STATUS] = MIDI_CONTROLLER | channel;
  ev.msg[MIDI_DATA1] = MIDI_VOLUME; // Volume level of the instrument
  ncd_expression[channel].reference = ncd_expression[channel].current
    = ev.msg[MIDI_DATA2] = volume & 0x7F;
  if (queue) {
    ncd_queue_push_event(ev);
  } else {
    NCD_MIDI_EVENT(ev.msg);
  }
  
  /* In plain MIDI we could use MIDI RUNNING STATUS to spare one byte
     but the array of events starting at a certain time has all
     elements of the same size. */
  ev.msg[MIDI_DATA1] = MIDI_SNDBANK_MSB; // Sound bank selection (MSB).
  ev.msg[MIDI_DATA2] = vd->msb;
  if (queue) {
    ncd_queue_push_event(ev);
  } else {
    NCD_MIDI_EVENT(ev.msg);
  }
  
  // MIDI_STATUS is the same
  ev.msg[MIDI_DATA1] = MIDI_SNDBANK_LSB; // LSB sound bank selection.
  ev.msg[MIDI_DATA2] = vd->lsb;
  if (queue) {
    ncd_queue_push_event(ev);
  } else {
    NCD_MIDI_EVENT(ev.msg);
  }
  
  // Sound selection in the current sound bank.
  ev.msg[MIDI_STATUS] = MIDI_PROGRAM_CHANGE | channel;
  ev.msg[MIDI_DATA1] = vd->mpcn;
  if (queue) {
    ncd_queue_push_event(ev);
  } else {
    NCD_MIDI_EVENT(ev.msg);
  }
  
  // We assume you can only use one drumkit per score
  // and so we load it only once before playing
  if (ncd_midi_drumkit_not_loaded && queue && (channel == DRUMCHANNEL)) {
    ncd_midi_load_drumkit(lv);
    ncd_midi_drumkit_not_loaded = false;
  }
}

void INThandler(int sig) {
  ncd_midi_all_notes_off();
  snd_rawmidi_close(midiin);
  snd_rawmidi_close(midiout);
  midiin  = NULL;    // snd_rawmidi_close() does not clear invalid pointer,
  midiout = NULL;    // so might be a good idea to erase it after closing.
  exit(0); 
}

void ncd_midi_init(char* portname) {
  register int channel;

  if (! (*ncd_midi_port_name)) {
    ncd_midi_detect_keyboard_device();
  }

  CHK(snd_rawmidi_open(&midiin, &midiout, ncd_midi_port_name, SND_RAWMIDI_SYNC));
  signal(SIGINT, INThandler);

  for (channel = 0; channel < MIDI_CHANNELS; channel++) {
    ncd_midi_set_volume(ncd_expression[channel].reference = DEFVOLUME, channel);
    ncd_midi_pitch_wheel(ncd_pitch_wheel[channel].current = NOBENDING, channel);

    // TODO: 2 must be changed to 24 to allow more than one tone of pitch bending
    ncd_pitch_bend_sensitivity(2, channel);
  }
}

// This implementation handles correctly only the types of messages generated
// Note: the number of bytes returned must include the status byte
int ncd_midi_event_size(ncd_midi_event e) {
  register unsigned char status = e[MIDI_STATUS];
  if (status == MIDI_META) {
     if (e[MIDI_DATA1] == MIDI_SET_TEMPO) {
       return 3; // Warning: non-standard, but works for us.
     }
  } else {
    switch (status & 0xF0) {
      case MIDI_NOTEON:
      case MIDI_NOTEOFF:
      case MIDI_CONTROLLER:
      case MIDI_PITCH_WHEEL:
        return 3;
      break;
      
      case MIDI_PROGRAM_CHANGE:
        return 2;
      break;
    }
  }

  trigger_error(0, "(MIDI) unknown number of args for message status %02hhx",
    status);
    
  return 0; // should never reach here
}

void ncd_midi_noteon(unsigned char note, unsigned char velocity, unsigned char channel) {
  ncd_midi_event e;

  e[MIDI_STATUS] = MIDI_NOTEON | (channel & 0xF);
  e[MIDI_DATA1] = note & 0x7F; // pitch value
  e[MIDI_DATA2] = velocity & 0x7F; // how strong to play the key

  NCD_MIDI_EVENT(e);
}

void ncd_midi_noteoff(unsigned char note, unsigned char channel) {
  ncd_midi_event e;

  e[MIDI_STATUS] = MIDI_NOTEOFF | (channel & 0xF);
  e[MIDI_DATA1] = note & 0x7F; // pitch value
  e[MIDI_DATA2] = 0x00; // release velocity is not used, set it to zero

  NCD_MIDI_EVENT(e);
}

// To be used to set the midi channel volume level
void ncd_midi_set_volume(unsigned char volume, unsigned char channel) {
  ncd_midi_event e;

  e[MIDI_STATUS] = MIDI_CONTROLLER | channel;
  e[MIDI_DATA1] = MIDI_VOLUME; // Volume level of the instrument
  e[MIDI_DATA2] = volume & 0x7F;
  ncd_expression[channel].current = volume;
  NCD_MIDI_EVENT(e);
}

// To be used for crescendos and decrescendos during the performance
// Currently unsupported by my keyboard
void ncd_midi_expression_fine(unsigned short volume, unsigned char channel) {
  ncd_midi_event e;

  e[MIDI_STATUS] = MIDI_CONTROLLER | channel;
  e[MIDI_DATA1] = MIDI_EXPRESSION_MSB; // Volume level of the instrument
  e[MIDI_DATA2] = volume & 0x7F;

  NCD_MIDI_EVENT(e);

  // MIDI_STATUS is the same
  e[MIDI_DATA1] = MIDI_EXPRESSION_LSB; // Volume level of the instrument
  e[MIDI_DATA2] = (volume >> 7) & 0x7F;

  NCD_MIDI_EVENT(e);
}

// See: https://www.recordingblogs.com/wiki/midi-registered-parameter-number-rpn
void ncd_midi_start_rpn(unsigned char rpn1, unsigned char rpn2, unsigned char channel) {
  ncd_midi_event e;

  e[MIDI_STATUS] = MIDI_CONTROLLER | channel;
  e[MIDI_DATA1] = 0x64; // 100
  e[MIDI_DATA2] = rpn1;

  NCD_MIDI_EVENT(e);

  // MIDI_STATUS is the same
  e[MIDI_DATA1] = 0x65; // 101
  e[MIDI_DATA2] = rpn2;

  NCD_MIDI_EVENT(e);
}

void ncd_midi_stop_rpn(unsigned char channel) {
  ncd_midi_start_rpn(0x7F, 0x7F, channel);
}

void ncd_pitch_bend_sensitivity(unsigned char semitones, unsigned char channel) {
  ncd_midi_event e;

  // (0,0) is the RPN for pitch bend sensitivity
  ncd_midi_start_rpn(0x00, 0x00, channel);

  e[MIDI_STATUS] = MIDI_CONTROLLER | channel;
  e[MIDI_DATA1] = 0x06;
  e[MIDI_DATA2] = semitones;

  NCD_MIDI_EVENT(e);

  // MIDI_STATUS is the same
  e[MIDI_DATA1] = 0x26;
  e[MIDI_DATA2] = 0x00; // cents (fine value, unsupported by this function)

  // Optional but good practice in order
  // to make sure we get out of controller mode
  ncd_midi_stop_rpn(channel);
}

void ncd_midi_expression(unsigned char volume, unsigned char channel) {
  ncd_midi_event e;

  e[MIDI_STATUS] = MIDI_CONTROLLER | channel;
  e[MIDI_DATA1] = MIDI_EXPRESSION_MSB; // Volume level of the instrument
  e[MIDI_DATA2] = volume & 0x7F;

  NCD_MIDI_EVENT(e);
}

// See: http://midi.teragonaudio.com/tech/midispec/wheel.htm
void ncd_midi_pitch_wheel(unsigned short value, unsigned char channel) {
  ncd_midi_event e;

  e[MIDI_STATUS] = MIDI_PITCH_WHEEL | channel;
  e[MIDI_DATA1] = value & 0x7F;
  e[MIDI_DATA2] = (value >> 7) & 0x7F;
  ncd_pitch_wheel[channel].current = value;

  NCD_MIDI_EVENT(e);
}

// Useful for debugging, e.g. to discover velocities of what you play
void ncd_midi_dump() {
  unsigned char byte;
  while (1) {
    CHK(snd_rawmidi_read(midiin, &byte, 1));
    if (byte != MIDI_REAL_TIME_CLOCK && byte != MIDI_SENSING) {
      printf("%02x ", byte);
      fflush(stdout);
    }
  }
}
 
ncd_midi_event *ncd_midi_wait_note() {
  static ncd_midi_event e;
  unsigned char status;
  while (1) {
    CHK(snd_rawmidi_read(midiin, &e[MIDI_STATUS], 1));

    status = e[MIDI_STATUS] & 0xF0;
    if (status == MIDI_NOTEON || status == MIDI_NOTEOFF) {
      CHK(snd_rawmidi_read(midiin, &e[MIDI_DATA1], 1));
      CHK(snd_rawmidi_read(midiin, &e[MIDI_DATA2], 1));

      // DEBUG
      /*printf("<- %02hhx %hhu%s %02hhx ", e[MIDI_STATUS],
        MIDI_OCTAVE(e[MIDI_DATA1]),
        midi_note_no_name[MIDI_NOTE_NO(e[MIDI_DATA1])],
        e[MIDI_DATA2]);*/

      return &e;
    }
  }
}

bool ncd_midi_same_event(ncd_midi_event e1, ncd_midi_event e2) {
  // Ignore channel number the note arrives from.
  unsigned char status1 = e1[MIDI_STATUS] & 0xF0,
    status2 = e2[MIDI_STATUS] & 0xF0;
  
  return e1[MIDI_DATA1] == e2[MIDI_DATA1]

    && ((e1[MIDI_DATA2] > 0 && e2[MIDI_DATA2] > 0) || (e1[MIDI_DATA2] == e2[MIDI_DATA2]))

    && (status1 == status2
         // Some keyboards send MIDI_NOTEOFF messages as MIDI_NOTEON with 0 velocity.
         || (status1 == MIDI_NOTEOFF && status2 == MIDI_NOTEON && e2[MIDI_DATA2] == 0)
         || (status2 == MIDI_NOTEOFF && status1 == MIDI_NOTEON && e1[MIDI_DATA2] == 0));
}

// useful to silence stuck notes
void ncd_midi_all_notes_off() {
  unsigned char channel;
  ncd_midi_event e;
	
  for (channel = 0; channel < MIDI_CHANNELS; channel++) {
    e[MIDI_STATUS] = MIDI_CONTROLLER | channel;
    e[MIDI_DATA1] = MIDI_ALL_NOTES_OFF;
    e[MIDI_DATA2] = 0x00;
    
    NCD_MIDI_EVENT(e);
  }
}

void ncd_midi_detect_keyboard_device() {
  snd_ctl_t *ctl;
  int card = -1, // get the first card in the list of sound cards
    dev;
  snd_rawmidi_info_t *info;
  char devname[32];
  const char *name;

  CHK(snd_card_next(&card));
  error_check(card < 0, 0, "no sound cards or other MIDI gear found");

  snd_rawmidi_info_alloca(&info);

  // for each sound card
  do {
    // find out if this card has a MIDI device
    // that can handle both input and output
    snprintf(devname, DEVMAXLEN, "hw:%d", card);
    CHK(snd_ctl_open(&ctl, devname, 0));
    dev = -1; // get the first device number for this card
    // for all devices on this card
    do {
      CHK(snd_ctl_rawmidi_next_device(ctl, &dev));
      if (dev >= 0) {
	    snd_ctl_rawmidi_info(ctl, info);
        name = snd_rawmidi_info_get_name(info);
        if (strcasestr(name, "keyboard")) {
          snprintf(ncd_midi_port_name, DEVMAXLEN, "hw:%d,%d,0", card, dev);
          return;
        }
      } else {
        break;
      }
    } while (1);
    snd_ctl_close(ctl);
    
    CHK(snd_card_next(&card));
  } while (card >= 0);

  trigger_error(0, "No MIDI keyboard detected. Try to specify a device name");
}
