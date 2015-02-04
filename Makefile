#-----------------------------------------------------------------
#  Copyright (c) 1989 Lars Fredriksen, Bryan So, Barton Miller
#
#  This program is distributed in the hope that it will be useful, but
#  WITHOUT ANY WARRANTY; without even the implied warranty of
#  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
#
#-----------------------------------------------------------------

# source file names
SRCS =  fuzz.c ptyjig.c
# object file names here
OBJS =  fuzz.o ptyjig.o


# Add your own flags for the C compiler.
# CFLAGS = -DDEBUG 
CFLAGS=  -O


# Don't modify these.
all: fuzz ptyjig
	@echo 'all programs generated'

fuzz:  fuzz.c
	cc ${CFLAGS} -o fuzz fuzz.c

ptyjig: ptyjig.c
	cc ${CFLAGS} -o ptyjig ptyjig.c

lint: 
	lint -hxb -DLINT  $(SRCS) > LINTERRS

clean:
	rm -f $(OBJS) core
