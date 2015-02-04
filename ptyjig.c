
#define DEBUG_off

#define LINUX
#define SOLARIS_off


/*    
 *  Copyright (c) 1989 Lars Fredriksen, Bryan So, Barton Miller
 *
 *  This program is distributed in the hope that it will be useful, but
 *  WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 */


/*
 *  ptyjig -- Super pipe for piping output to Unix utilities.
 *
 *  ptyjig [option(s)] cmd [args]
 *
 *  Run Unix command "cmd" with arguments "args" in background, piping
 *  standard input to "cmd" as its input and prints out "cmd"'s output
 *  to stdout.  This program sets up pseudo-terminal pairs, so that 
 *  it can be used to pipe input to programs that read directly from
 *  tty.
 *
 *  -e suppresses sending of EOF character after stdin exhausted
 *  -s suppresses interrupts.
 *  -x suppresses the standard output.
 *  -i specifies a file to which the standard input is saved.
 *  -o specifies a file to which the standard output is saved.
 *  -d specifies a keystroke delay in seconds (floating point accepted.)
 *  -t specifies a timeout interval.  The program will exit if the
 *     standard input is exhausted and "cmd" does not send output
 *     for "ttt" seconds.
 *  -w specifies another delay parameter. The program starts to send
 *     input to "cmd" after "www" seconds.
 *
 *  Defaults:
 *             -i /dev/nul -o /dev/nul -d 0 -t 2
 *
 *  Examples:
 *         
 *     pty -o out -d 0.2 -t 10 vi text1 < text2
 *
 *        Starts "vi text1" in background, typing the characters in 
 *        "text2" into it with a delay of 0.2sec between each char-
 *        acter, and save the output by "vi" to "out".  The program
 *        ends when "vi" stops outputting for 10 seconds.
 *
 *     pty -i in -o out csh
 *
 *        Behaves like "script out" except the keystrokes typed by
 *        a user are also saved into "in".
 *
 *  Authors:
 *
 *     Bryan So, Lars Fredriksen
 *
 *  Updated by:
 *
 *     Gregory Smethells, Brian Bowers, Karlen Lie
 *
 */


#include <sys/types.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <signal.h>
#include <stdio.h>
#include <strings.h>


#ifdef SOLARIS
#include <sys/ttold.h>
#endif

#ifdef LINUX
#include <sys/termios.h>
#endif

#include <fcntl.h>
#include <unistd.h>
#include <sgtty.h>

#define ECHO      0000010
#define CHILD     0
#define RAW       040
#define CRMOD     020
#define O_CBREAK  0x00000002
#define CBREAK    O_CBREAK

#define TRUE  1
#define FALSE 0


/* GLOBAL VARIABLES */

#ifdef SOLARIS
union wait {
  int w_status;
  int w_stopval;
  int w_termsig;
  int w_stopsig;
  int w_coredump;
  int w_retcode;
};
#endif

char     flage = TRUE;
int      flags = FALSE;
int      flagx = FALSE;
int      flagi = FALSE;
int      flago = FALSE;
unsigned flagt = 2 * 1000000;   /* Timeout interval in useconds */
unsigned flagw = FALSE;         /* Starting wait in useconds */
unsigned flagd = FALSE;         /* Delay between keystrokes in useconds */

char* namei;
char* nameo;
FILE* filei;
FILE* fileo;

/* pids for the reader, writer and exec */
int readerPID = -1;
int writerPID = -1;
int execPID   = -1; 

/* tty and pty file descriptors */
int   tty = -1;
int   pty = -1; 

char  ttyNameUsed[40];
char* progname;

