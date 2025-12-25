#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <linux/limits.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include "LineParser.h"

#define MAX_INPUT_SIZE 2048
#define HISTLEN 20 // Size of history queue

// --- Status Macros ---
#define TERMINATED  -1
#define RUNNING 1
#define SUSPENDED 0

// --- Process Struct ---
typedef struct process {
    cmdLine* cmd;
    pid_t pid;
    int status;
    struct process *next;
} process;

// --- History Globals ---
char* history[HISTLEN]; // Array of pointers to strings
int newest = -1;        // Index of the most recently added command
int oldest = 0;         // Index of the oldest command
int hist_count = 0;     // Current number of items in history

// --- Other Globals ---
int g_isDebug = 0;
process* process_list = NULL;

// --- Function Declarations ---
void addProcess(process** process_list, cmdLine* cmd, pid_t pid);
void printProcessList(process** process_list);
void freeProcessList(process* process_list);
void updateProcessList(process **process_list);
void updateProcessStatus(process* process_list, int pid, int status);
void execute(cmdLine *pCmdLine);
void execute_pipe(cmdLine *pCmdLine);
int handle_signal_command(cmdLine *pCmdLine, int sig, const char *cmd_name);

// --- History Functions ---

void addToHistory(char* command) {
    // Duplicate the string because the input buffer will be overwritten
    char* cmdCopy = strdup(command); 
    
    if (hist_count < HISTLEN) {
        // Queue is not full
        newest = (newest + 1) % HISTLEN;
        history[newest] = cmdCopy;
        hist_count++;
    } else {
        // Queue is full: Overwrite oldest
        free(history[oldest]); // Free memory of the overwritten command
        history[oldest] = cmdCopy;
        newest = oldest;       // The slot we just wrote to is now the newest
        oldest = (oldest + 1) % HISTLEN; // Move oldest pointer forward
    }
}

void printHistory() {
    // Logic: Iterate from oldest to newest
    // Note: The prompt implies printing the array index or sequential list. 
    // We will print the actual array index to make !n easier to understand.
    
    int i;
    int current_idx;
    for (i = 0; i < hist_count; i++) {
        current_idx = (oldest + i) % HISTLEN;
        printf("%d: %s\n", current_idx, history[current_idx]);
    }
}

void freeHistory() {
    for (int i = 0; i < hist_count; i++) {
        int idx = (oldest + i) % HISTLEN;
        free(history[idx]);
    }
}

char* getHistoryCommand(int index) {
    // Check if index is valid (must be a currently occupied slot)
    if (hist_count == 0) return NULL;
    
    // Simple check: Is the pointer at this index not NULL?
    // (In a circular buffer, checking validity is tricky, assuming 0..HISTLEN-1 input)
    if (index < 0 || index >= HISTLEN || history[index] == NULL) {
        return NULL;
    }
    return history[index];
}

// --- Process Manager Implementation ---

void addProcess(process** process_list, cmdLine* cmd, pid_t pid) {
    process* new_proc = malloc(sizeof(process));
    new_proc->cmd = cmd;
    new_proc->pid = pid;
    new_proc->status = RUNNING;
    new_proc->next = *process_list;
    *process_list = new_proc;
}

void updateProcessStatus(process* process_list, int pid, int status) {
    process* curr = process_list;
    while (curr != NULL) {
        if (curr->pid == pid) {
            curr->status = status;
            return;
        }
        curr = curr->next;
    }
}

void updateProcessList(process **process_list) {
    process* curr = *process_list;
    while (curr != NULL) {
        int status;
        pid_t result = waitpid(curr->pid, &status, WNOHANG | WUNTRACED | WCONTINUED);

        if (result == -1) {
            curr->status = TERMINATED;
        } else if (result != 0) {
            if (WIFEXITED(status) || WIFSIGNALED(status)) {
                curr->status = TERMINATED;
            } else if (WIFSTOPPED(status)) {
                curr->status = SUSPENDED;
            } else if (WIFCONTINUED(status)) {
                curr->status = RUNNING;
            }
        }
        curr = curr->next;
    }
}

