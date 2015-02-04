
#define DEBUG_off

/*
 *  Copyright (c) 1989 Lars Fredriksen, Bryan So, Barton Miller
 *
 *  This program is distributed in the hope that it will be useful, but
 *  WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 */

/*
 *  Fuzz generator
 *
 *  Usage:  fuzz [option(s)] NUM 
 *
 *  Generate NUM random bytes on stdout
 *
 *  Options:
 *      NUM       length of output in bytes -OR- # of strings when using -l
 *     -0         NULL (0 byte) characters included
 *     -a         all ASCII character (default)
 *     -d delay   Delay for "delay" seconds between characters
 *     -o file    Record characters in "file"
 *     -r file    Replay characters in "file"
 *     -l         random length LF terminated strings (lll max. default 255)
 *     -p         printable ASCII only
 *     -s         use sss as random seed
 *     -e         send "epilog" after all random characters
 *     -x         print the random seed as the first line
 *
 *  Defaults:
 *     fuzz -a 
 *           
 *  Authors:
 *
 *     Lars Fredriksen, Bryan So
 *
 *  Updated by:
 *
 *     Gregory Smethells, Brian Bowers, Karlen Lie
 *
 */

static char *progname = "fuzz";

#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <strings.h>
#include <time.h>
#include <ctype.h>
#include <unistd.h>

#define SWITCH '-'

#define TRUE  1
#define FALSE 0


/* Function Prototypes */
void usage();
void init();
void replay();
void fuzz();
void putch(int i);
void fuzzchar(int m, int h);
void fuzzstr(int m, int h);
void myputs(char *s);
int  oct2dec(int i);

/* Global flags */
int      flag0  = FALSE;
int      flaga  = TRUE;		/* FALSE if flagp */
unsigned flagd  = FALSE;
int      flagl  = FALSE;
int      flags  = FALSE;
int      flage  = FALSE;
int      seed   = FALSE;
int      flagn  = FALSE;
int      flagx  = FALSE;
int      flago  = FALSE;
int      flagr  = FALSE;
int      length = FALSE;
char     epilog[1024];
char    *infile, *outfile;
FILE    *in, *out;


/*
 * Parse any argument flags and create the requested output 
 * file with random bytes
 */
int main(int argc, char **argv) {
  float f;


  /* Parse command line */
  while( *(++argv) != NULL ) {
    if (**argv != SWITCH) {	/* Not a switch, must be a length */
      if( sscanf(*argv, "%d", &length) != 1 ) {
         usage();
      }

      flagn = TRUE;
    } 
    else {                     /* A switch */
      switch ((*argv)[1]) {
        case '0':
          flag0 = TRUE;
          break;

        case 'a':
          flaga = TRUE;
          break;

        case 'd':
          argv++;

          if (sscanf(*argv, "%f", &f) != 1) {
            usage();
          }

          flagd = (unsigned) (f * 1000000.0);
          break;

        case 'o':
          flago = TRUE;
          argv++;
          outfile = *argv;
          break;

        case 'r':
          flagr = TRUE;
          argv++;
          infile = *argv;
          break;

        case 'l':
          flagl = 255;

          if( argv[1] != NULL && argv[1][0] != SWITCH ) {
            argv++;

            if( sscanf(*argv, "%d", &flagl) != 1 || flagl <= 0 ) {
              usage();
            }
          }
          break;

        case 'p':
          flaga = FALSE;
          break;

        case 's':
          argv++;
          flags = TRUE;

          if( sscanf(*argv, "%d", &seed) != 1 ) {
            usage();
          }
          break;

        case 'e':
          argv++;
          flage = TRUE;
 
          if( *argv == NULL ) {
            usage();
          }

          sprintf(epilog, "%s", *argv);
          break;

        case 'x':
          flagx = TRUE;
          break;

        default:
          usage();
      }
    }
  }

  init();

  if( flagr ) {
    replay();
  }
  else {
    fuzz();
  }

  myputs( epilog );

  if( flago ) {
    if( fclose(out) == EOF ) {
      perror(outfile);
      exit(1);
    }
  }

  if( flagr ) {
    if( fclose(in) == EOF ) {
      perror(infile);
      exit(1);
    }
  }

  return( 0 );
}


/* 
 * Print help screen 
 */
void usage() {
  printf("  Usage: \n");
  printf("    fuzz [option(s)] NUM \n\n"); 
  printf("  Generate NUM random bytes on stdout\n\n"); 
  printf("  Options:\n"); 
  printf("      NUM       length of output in bytes -OR- # of strings when using -l\n");
  printf("     -0         include NULL (0 byte) character in output\n"); 
  printf("     -a         use all ASCII characters in output (default)\n"); 
  printf("     -d DELAY   delay for DELAY seconds between characters\n"); 
  printf("     -o FILE    record characters in FILE\n"); 
  printf("     -r FILE    replay characters in FILE\n"); 
  printf("     -l         use random length LF terminated strings (lll max. default 255) \n"); 
  printf("     -p         use only printable ASCII character in output\n"); 
  printf("     -s SEED    force random seed to be SEED\n"); 
  printf("     -e EPILOG  finish random output stream with characters given by EPILOG\n"); 
  printf("     -x         print the random seed as the first line \n\n"); 
  printf("  Defaults: \n"); 
  printf("     fuzz -a\n\n"); 
  printf("  Authors: \n"); 
  printf("     Lars Fredriksen, Bryan So \n\n"); 
  printf("  Updated by: \n"); 
  printf("     Gregory Smethells, Brian Bowers, Karlen Lie \n\n"); 

  exit(1);
}