/* for more verbose output at the end of execution */
/* makes reason for dying more obvious */
struct  mesg {
  char    *iname;
  char    *pname;
} mesg[] = {
  0,      0,
  "HUP",  "Hangup",
  "INT",  "Interrupt",
  "QUIT", "Quit",
  "ILL",  "Illegal instruction",
  "TRAP", "Trace/BPT trap",
  "IOT",  "IOT trap",
  "EMT",  "EMT trap",
  "FPE",  "Floating exception",
  "KILL", "Killed",
  "BUS",  "Bus error",
  "SEGV", "Segmentation fault",
  "SYS",  "Bad system call",
  "PIPE", "Broken pipe",
  "ALRM", "Alarm clock",
  "TERM", "Terminated",
  "URG",  "Urgent I/O condition",
  "STOP", "Stopped (signal)",
  "TSTP", "Stopped",
  "CONT", "Continued",
  "CHLD", "Child exited",
  "TTIN", "Stopped (tty input)",
  "TTOU", "Stopped (tty output)",
  "IO",   "I/O possible",
  "XCPU", "Cputime limit exceeded",
  "XFSZ", "Filesize limit exceeded",
  "VTALRM","Virtual timer expired",
  "PROF", "Profiling timer expired",
  "WINCH","Window size changed",
  0,      "Signal 29",
  "USR1", "User defined signal 1",
  "USR2", "User defined signal 2",
  0,      "Signal 32"
};


/* A flag indicating status of the writer */
int writing = TRUE;

/* A flag indicating status of the executing program */
int executing = TRUE;

#ifdef SOLARIS
struct sgttyb oldsb; /* terminal descriptors */
struct sgttyb gb;
struct tchars gtc;
struct ltchars glc;
struct winsize gwin;
int    glb, gl;
#endif






/*
 * Unfix the above and exit when we are done. 
 */
void done() {
#ifdef DEBUG
  fprintf( stderr, "ptyjig: in done()\n" );
#endif

  /* Close output files if opened */
  if( flagi ) {
    fclose( filei );
    flagi = FALSE;
  }

  if( flago ) {
    fclose( fileo );
    flago = FALSE;
  }

#ifdef SOLARIS
  /* Reset the ECHO: else shell doesn't echo any typed chars */
  ioctl(0, TIOCSETP, (char*)&oldsb);
#endif
}



/*
 * Signal handler for SIGCHLD
 */
void sigchld(int sig) {
  int pid;
  union wait status;


#ifdef DEBUG
  fprintf( stderr, "ptyjig: got signal SIGCHLD\n" );
#endif

  /* Guarantee to return since a child is dead */
  pid = wait3( (int*)&status, WUNTRACED, 0 );  

  if( pid ) {
#ifdef DEBUG
    printf("ptyjig: pid = %d\n",pid);
    printf("ptyjig: status = %d %d %d %d %d\n",
            status.w_termsig,status.w_coredump,status.w_retcode,
            status.w_stopval,status.w_stopsig);
#endif

    if( status.w_stopsig == SIGTSTP ) {
      kill( pid, SIGCONT );
    }
    else {
      signal( SIGINT,   SIG_DFL );
      signal( SIGQUIT,  SIG_DFL ); 
      signal( SIGTERM,  SIG_DFL ); 
      signal( SIGWINCH, SIG_DFL ); 
      signal( SIGCHLD,  SIG_IGN );

      done();

      if( pid != execPID ) {
#ifdef DEBUG
        fprintf( stderr, "ptyjig: somebody killed my child\n" );
        fprintf( stderr, "ptyjig: killing execPID = %d\n", execPID );
#endif

        kill( execPID, SIGKILL);          /* kill the exec too */
        kill( readerPID, status.w_termsig); /* use the same method to suicide */
      }


      kill( execPID, SIGKILL ); /* Just to make sure it is killed */

      if( pid != writerPID && writerPID != -1 ) {
#ifdef DEBUG
        printf( "ptyjig: killing writerPID = %d\n", writerPID );
#endif
        kill( writerPID, SIGKILL );
      }

      if( status.w_termsig ) {
        fprintf( stderr,"ptyjig: %s: %s%s\n",progname,
                 mesg[status.w_termsig].pname,
                 status.w_coredump ? " (core dumped)" : "" );
      }

      /* If process terminates normally, return its retcode */
      /* If abnormally, return termsig.  This is not exactly */
      /* the same as csh, since the csh method is not too obvious */

      exit( status.w_termsig ? status.w_termsig : status.w_retcode );
    }

    exit(0);
  }
}



/*
 * Clean up processes
 */
void clean() {
#ifdef DEBUG
  fprintf( stderr, "ptyjig: in clean()\n" );
#endif

  signal( SIGCHLD, SIG_IGN ); /* Not necessary for sigchld to take over */

  /* must close files, and kill all running processes */
  if( execPID != -1 ) {
#ifdef DEBUG
    fprintf( stderr, "ptyjig: killing execPID = %d\n", execPID );
#endif
    kill( execPID, SIGKILL );
  }

  if( writerPID != -1 ) {
#ifdef DEBUG
    fprintf( stderr, "ptyjig: killing writerPID = %d\n", writerPID );
#endif
    kill( writerPID, SIGKILL );
  }

  done();
}



