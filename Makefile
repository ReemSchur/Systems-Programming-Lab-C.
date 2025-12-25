# Define the compiler and flags
CC = gcc
# CFLAGS: -Wall enables common warnings, -g includes debug information
# NOTE: The '-m32' flag is maintained based on your original request.
CFLAGS = -Wall -g -m32

# External LineParser files (used by myshell)
LINEPARSER_OBJS = LineParser.o

# --- Main Targets ---

# Default target: builds all executables for the lab (myshell, looper, mypipe)
all: myshell looper mypipe

# Target 1: Compiling the myshell executable (Task 1, 2, 3)
myshell: myshell.o $(LINEPARSER_OBJS)
	$(CC) $(CFLAGS) -o myshell myshell.o $(LINEPARSER_OBJS)

# Target 2: Compiling the looper executable (Task 0b)
looper: Looper.o
	$(CC) $(CFLAGS) -o looper Looper.o

# Target 3: Compiling the mypipe executable (Task 4)
mypipe: mypipe.o
	$(CC) $(CFLAGS) -o mypipe mypipe.o

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

# Rule for mypipe object file
mypipe.o: mypipe.c
	$(CC) $(CFLAGS) -c mypipe.c

# --- Cleanup Rule ---

# Target to clean up compiled files, executables, and object files
clean:
	rm -f *.o myshell looper mypipe