void printProcessList(process** process_list) {
    updateProcessList(process_list);

    process* curr = *process_list;
    process* prev = NULL;
    process* next_node;

    printf("PID\t\tCommand\t\tSTATUS\n");

    while (curr != NULL) {
        char* status_str;
        if (curr->status == RUNNING) status_str = "Running";
        else if (curr->status == SUSPENDED) status_str = "Suspended";
        else status_str = "Terminated";

        printf("%d\t\t%s\t\t%s\n", curr->pid, curr->cmd->arguments[0], status_str);

        if (curr->status == TERMINATED) {
            next_node = curr->next;
            freeCmdLines(curr->cmd);
            free(curr);

            if (prev == NULL) {
                *process_list = next_node;
            } else {
                prev->next = next_node;
            }
            curr = next_node;
        } else {
            prev = curr;
            curr = curr->next;
        }
    }
}

void freeProcessList(process* process_list) {
    process* curr = process_list;
    while (curr != NULL) {
        process* temp = curr;
        curr = curr->next;
        freeCmdLines(temp->cmd);
        free(temp);
    }
}

// --- Helper for Signals ---
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
            printf("%s: Signal sent to PID %d\n", cmd_name, target_pid);
             int new_status = RUNNING;
            if (sig == SIGSTOP) new_status = SUSPENDED;
            else if (sig == SIGINT) new_status = TERMINATED; 
            else if (sig == SIGCONT) new_status = RUNNING;
            updateProcessStatus(process_list, target_pid, new_status);
        }
        return 1; 
    }
    return 0; 
}