/* 
 * Handle window size change SIGWINCH
 */
void sigwinch(int sig) {
  struct winsize ws;


  ioctl( 0,   TIOCGWINSZ, &ws );
  ioctl( pty, TIOCSWINSZ, &ws );

  kill( execPID, SIGWINCH );
}



/* 
 * Handle user interrupt SIGINT
 */
void clean_int(int sig) {
#ifdef DEBUG
  fprintf( stderr, "ptyjig: got signal SIGINT\n" );
#endif

  signal( SIGINT, SIG_DFL );
  clean();

  kill( readerPID, SIGINT );
}



/* 
 * Handle quit signal SIGQUIT
 */
void clean_quit(int sig) {
#ifdef DEBUG
  fprintf( stderr, "ptyjig: got signal SIGQUIT\n" );
#endif

  clean();
  signal( SIGQUIT, SIG_DFL );
  kill( readerPID, SIGQUIT );
}



/* 
 * Handle user terminate signal SIGTERM
 */
void clean_term(int sig) {
#ifdef DEBUG
  fprintf( stderr, "ptyjig: got signal SIGTERM\n" );
#endif

  clean();
  signal( SIGTERM, SIG_DFL );

  kill( readerPID, SIGTERM );
}



/*
 * Open /dev/tty and grab attribute info associated
 * with a "normal" tty session
 */
#ifdef SOLARIS
void gettty() {
  int copyOfTTY;


  if(  (copyOfTTY = open("/dev/tty",O_RDWR)) >= 0  ) {
    /* GET them all */
    ioctl( copyOfTTY, TIOCGETP,   (char*)  &oldsb );
    ioctl( copyOfTTY, TIOCGETC,   (char*)  &gtc   );
    ioctl( copyOfTTY, TIOCGETD,   (char *) &gl    );
    ioctl( copyOfTTY, TIOCGLTC,   (char *) &glc   );
    ioctl( copyOfTTY, TIOCLGET,   (char *) &glb   );
    ioctl( copyOfTTY, TIOCGWINSZ, (char *) &gwin  );

    close( copyOfTTY );
  }
}
#endif



/* 
 * Setup the tty to be RAW, no ECHO, and CBREAK for Solaris types
 */
#ifdef SOLARIS
void fixtty() {
  struct sgttyb b;


  if( flage ) {
    flage = gtc.t_eofc;
  }

  b = oldsb;
  b.sg_flags |= RAW | CBREAK;
  b.sg_flags &= ~ECHO;

  ioctl(0, TIOCSETP, (char *) &b);
}
#endif



/*
 * Opens a master pseudo-tty device on range ptyp0 ... ptyr9 
 */
void setup_pty() {
  char    c;
  int     i;
  struct stat stb;
  int     foundOne = FALSE;


  /*
   * Make up the pseudo-tty names, namely /dev/ptyp0.../dev/ptyr9 
   * Solaris can handle up to 's' and up to 16, respectively, but
   * to be portable, we use only the overlapping range.
   */
  for( c = 'p' ; c <= 'r' && !foundOne ; c++ ) {
    for( i = 0 ; i <= 9 && !foundOne ; i++ ) {
      sprintf( ttyNameUsed, "/dev/pty%c%x", c, i );

      if( stat(ttyNameUsed, &stb) < 0 ) {
        fprintf( stderr, "ptyjig: no pty's available\n" );
        exit( 2 );
      }

      if(  (pty = open( ttyNameUsed, O_RDWR )) > 0  ) {
        /* Check for validity of the other side */
        ttyNameUsed[5] = 't';

        if(  access( ttyNameUsed, R_OK | W_OK ) == 0  ) {
          foundOne = TRUE;
        }
        else {
          close( pty );
        }
      }
    }
  }

  if( !(foundOne) ) {
    fprintf( stderr, "ptyjig: no pty's available\n" );
    exit( 2 );
  }
}



