#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <stdlib.h>

int main() {
    int pipefd[2];
    pid_t pid1, pid2;

    char *args_ls[] = {"ls", "-lsa", NULL};
    char *args_tail[] = {"tail", "-n", "3", NULL};

    if (pipe(pipefd) == -1) {
        perror("pipe");
        exit(EXIT_FAILURE);
    }

    fprintf(stderr, "(parent_process>forking...)\n");
    pid1 = fork();
    if (pid1 == -1) {
        perror("fork");
        exit(EXIT_FAILURE);
    }

    if (pid1 == 0) {
        // Child 1 (ls -lsa)
        fprintf(stderr, "(child1>redirecting stdout to the write end of the pipe...)\n");
        close(STDOUT_FILENO);
        if (dup(pipefd[1]) == -1) {
            perror("dup");
            exit(EXIT_FAILURE);
        }
        close(pipefd[1]);

        fprintf(stderr, "(child1>going to execute cmd: ls -lsa)\n");
        execvp(args_ls[0], args_ls);
        perror("execvp child1");
        exit(EXIT_FAILURE);
    } else {
        // Parent
        fprintf(stderr, "(parent_process>created process with id: %d)\n", pid1);
        
        fprintf(stderr, "(parent_process>closing the write end of the pipe...)\n");
        close(pipefd[1]);
    }

    fprintf(stderr, "(parent_process>forking...)\n");
    pid2 = fork();
    if (pid2 == -1) {
        perror("fork");
        exit(EXIT_FAILURE);
    }

    if (pid2 == 0) {
        // Child 2 (tail -n 3)
        fprintf(stderr, "(child2>redirecting stdin to the read end of the pipe...)\n");
        close(STDIN_FILENO);
        if (dup(pipefd[0]) == -1) {
            perror("dup");
            exit(EXIT_FAILURE);
        }
        close(pipefd[0]);

        fprintf(stderr, "(child2>going to execute cmd: tail -n 3)\n");
        execvp(args_tail[0], args_tail);
        perror("execvp child2");
        exit(EXIT_FAILURE);
    } else {
        // Parent
        fprintf(stderr, "(parent_process>created process with id: %d)\n", pid2);
        
        fprintf(stderr, "(parent_process>closing the read end of the pipe...)\n");
        close(pipefd[0]);
    }

    fprintf(stderr, "(parent_process>waiting for child processes to terminate...)\n");
    wait(NULL);
    wait(NULL);

    fprintf(stderr, "(parent_process>exiting...)\n");

    return 0;
}