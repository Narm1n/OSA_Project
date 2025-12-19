// client.c — Version 1
// Build: gcc -Wall -O2 -o client client.c
// Run: ./client <server_pid>

// Sends SIGUSR1 for English→French
// Sends SIGUSR2 for French→English
// Repeats 100 times with small pauses.

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <server_pid>\n", argv[0]);
        return 1;
    }

    pid_t server_pid = (pid_t)atoi(argv[1]);
    if (server_pid <= 0) {
        fprintf(stderr, "Invalid PID.\n");
        return 1;
    }

    srand((unsigned)time(NULL) ^ (unsigned)getpid());

    printf("=== Dictionary Client ===\n");
    printf("Target server PID: %d\n", server_pid);
    printf("Sending 100 random translation requests...\n\n");

    for (int i = 1; i <= 100; i++) {
        int type = rand() % 2;  // 0 or 1
        int sig = (type == 0) ? SIGUSR1 : SIGUSR2;

        if (kill(server_pid, sig) == -1) {
            perror("kill");
            return 1;
        }

        if (sig == SIGUSR1)
            printf("[%3d] Sent SIGUSR1 (EN→FR)\n", i);
        else
            printf("[%3d] Sent SIGUSR2 (FR→EN)\n", i);

        fflush(stdout);
        usleep(200000); // 0.2 second delay between requests
    }

    printf("\nAll 100 requests sent.\n");
    return 0;
}

