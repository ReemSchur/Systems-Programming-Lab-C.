# Define the compiler and flags
CC = gcc
# CFLAGS: -Wall enables common warnings, -g includes debug information, -m32 for 32-bit environment
CFLAGS = -Wall -g -m32

# External LineParser files (used by myshell)
LINEPARSER_OBJS = LineParser.o

# --- Main Targets ---

# Default target: builds all executables including the new pipeline task
all: myshell looper mypipeline

# Target 1: Compiling the myshell executable
myshell: myshell.o $(LINEPARSER_OBJS)
	$(CC) $(CFLAGS) -o myshell myshell.o $(LINEPARSER_OBJS)

# Target 2: Compiling the looper executable
looper: Looper.o
	$(CC) $(CFLAGS) -o looper Looper.o

# Target 3: Compiling the mypipeline executable (Lab C - Part 1)
mypipeline: mypipeline.o
	$(CC) $(CFLAGS) -o mypipeline mypipeline.o

# --- Object File Rules ---

# Rule for myshell object file
myshell.o: myshell.c LineParser.h
	$(CC) $(CFLAGS) -c myshell.c

# Rule for LineParser object file
LineParser.o: LineParser.c LineParser.h
	$(CC) $(CFLAGS) -c LineParser.c

# Rule for Looper object file
Looper.o: Looper.c
	$(CC) $(CFLAGS) -c Looper.c

# Rule for mypipeline object file
mypipeline.o: mypipeline.c
	$(CC) $(CFLAGS) -c mypipeline.c

# --- Cleanup Rule ---

# Target to clean up compiled files
clean:
	rm -f *.o myshell looper mypipeline