#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <linux/limits.h>
#include <errno.h> 
#include <fcntl.h>   // Required for open() flags
#include <signal.h>  // Required for kill() and signal definitions
#include "LineParser.h" 

#define MAX_INPUT_SIZE 2048
#define READ_END 0
#define WRITE_END 1
int g_isDebug = 0; // Global flag for debug mode

// Function declarations
int handle_signal_command(cmdLine *pCmdLine, int sig, const char *cmd_name);
void execute_pipe(cmdLine *pCmdLine); // Task 2 - Pipe implementation

// --- Task 3: Helper function to handle signal commands (zzzz, kuku, blast) ---

int handle_signal_command(cmdLine *pCmdLine, int sig, const char *cmd_name) {
    if (strcmp(pCmdLine->arguments[0], cmd_name) == 0) {
        
        if (pCmdLine->argCount < 2) {
            fprintf(stderr, "%s: Missing process ID.\n", cmd_name);
            return 1;
        }
        
        pid_t target_pid = atoi(pCmdLine->arguments[1]);
        
        if (kill(target_pid, sig) == -1) {
            perror("kill failed");
        } else {
            // Confirmation (optional, but helpful for debugging)
            // fprintf(stderr, "DEBUG: Signal %d sent successfully to PID %d.\n", sig, target_pid);
        }
        return 1; // Command handled
    }
    return 0; // Command was not this signal command
}

// --- Task 2: Pipe Implementation ---

void execute_pipe(cmdLine *pCmdLine) {
    int pipefd[2];
    pid_t pid1, pid2;

    if (pipe(pipefd) == -1) {
        perror("pipe failed");
        return;
    }

    // 1. Fork the left command (Producer)
    pid1 = fork();
    if (pid1 == -1) {
        perror("fork failed for left command");
        close(pipefd[0]); close(pipefd[1]);
        return;
    }

    if (pid1 == 0) {
        // Child 1 (Left Command)
        
        // Redirect stdout (FD 1) to the write-end of the pipe
        if (dup2(pipefd[WRITE_END], STDOUT_FILENO) == -1) {
            perror("dup2 left write failed"); _exit(1);
        }
        
        // Close unused pipe descriptors (CRITICAL)
        close(pipefd[READ_END]); 
        close(pipefd[WRITE_END]); 

        // Execute the left command (pCmdLine) - I/O Redirection for the left command is implicit in this pipe logic
        if (execvp(pCmdLine->arguments[0], pCmdLine->arguments) == -1) {
            perror("execvp failed for left command");
            _exit(1);
        }
    } 
    
    // 2. Fork the right command (Consumer)
    pid2 = fork();
    if (pid2 == -1) {
        perror("fork failed for right command");
        close(pipefd[0]); close(pipefd[1]);
        waitpid(pid1, NULL, 0); 
        return;
    }

    if (pid2 == 0) {
        // Child 2 (Right Command)
        
        // Redirect stdin (FD 0) to the read-end of the pipe
        if (dup2(pipefd[READ_END], STDIN_FILENO) == -1) {
            perror("dup2 right read failed"); _exit(1);
        }
        
        // Close unused pipe descriptors (CRITICAL)
        close(pipefd[READ_END]); 
        close(pipefd[WRITE_END]); 

        // Execute the right command (pCmdLine->next)
        if (execvp(pCmdLine->next->arguments[0], pCmdLine->next->arguments) == -1) {
            perror("execvp failed for right command");
            _exit(1);
        }
    }

    // 3. Parent Process (Shell)
    // Parent MUST close ALL pipe descriptors
    close(pipefd[READ_END]); 
    close(pipefd[WRITE_END]); 

    // Wait for both child processes to finish (Mandatory for foreground pipe)
    waitpid(pid1, NULL, 0); 
    waitpid(pid2, NULL, 0);
}


/**
 * @brief Executes the command specified in pCmdLine, including internal commands and process management.
 */
