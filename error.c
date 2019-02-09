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
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
extern char *ncd_pname;

void error_if(int cond) {
  if (cond) {
    perror(ncd_pname);
    exit(EXIT_FAILURE);
  }
}

void warning(int line_no, char *msg, ...) {
  va_list ap;
  va_start(ap, msg);
  if (line_no) {
    fprintf(stderr, "%s: line %d: ", ncd_pname, line_no);
  } else {
    fprintf(stderr, "%s: ", ncd_pname);
  }
  vfprintf(stderr, msg, ap);
  fputs(".\n", stderr);
  va_end(ap);
}

void error_check(int cond, int line_no, char *msg, ...) {
  if (cond) {
    va_list ap;
    va_start(ap, msg);
    warning(line_no, msg, ap);
    //va_end(ap);
    exit(EXIT_FAILURE);
  }
}