/*
 * Opens the slave device.  The device name is already in "ttyNameUsed" 
 * put in there by setup_pty(). 
 */
void setup_tty() {
#ifdef SOLARIS
  struct sgttyb b;
  int copyOfTTY;
#endif



#ifdef SOLARIS
  if(  (copyOfTTY = open( "/dev/tty", O_RDWR)) >= 0  ) {
    ioctl( copyOfTTY, TIOCNOTTY, 0 );  
    /*      close(copyOfTTY);      prevent character lost by not closing it */
  }
#endif

  /* Open modified "ttyNameUsed" as the control terminal */
  ttyNameUsed[5] = 't';
  tty = open( ttyNameUsed, O_RDWR );

  if( tty < 0 ) {
    perror( ttyNameUsed );
    exit( 1 );
  }

#ifdef SOLARIS
  /* Transfer the capabilities to the new tty */
  b = oldsb;

  /* In particular, when coming from a pipe, do not modify '\r' to '\r\n' */
  if( !isatty(0) ) {
    b.sg_flags &= ~CRMOD;
  }

  ioctl( tty, TIOCSETP,   (char *) &b    );
  ioctl( tty, TIOCSETC,   (char *) &gtc  );
  ioctl( tty, TIOCSETD,   (char *) &gl   );
  ioctl( tty, TIOCSLTC,   (char *) &glc  );
  ioctl( tty, TIOCLSET,   (char *) &glb  );
  ioctl( tty, TIOCSWINSZ, (char *) &gwin );
#endif
}



/*
 * Sets boolean to false to stop CMD's execution?
 */
void execute_done( int sig ) {
  executing = FALSE;
}



/*
 * Fork off a copy and execute "arg".  Before executing, assign "tty" to
 * stdin, stdout and stderr, so that the output of the child program can be
 * recorded by the other end of "tty". 
 */
void execute( char** cmd ) {
  int fstdin, fstdout, fstderr;


  signal( SIGUSR1, execute_done );

  if(  (execPID = fork()) == -1  ) {
    perror("execute(): fork");
    exit( 1 );
  }

  if( execPID == CHILD ) {
    /* save copies in case exec fails */
    fstdin  = dup(0);
    fstdout = dup(1);
    fstderr = dup(2); 

    setup_tty();

    dup2(tty, 0);        /* copy tty to stdin  */
    dup2(tty, 1);        /* copy tty to stdout  */
    dup2(tty, 2);        /* copy tty to stderr  */

    close(tty);

    if( flags ) {        /* suppress signals if -s present */
      signal( SIGINT,  SIG_IGN );
      signal( SIGQUIT, SIG_IGN );
      signal( SIGTSTP, SIG_IGN );
    }

    /* Better be setup to handle a SIGUSR1 */
    kill( getppid(), SIGUSR1 );

    execvp( cmd[0], cmd );

    /* IF IT EVER GETS HERE, error when executing "cmd"  */
    dup2(fstdin,  0);
    dup2(fstdout, 1);
    dup2(fstderr, 2);

#ifdef SOLARIS
    ioctl(0, TIOCSETP, (char*)&oldsb);
#endif

    perror( cmd[0] );

    exit(1);
  }


  while( executing ); /* let child run until it gives signal */

  usleep( flagw );

#ifdef DEBUG
  fprintf( stderr, "ptyjig: execPID = %d\n", execPID );
#endif
}
 


/* 
 *  Sleeps for 1 second, then KILLs PID execPID using SIGKILL
 */
void reader_done(int sig) {
  sleep( 1 );   /* Let execPID die naturally */

#ifdef DEBUG
  fprintf( stderr, "ptyjig: killing execPID = %d\n", execPID );
#endif

  kill( execPID, SIGKILL ); /* If it doesn't die on its own, kill it */
}



/*
 * Sets boolean to false to stop writer
 */
void writer_done( int sig ) {
  writing = FALSE;
  ualarm(flagt, 0);
}



/*
 * Read from stdin and send everything character read to "pty".  Record the
 * keystrokes in "filei" if -i flag is on. 
 */