// --- Pipe Execution ---
void execute_pipe(cmdLine *pCmdLine) {
    int pipefd[2];
    pid_t pid1, pid2;

    if (pipe(pipefd) == -1) {
        perror("pipe failed");
        return;
    }

    pid1 = fork();
    if (pid1 == -1) {
        perror("fork left failed");
        close(pipefd[0]); close(pipefd[1]);
        return;
    }

    if (pid1 == 0) { // Left Child
        if (pCmdLine->inputRedirect != NULL) {
            close(STDIN_FILENO);
            int fd_in = open(pCmdLine->inputRedirect, O_RDONLY);
            if (fd_in == -1) { perror("open input failed"); _exit(1); }
            if (fd_in != STDIN_FILENO) { dup2(fd_in, STDIN_FILENO); close(fd_in); }
        }
        close(STDOUT_FILENO);
        dup(pipefd[1]);
        close(pipefd[1]);
        close(pipefd[0]);
        if (execvp(pCmdLine->arguments[0], pCmdLine->arguments) == -1) {
            perror("execvp left failed"); _exit(1);
        }
    } 
    
    close(pipefd[1]); // Parent closes write end

    pid2 = fork();
    if (pid2 == -1) {
        perror("fork right failed");
        close(pipefd[0]);
        waitpid(pid1, NULL, 0); 
        return;
    }

    if (pid2 == 0) { // Right Child
        close(STDIN_FILENO);
        dup(pipefd[0]);
        close(pipefd[0]);
        if (pCmdLine->next->outputRedirect != NULL) {
            close(STDOUT_FILENO);
            int fd_out = open(pCmdLine->next->outputRedirect, O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (fd_out == -1) { perror("open output failed"); _exit(1); }
            if (fd_out != STDOUT_FILENO) { dup2(fd_out, STDOUT_FILENO); close(fd_out); }
        }
        if (execvp(pCmdLine->next->arguments[0], pCmdLine->next->arguments) == -1) {
            perror("execvp right failed"); _exit(1);
        }
    }

    close(pipefd[0]); // Parent closes read end
    waitpid(pid1, NULL, 0); 
    waitpid(pid2, NULL, 0);
}

// --- Main Execution Logic ---
void execute(cmdLine *pCmdLine) {
    if (pCmdLine == NULL) return;

    // Task 4: History Command
    if (strcmp(pCmdLine->arguments[0], "history") == 0) {
        printHistory();
        freeCmdLines(pCmdLine);
        return;
    }

    // Task 2: Pipe
    if (pCmdLine->next != NULL) {
        if (pCmdLine->outputRedirect != NULL) {
            fprintf(stderr, "Error: Output redirection on left side of pipe forbidden.\n");
            freeCmdLines(pCmdLine); return;
        }
        if (pCmdLine->next->inputRedirect != NULL) {
            fprintf(stderr, "Error: Input redirection on right side of pipe forbidden.\n");
            freeCmdLines(pCmdLine); return;
        }
        execute_pipe(pCmdLine);
        freeCmdLines(pCmdLine);
        return; 
    }

    // Commands
    if (strcmp(pCmdLine->arguments[0], "quit") == 0) {
        freeCmdLines(pCmdLine);
        freeProcessList(process_list);
        freeHistory(); // Cleanup history
        exit(0);
    }
    
    if (handle_signal_command(pCmdLine, SIGSTOP, "zzzz") ||
        handle_signal_command(pCmdLine, SIGCONT, "kuku") ||
        handle_signal_command(pCmdLine, SIGINT, "blast")) {
        freeCmdLines(pCmdLine);
        return;
    }

    if (strcmp(pCmdLine->arguments[0], "cd") == 0) {
        char *target_dir = (pCmdLine->argCount > 1) ? pCmdLine->arguments[1] : NULL;
        if (target_dir == NULL || strcmp(target_dir, "~") == 0) target_dir = getenv("HOME");
        if (chdir(target_dir) == -1) perror("chdir failed"); 
        freeCmdLines(pCmdLine);
        return;
    }
    
    if (strcmp(pCmdLine->arguments[0], "procs") == 0) {
        printProcessList(&process_list);
        freeCmdLines(pCmdLine);
        return;
    }

    // Standard Exec
    pid_t pid = fork();

    if (pid == -1) {
        perror("fork failed");
        freeCmdLines(pCmdLine);
        return; 
    } else if (pid == 0) {
        // Child
        signal(SIGQUIT, SIG_DFL);
        signal(SIGTSTP, SIG_DFL);
        signal(SIGTTIN, SIG_DFL);
        signal(SIGTTOU, SIG_DFL);
        signal(SIGCHLD, SIG_DFL);

        if (!pCmdLine->blocking) {
            if (setpgid(0, 0) == -1) perror("setpgid");
        }
        
        if (pCmdLine->outputRedirect != NULL) {
            int fd_out = open(pCmdLine->outputRedirect, O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (fd_out == -1) { perror("open output failed"); _exit(1); }
            dup2(fd_out, STDOUT_FILENO); close(fd_out);
        }

        if (pCmdLine->inputRedirect != NULL) {
            int fd_in = open(pCmdLine->inputRedirect, O_RDONLY);
            if (fd_in == -1) { perror("open input failed"); _exit(1); }
            dup2(fd_in, STDIN_FILENO); close(fd_in);
        }
        
        if (g_isDebug) fprintf(stderr, "PID: %d\nExecuting: %s\n", getpid(), pCmdLine->arguments[0]);
        
        execvp(pCmdLine->arguments[0], pCmdLine->arguments);
        perror("execvp failed");
        _exit(1); 
    } else {
        // Parent
        addProcess(&process_list, pCmdLine, pid);
        if (pCmdLine->blocking) waitpid(pid, NULL, 0);
    }
}

int main(int argc, char **argv) {
    if (argc > 1 && strcmp(argv[1], "-d") == 0) {
        g_isDebug = 1;
        fprintf(stderr, "Debug mode activated.\n");
    }
    
    char current_path[PATH_MAX];
    char input_buffer[MAX_INPUT_SIZE];
    cmdLine *parsed_line;

    while (1) {
        if (getcwd(current_path, PATH_MAX) != NULL) printf("%s$ ", current_path);
        else printf("$ ");

        if (fgets(input_buffer, MAX_INPUT_SIZE, stdin) == NULL) break;
        input_buffer[strcspn(input_buffer, "\n")] = 0; // Remove newline

        // --- Task 4: History Mechanism Handling (!! and !n) ---
        // We must handle this BEFORE parsing, as we substitute the raw string.
        
        if (strcmp(input_buffer, "!!") == 0) {
            // Retrieve last command
            if (hist_count == 0) {
                fprintf(stderr, "Error: History is empty.\n");
                continue; // Skip execution
            }
            char* last_cmd = history[newest];
            strcpy(input_buffer, last_cmd); // Replace buffer with history
            printf("%s\n", input_buffer);   // Echo the command
        } 
        else if (input_buffer[0] == '!' && strlen(input_buffer) > 1) {
            // Retrieve specific index (!n)
            int n = atoi(input_buffer + 1); // Parse number after '!'
            char* cmd = getHistoryCommand(n);
            if (cmd == NULL) {
                fprintf(stderr, "Error: No such command in history (Index %d).\n", n);
                continue;
            }
            strcpy(input_buffer, cmd);
            printf("%s\n", input_buffer); // Echo
        }

        // Add to history (unless it's empty)
        if (strlen(input_buffer) > 0) {
             addToHistory(input_buffer);
        }
        // --------------------------------------------------------

        parsed_line = parseCmdLines(input_buffer);

        if (parsed_line != NULL) {
            execute(parsed_line);
        }
    }
    
    freeProcessList(process_list);
    freeHistory();
    return 0;
}