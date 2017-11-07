#ifndef NOCRAZYDOTS_MIDI_H
#define NOCRAZYDOTS_MIDI_H

#include <stdbool.h>
#include <alsa/asoundlib.h>

// defaut octave (from 0 to 10, 5 is middle)
#define DEFOCTAVE 5

// velocity values
#define PPPP 8
#define PPP 20
#define PP 31
#define P 42
#define MP 53
#define MF 64
#define F 80
#define FF 96
#define FFF 112
#define FFFF 127

#define DEFVELOCITY MP
#define DEFDURATION 0.25 // 0.25 = quarter note

#define MIDI_CHANNELS 16
#define MIDI_NOTE(octave, note_no) ((octave) * 12 + (note_no))
#define MIDI_OCTAVE(midi_note) ((midi_note) / 12)
#define MIDI_NOTE_NO(midi_note) ((midi_note) % 12)

#define DEVMAXLEN 32 // including null-byte at the end
extern char ncd_midi_port_name[DEVMAXLEN];

typedef struct {
  // Reference volume level for percentage (as set by last directive)
  unsigned char reference;

  // Current volume level. Float so to not amplify rounding errors
  // during hairpins.
  float current;

  // How much to lower/bump up volume every EXPR_STEP ms
  // negative values for decrescendo
  float volume_step;

  float left_duration; // Hairpin duration left so far
} ncd_volume;
// State of hairpin for each channel.
extern ncd_volume ncd_expression[MIDI_CHANNELS];

extern int ncd_midi_err_code;
extern snd_rawmidi_t *midiin, *midiout;
extern char *midi_drum_name[128];
#define CHK(stmt) if ((ncd_midi_err_code = (stmt)) < 0) { \
  trigger_error(0, "(MIDI) %s", snd_strerror(ncd_midi_err_code)); \
}

// Channel used to transmit the playback of drum instruments.
#define DRUMCHANNEL 9 // 9 is 10 as specified by user

// Default location for keyboard voice list and drumkit defs
#define MIDIDATADIR "/usr/share/nocrazydots/data/" // must include the file separator
// Name of the main voice list file in the MIDIDATADIR
#define VOICEFILE "voices.txt"

// MIDI event types
#define MIDI_NOTEON 0x90
#define MIDI_NOTEOFF 0x80
#define MIDI_META 0xFF
#define MIDI_SET_TEMPO 0x51
#define MIDI_CONTROLLER 0xB0
#define MIDI_VOLUME 0x07
#define MIDI_EXPRESSION_MSB 0x0B
#define MIDI_EXPRESSION_LSB 0x2B

/* [0]: high nibble: event type (NOTEON, NOTEOFF, etc.); low nibble: channel
   [1]: data byte 1 (es. pitch)
   [2]: data byte 2 (es. velocity)   
   We do not need other data bytes for now. But since most machines
   handle efficiently 32-bits (or even 64) in both memory and registers,
   we could have one more to make 4 * 8-bits=32-bits */
typedef unsigned char ncd_midi_event[3];

// MIDI event fields
enum {MIDI_STATUS, MIDI_DATA1, MIDI_DATA2, MIDI_DATA3};

extern struct hsearch_data ncd_midi_voice_table, ncd_midi_drum_table;

void ncd_midi_init();
void ncd_midi_load_voices(char *datadir);
void ncd_midi_load_drumkit(char *name);
void ncd_midi_set_tempo(unsigned char bpm);
void ncd_midi_set_voice(char *voice, unsigned char channel,
  unsigned char volume, bool queue);
int ncd_midi_event_size(ncd_midi_event e);

#ifdef DEBUG
  #define NCD_MIDI_EVENT(e) { \
    printf("-> %02hhx %02hhx %02hhx\t", \
      (e)[MIDI_STATUS], (e)[MIDI_DATA1], (e)[MIDI_DATA2]); \
    if (((e)[MIDI_STATUS] & 0xF0) == MIDI_CONTROLLER \
        && (e)[MIDI_DATA1] == MIDI_VOLUME) { \
      printf("ch   %4hhu  vol %3hhu\n", \
        ((e)[MIDI_STATUS] & 0x0F) + 1, (e)[MIDI_DATA2] & 0x7F); \
    } else { \
      printf("note %2hhu%s  vel %3hhu\n", \
            MIDI_OCTAVE((e)[MIDI_DATA1]), \
            midi_note_no_name[MIDI_NOTE_NO((e)[MIDI_DATA1])], \
            (e)[MIDI_DATA2]); \
    } \
    CHK(snd_rawmidi_write(midiout, (e), ncd_midi_event_size(e))); \
  }
#else
  #define NCD_MIDI_EVENT(e) CHK(snd_rawmidi_write(midiout, (e), ncd_midi_event_size(e)))
#endif

void ncd_midi_noteon(unsigned char note, unsigned char velocity,
  unsigned char channel);
void ncd_midi_noteoff(unsigned char note, unsigned char channel);
void ncd_midi_set_volume(float volume, unsigned char channel);
void ncd_midi_expression(unsigned char volume, unsigned char channel);
void ncd_midi_expression_fine(unsigned short volume, unsigned char channel);
unsigned char ncd_midi_drum_no(char *effect_acronym);
void ncd_midi_dump();
bool ncd_midi_same_event(ncd_midi_event e1, ncd_midi_event e2);
void ncd_midi_all_notes_off();
ncd_midi_event *ncd_midi_wait_note();
void ncd_midi_detect_keyboard_device();

#endif