void writer() {
  char c;


  /*
   * Read from keyboard continuously and send it to "pty" 
   */
  while( read(0, &c, 1) == 1 ) { 
    if( write(pty, &c, 1) != 1 ) {
      break;
    }

    if (flagi) {
      /* Do not send '\r', send '\n' instead */
      if( c == '\r' ) {
        c = '\n';
      }

      if(  write( fileno(filei), &c, 1 ) != 1  ) {
        perror( namei );
        break;
      }
    }

    /* Delay writing to "pty" if flagged */
    if( flagd ) {
      usleep( flagd );
    }
  }

  if( flage ) {
    (void)write( pty, &flage, 1 );
  }

#ifdef DEBUG
  fprintf( stderr, "ptyjig: writer finished\n" );
#endif

  kill( readerPID, SIGUSR1 ); /* tell reader to quit if no more char from exec */

  while( 1 );  /* XXX: INFINITE LOOP: Wait until parent kills me */
}



/*
 * Read from "pty" and send it to stdout 
 */
void reader() {
  char    c[BUFSIZ];
  int     i;


  /*
   * Continuously read from "pty" until exhausted.  Write every character
   * to stdout if -x flag is not present, and to "fileo" if -o flag is on.   
   */
  signal( SIGALRM, reader_done );
  signal( SIGUSR1, writer_done );

  while ((i = read(pty, c, sizeof(c))) > 0) {
    if( !flagx ) {
      if( write(1, c, i) != i ) {
        exit( 1 );
      }
    }

    if( flago ) {
      if(  write( fileno(fileo), c, i ) != i  ) {
        perror( nameo );
        exit( 1 );
      }
    }

    /*
     * The following "if" essentially means when "writer" is done, and
     * there is no more keystroke coming from "pty" wait for "flagt"
     * seconds and quit.  If during this wait, a character comes from
     * "pty", then the wait is set back. 
     */
    if( !writing ) {
      ualarm( flagt, 0 );
    }
  }

#ifdef DEBUG
  fprintf( stderr, "ptyjig: reader finished\n" );
#endif

  reader_done( 0 );
}



/*
 * WHY?
 */
void doreader() {
  reader();
}



/*
 * Fork writer 
 */
void dowriter() {
  if(  (writerPID = fork()) == -1  ) {
    perror("dowriter(): fork:");
    exit(1);
  } 

  if( writerPID == CHILD ) {
    writerPID = getpid();
    writer();
  }
     
#ifdef DEBUG
  fprintf( stderr, "ptyjig: writerPID = %d\n", writerPID );
#endif
}



/*
 * Display help screen 
 */
void usage() {
  printf("  ptyjig -- Super pipe for piping output to Unix utilities.\n\n");
  printf("  Usage:\n");
  printf("    ptyjig [options] cmd <args>\n\n");
  printf("  Description:\n");
  printf("    Run command \"cmd\" with arguments \"args\" in background, piping\n");
  printf("    stdin to \"cmd\" as its input and prints out \"cmd\"'s output\n");
  printf("    to stdout.  This program sets up pseudo-terminal pairs, so that\n");
  printf("    it can be used to pipe input to programs that read directly from\n");
  printf("    a tty interface.\n\n");
  printf("  Options:\n");
  printf("    -e          suppresses sending EOF char after stdin exhausted\n");
  printf("    -s          suppresses interrupts\n");
  printf("    -x          suppresses the standard output\n");
  printf("    -i FILEIN   standard input saved to file FILEIN\n");
  printf("    -o FILEOUT  standard output saved to file FILEOUT\n");
  printf("    -d DELAY    use a keystroke delay of DELAY seconds (accepts floating pt)\n");
  printf("    -t TIMEOUT  kill \"cmd\" if stdin exhausted and \"cmd\" doesn't send\n");
  printf("                output for TIMEOUT seconds\n");
  printf("    -w WAIT     wait WAIT seconds before streaming input to \"cmd\"\n\n");
  printf("  Defaults:\n");
  printf("    -i /dev/null -o /dev/null -d 0 -t 2\n\n");
  printf("  Examples:\n\n");
  printf("     pty -o out -d 0.05 -t 10 vi text1 < text2\n\n");
  printf("       Starts \"vi text1\" in background, typing the characters\n");
  printf("       in \"text2\" into it with a delay of 0.05 sec between each\n");
  printf("       character, and save the output of \"vi\" to \"out\".\n");
  printf("       Program ends when \"vi\" stops outputting for 10 seconds.\n\n");
  printf("     pty -i in -o out csh\n\n");
  printf("       Behaves like \"script out\" except the keystrokes typed by\n");
  printf("       a user are also saved into \"in\".\n");
  printf("  Authors: \n" );
  printf("     Lars Fredriksen, Bryan So, Barton Miller\n\n" );
  printf("  Updated by: \n" );
  printf("     Gregory Smethells, Brian Bowers, Karlen Lie\n" );

  exit( 1 );
}