/*
 * Initialize random number generator and others 
 */
void init() {
  long now;


  /* Init random numbers */
  if( !flags ) {
    seed = (int)(time(&now) % 37);
  }

  srand(seed);

  /* Random length if necessary */
  if( !flagn ) {
    length = rand() % 100000;
  }

  /* Open data files if necessary */
  if( flago ) {
    if ((out = fopen(outfile, "wb")) == NULL) {
      perror(outfile);
      exit(1);
    }
  }

  if( flagr ) {
    if( (in = fopen(infile, "rb")) == NULL ) {
      perror(infile);
      exit(1);
    }
  } 
  else if( flagx ) {
    printf("%d\n", seed);

    if( fflush(stdout) == EOF ) {
      perror(progname);
      exit(1);
    }

    if( flago ) {
      fprintf(out, "%d\n", seed);

      if( fflush(out) == EOF ) {
        perror(outfile);
        exit(1);
      }
    }
  }
}


/*
 * Replay characters in "in" 
 */
void replay() {
  int c;


  while(  (c = getc(in)) != EOF  ) {
    putch(c);
  }
}


/*
 * Decide the effective range of the random characters 
 * Every random character is of the form c = rand() % m + h 
 */
void fuzz() {
  int h = 1;
  int m = 255;              /* Defaults, 1-255 */


  if( flag0 ) {
    h = 0;
    m = 256;               /* All ASCII, including 0, 0-255 */
  }

  if( !flaga ) {
    h = 32;
    m = 95 + (flag0 != FALSE); /* Printables, 32-126 */
  }

  if( flagl ) {
    fuzzstr(  m, h );
  }
  else {
    fuzzchar( m, h );
  }
}


/*
 * Output a character to standard out with delay 
 */
void putch( int i ) {
  char c;


  c = (char) i;

  if( write(1, &c, 1) != 1 ) {
    perror(progname);

    if( flagr ) {
      (void)fclose( in );
    }

    if( flago ) {
      (void)fclose( out );
    }

    exit(1);
  }

  if( flago ) {
    if( write(fileno(out), &c, 1) != 1 ) {
      perror(outfile);
      exit(1);
    }
  }

  if( flagd ) {
    usleep( flagd );
  }
}


/*
 * Make a random character 
 */
void fuzzchar( int m, int h ) {
  int i, c;


  for( i = 0 ; i < length ; i++ ) {
    c = (int) (rand() % m) + h;

    if( flag0 && !flaga && c == 127 ) {
      c = 0;
    }

    putch( c );
  }
}


/*
 * make random strings 
 */
void fuzzstr( int m, int h ) {
  int i, j, l, c;


  for( i = 0 ; i < length ; i++ ) {
    l = rand() % flagl;	/* Line length  */

    for( j = 0 ; j < l ; j++ ) {
      c = (int) (rand() % m) + h;
 
      if( flag0 && !flaga && c == 127 ) {
        c = 0;
      }

      putch( c );
    }

    putch( '\n' );
  }
}


/*
 * Output the "epilog" with C escape sequences 
 */
void myputs( char *s ) {
  int c;


  while( *s != 0 ) {
    if( *s == '\\' ) {
      switch( *(++s) ) {
        case 'b':
          putch('\b');
          break;

        case 'f':
          putch('\f');
          break;

        case 'n':
          putch('\n');
          break;

        case 'r':
          putch('\r');
          break;

        case 't':
          putch('\r');
          break;

        case 'v':
          putch('\v');
          break;

        case 'x':
          s++;
          (void)sscanf( s, "%2x", &c );
          putch(c);
          s++;
          break;

        default:
          if( isdigit((int)*s) ) {
            (void) sscanf(s, "%3d", &c);
            putch(oct2dec(c));

            for( c = 0 ; c < 3 && isdigit((int)*s) ; c++ ) {
              s++;
            }

            s--;
          } 
          else {
            putch( *s );
          }
      }
    } 
    else {
      putch( *s );
    }

    s++;
  }
}


/*
 * Octal to Decimal conversion, required by myputs() 
 */
int oct2dec( int i ) {
  char s[8];
  int  r = 0;


  sprintf(s, "%d", i);

  for( i = 0 ; i < strlen(s) ; i++ ) {
    r = r * 8 + (s[i] - '0');
  }

  return( r );
}

