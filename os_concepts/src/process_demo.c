/*
 * process_demo.c - Creates a child process with fork(), runs a Linux command
 * in it with execvp(), and waits for it with waitpid().
 * OS concepts: Unit 1 - System calls (fork, exec, wait, getpid);
 * Unit 2 - Operations on processes (creation, termination, parent/child).
 */
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#define MAX_ARGS 16

int main(void)
{
    char line[256];
    char *args[MAX_ARGS];
    int argc = 0;

    printf("Parent PID: %d\n", getpid());
    printf("Enter a Linux command to run in a child process (e.g. ls -l): ");
    fflush(stdout);
    if (!fgets(line, sizeof line, stdin))
        return 1;
    line[strcspn(line, "\n")] = '\0';

    for (char *tok = strtok(line, " "); tok && argc < MAX_ARGS - 1;
         tok = strtok(NULL, " "))
        args[argc++] = tok;
    args[argc] = NULL;

    if (argc == 0) {
        fprintf(stderr, "No command given.\n");
        return 1;
    }

    fflush(stdout);                 /* avoid duplicated buffered output in child */
    pid_t pid = fork();             /* system call: create a child process */
    if (pid < 0) {
        perror("fork");
        return 1;
    }

    if (pid == 0) {                 /* child */
        printf("Child PID: %d (parent PID: %d) running '%s'\n",
               getpid(), getppid(), args[0]);
        fflush(stdout);
        execvp(args[0], args);      /* replace child image with the command */
        perror("execvp");
        _exit(127);
    }

    int status;
    waitpid(pid, &status, 0);       /* parent waits for the child to finish */
    if (WIFEXITED(status))
        printf("Child %d exited with status %d\n", pid, WEXITSTATUS(status));
    else
        printf("Child %d terminated abnormally\n", pid);
    return 0;
}