/*
 * Parse the args, start reader and writer, fork the process to be "piped" to 
 */
int main( int argc, char** argv ) {
  int     num;
  int     cont;
  float   f;
  extern int   optind;
  extern char* optarg;

#ifdef LINUX
    struct termios termIOSettings;
#endif


  /* Parse the arguments to ptyjig. A ":" after a letter means */
  /* that flag takes an argument value and isn't just a boolean */
  while(  (argc > 1) && (argv[1][0] == '-')  ) {
    num  = 1;
    cont = TRUE;

    while( cont ) {
      switch( argv[1][num] ) {
        case 'e':
          flage = FALSE;
          break;

        case 's':
          flags = TRUE;
          break;

        case 'x':
          flagx = TRUE;
          break;

        case 'i':
          flagi = TRUE;
          namei = argv[2];
          argc--;
          argv++;
          cont = FALSE;
          break;

        case 'o':
          flago = TRUE;
          nameo = argv[2];
          argc--;
          argv++;
          cont = FALSE;
          break;

        case 'd':
          if(  sscanf( argv[2], "%f", &f ) < 1  ) {
            usage();
          }
  
          argc--;
          argv++;
          cont = FALSE;
  
          /* Convert to microseconds */
          flagd = (unsigned)(f * 1000000.0);
          break;

        case 't':
          if(  sscanf( argv[2], "%f", &f ) < 1  ) {
            usage();
          }

          argc--;
          argv++;
          cont = FALSE;

          /* Convert to microseconds */
          flagt = (unsigned)(f * 1000000.0);
          break;

        case 'w':
          if(  sscanf( argv[2], "%f", &f ) < 1  ) {
            usage();
          }
 
          argc--;
          argv++;
          cont = FALSE;

          /* Convert to microseconds */
          flagw = (unsigned)(f * 1000000.0);
          break;

        default:
          usage();
      }
 
      num++;
    
      if( cont && !argv[1][num] ) {
        cont = FALSE;
      }
    }

    argc--;
    argv++;
  }


  /* Now, argv better point to a command */
  if( !argv[1] ) {
    usage();
  }

  /* Possibly open a "save the input file"  */
  if( flagi ) {
    filei = fopen( namei, "wb" );

    if( filei == NULL ) {
      perror(namei);
      exit(1);
    }
  }

  /* Possible open a "save the output file" */
  if( flago ) {
    fileo = fopen( nameo, "wb" );

    if( fileo == NULL ) {
      perror(nameo);
      exit(1);
    }
  }


#ifdef SOLARIS
  /* get attribute info about /dev/tty */
  gettty();
#endif

  /* open an arbitrary pseudo-terminal pair  */
  setup_pty();

#ifdef LINUX
  /* Get Attributes for Master */
  tcgetattr( pty, &termIOSettings );

  /* Make output as RAW as possible */
  cfmakeraw( &termIOSettings );

  termIOSettings.c_lflag |= ECHO;

  /* Set Attributes for Master to RAWed version of "termIOSettings" */
  (void) tcsetattr( pty, TCSANOW, &termIOSettings );
#endif

  signal( SIGCHLD, sigchld ); 

  /* fork and execute test program with arguments */
  progname = argv[1]; 
  execute( (char **) &argv[1] );

#ifdef SOLARIS
  /* setup the tty to be RAW, CBREAK, and not ECHO */
  fixtty();
#endif

  signal( SIGWINCH, sigwinch ); 

  readerPID = getpid(); 

  dowriter();

  /* put here instead of above to avoid invoking clean_XYZ twice */
  signal( SIGQUIT, clean_quit ); 
  signal( SIGTERM, clean_term ); 
  signal( SIGINT,  clean_int  ); 

  doreader();

  while( 1 );  /* XXX: INFINITE LOOP: Wait for SIGCHLD to make us exit */
}


