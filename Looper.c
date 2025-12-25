#include <stdio.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>

void handler(int sig) {
    if (sig == SIGINT) {
        printf("\nLooper handling SIGINT\n");
        signal(SIGINT, SIG_DFL); // Restore default to allow termination next time
        raise(SIGINT); // Terminate
    } else if (sig == SIGTSTP) {
        printf("\nLooper handling SIGSTOP\n");
        signal(SIGTSTP, SIG_DFL); // Restore default to allow stop
        raise(SIGTSTP); // Stop
    } else if (sig == SIGCONT) {
        printf("\nLooper handling SIGCONT\n");
        signal(SIGCONT, handler); // Re-install handler
        signal(SIGTSTP, handler); // Re-install TSTP handler
    }
}

int main() {
    printf("Starting Looper (PID: %d)...\n", getpid());
    
    // Register signals
    signal(SIGINT, handler);
    signal(SIGTSTP, handler);
    signal(SIGCONT, handler);

    while(1) {
        sleep(2);
    }
    return 0;
}