#ifndef NOCRAZYDOTS_PARSER_H
#define NOCRAZYDOTS_PARSER_H

#include <stdio.h>
#include <strings.h>

#define MAXIDLEN 100
#define STREQ(var, lit) (strcasecmp(var, lit) == 0)
#define STREQ2(var, lit1, lit2) (STREQ(var, lit1) || STREQ(var, lit2))
#define STREQ3(var, lit1, lit2, lit3) (STREQ2(var, lit1, lit2) || STREQ(var, lit3))
#define STREQ4(var, lit1, lit2, lit3, lit4) (STREQ3(var, lit1, lit2, lit3) || STREQ(var, lit4))

// convert from 0..11 octave-relative MIDI note number to name 
extern const char *midi_note_no_name[12];

extern int ncd_parser_line_no;

void ncd_parse(FILE *fp);

#endif