void execute(cmdLine *pCmdLine) {
    if (pCmdLine == NULL) return;

    // --- Task 2: Check for Pipe ---
    if (pCmdLine->next != NULL) {
        execute_pipe(pCmdLine);
        return; // Pipe handled, exit the regular execute flow
    }
    // ---------------------------------

    // Check for the internal "quit" command
    if (strcmp(pCmdLine->arguments[0], "quit") == 0) {
        freeCmdLines(pCmdLine); 
        exit(0);
    }
    
    // --- Task 3: Handle zzzz, kuku, blast (Signal Commands) ---
    if (handle_signal_command(pCmdLine, SIGSTOP, "zzzz")) return;
    if (handle_signal_command(pCmdLine, SIGCONT, "kuku")) return;
    if (handle_signal_command(pCmdLine, SIGINT, "blast")) return;
    // --- End Task 3 ---

    // --- Task 1b: Implement the internal "cd" command ---
    if (strcmp(pCmdLine->arguments[0], "cd") == 0) {
        char *target_dir = (pCmdLine->argCount > 1) ? pCmdLine->arguments[1] : NULL;
        
        if (target_dir == NULL || strcmp(target_dir, "~") == 0) {
            target_dir = getenv("HOME");
            if (target_dir == NULL) {
                fprintf(stderr, "cd failed: HOME environment variable not set.\n");
                return;
            }
        }
        
        if (chdir(target_dir) == -1) {
            perror("chdir failed"); 
        }
        return; // CRITICAL: Exit without forking!
    }
    // --- End Task 1b ---

    // 1. Create a new process (child)
    pid_t pid = fork();

    if (pid == -1) {
        perror("fork failed");
        return; 
    } 
    else if (pid == 0) {
        // Child Process
        
        // --- FIX for Background Processes (Job Control): Detach I/O (Task 1c fix) ---
        if (!pCmdLine->blocking) {
            if (setpgid(0, 0) == -1) { perror("setpgid failed"); }

            int dev_null = open("/dev/null", O_RDONLY);
            if (dev_null != -1) {
                dup2(dev_null, STDIN_FILENO);
                close(dev_null);
            }
        }
        // --- End Background Fix ---
        
        // --- Task 2: I/O Redirection Implementation ---
        
        // 1. Output Redirection (>)
        if (pCmdLine->outputRedirect != NULL) {
            int fd_out = open(pCmdLine->outputRedirect, O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (fd_out == -1) { perror("open output failed"); _exit(1); }
            if (dup2(fd_out, STDOUT_FILENO) == -1) { perror("dup2 output failed"); close(fd_out); _exit(1); }
            close(fd_out);
        }

        // 2. Input Redirection (<)
        if (pCmdLine->inputRedirect != NULL) {
            int fd_in = open(pCmdLine->inputRedirect, O_RDONLY);
            if (fd_in == -1) { perror("open input failed"); _exit(1); }
            if (dup2(fd_in, STDIN_FILENO) == -1) { perror("dup2 input failed"); close(fd_in); _exit(1); }
            close(fd_in);
        }
        
        // --- End Task 2 I/O Redirection ---

        // Task 1a: Print debug info to stderr
        if (g_isDebug) {
            fprintf(stderr, "PID: %d\n", getpid());
            fprintf(stderr, "Executing command: %s\n", pCmdLine->arguments[0]);
        }
        
        // Execute the command
        if (execvp(pCmdLine->arguments[0], pCmdLine->arguments) == -1) {
            perror("execvp failed");
            _exit(1); 
        }
    } 
    else {
        // Parent Process (Task 1c: waitpid)
        if (pCmdLine->blocking) {
            if (waitpid(pid, NULL, 0) == -1) {
                 perror("waitpid failed");
            }
        }
    }
}

int main(int argc, char **argv) {
    // Task 1a: Check for -d flag
    if (argc > 1 && strcmp(argv[1], "-d") == 0) {
        g_isDebug = 1;
        fprintf(stderr, "Debug mode activated.\n");
    }
    
    char current_path[PATH_MAX];
    char input_buffer[MAX_INPUT_SIZE];
    cmdLine *parsed_line;

    while (1) {
        if (getcwd(current_path, PATH_MAX) != NULL) {
            printf("%s$ ", current_path);
        } else {
            fprintf(stderr, "Error: Could not retrieve current directory.\n");
            printf("$ ");
        }

        if (fgets(input_buffer, MAX_INPUT_SIZE, stdin) == NULL) {
            break;
        }

        input_buffer[strcspn(input_buffer, "\n")] = 0;
        parsed_line = parseCmdLines(input_buffer);

        if (parsed_line != NULL) {
            execute(parsed_line);
            freeCmdLines(parsed_line);
        }
    }
    
    printf("Exiting Shell normally.\n");
    return 0;
}