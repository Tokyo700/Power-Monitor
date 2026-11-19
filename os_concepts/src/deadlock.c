/*
 * deadlock.c - Deadlock avoidance (Banker's algorithm) and deadlock detection.
 * OS concept: Unit 3 - Deadlocks (safe state, avoidance, detection).
 */
#include <stdio.h>

#define MAX_PROC 10
#define MAX_RES  10

static int n, m;                       /* processes, resource types */
static int alloc[MAX_PROC][MAX_RES];
static int maxd[MAX_PROC][MAX_RES];
static int need[MAX_PROC][MAX_RES];
static int avail[MAX_RES];

static int read_matrix(const char *name, int rows, int cols, int mat[][MAX_RES])
{
    printf("Enter the %s matrix (%d rows x %d columns):\n", name, rows, cols);
    for (int i = 0; i < rows; i++)
        for (int j = 0; j < cols; j++) {
            printf("  P%d R%d: ", i, j);
            if (scanf("%d", &mat[i][j]) != 1 || mat[i][j] < 0) {
                fprintf(stderr, "Invalid value.\n");
                return 0;
            }
        }
    return 1;
}

static int read_vector(const char *name, int len, int vec[])
{
    printf("Enter the %s vector (%d values): ", name, len);
    for (int j = 0; j < len; j++)
        if (scanf("%d", &vec[j]) != 1 || vec[j] < 0) {
            fprintf(stderr, "Invalid value.\n");
            return 0;
        }
    return 1;
}

/* Safety algorithm: returns 1 if a safe sequence exists and stores it in seq. */
static int is_safe(const int av[], int al[][MAX_RES], int nd[][MAX_RES], int seq[])
{
    int work[MAX_RES], finish[MAX_PROC] = {0}, count = 0;
    for (int j = 0; j < m; j++)
        work[j] = av[j];

    while (count < n) {
        int progress = 0;
        for (int i = 0; i < n; i++) {
            if (finish[i])
                continue;
            int ok = 1;
            for (int j = 0; j < m; j++)
                if (nd[i][j] > work[j]) { ok = 0; break; }
            if (ok) {
                for (int j = 0; j < m; j++)
                    work[j] += al[i][j];
                finish[i] = 1;
                seq[count++] = i;
                progress = 1;
            }
        }
        if (!progress)
            return 0;                  /* no process can finish: unsafe */
    }
    return 1;
}

static void print_sequence(const int seq[])
{
    for (int k = 0; k < n; k++)
        printf("P%d%s", seq[k], k < n - 1 ? " -> " : "\n");
}

static void avoidance(void)
{
    int seq[MAX_PROC];

    if (!read_matrix("Allocation", n, m, alloc) ||
        !read_matrix("Maximum", n, m, maxd) ||
        !read_vector("Available", m, avail))
        return;

    for (int i = 0; i < n; i++)
        for (int j = 0; j < m; j++) {
            need[i][j] = maxd[i][j] - alloc[i][j];
            if (need[i][j] < 0) {
                printf("Error: Allocation exceeds Maximum for P%d.\n", i);
                return;
            }
        }

    if (is_safe(avail, alloc, need, seq)) {
        printf("System is in a SAFE state. Safe sequence: ");
        print_sequence(seq);
    } else {
        printf("System is in an UNSAFE state (deadlock is possible).\n");
        return;
    }

    /* Resource request: grant only if the system stays safe. */
    int pid, req[MAX_RES];
    printf("Process ID making a request (-1 to skip): ");
    if (scanf("%d", &pid) != 1 || pid < 0)
        return;
    if (pid >= n) {
        printf("No such process.\n");
        return;
    }
    if (!read_vector("request", m, req))
        return;

    for (int j = 0; j < m; j++) {
        if (req[j] > need[pid][j]) {
            printf("Error: request exceeds the process's maximum claim.\n");
            return;
        }
        if (req[j] > avail[j]) {
            printf("Request must wait: not enough resources available.\n");
            return;
        }
    }

    /* Tentatively allocate, test safety, roll back if unsafe. */
    for (int j = 0; j < m; j++) {
        avail[j] -= req[j];
        alloc[pid][j] += req[j];
        need[pid][j] -= req[j];
    }
    if (is_safe(avail, alloc, need, seq)) {
        printf("Request GRANTED. New safe sequence: ");
        print_sequence(seq);
    } else {
        for (int j = 0; j < m; j++) {
            avail[j] += req[j];
            alloc[pid][j] -= req[j];
            need[pid][j] += req[j];
        }
        printf("Request DENIED: granting it would lead to an unsafe state.\n");
    }
}

static void detection(void)
{
    int req[MAX_PROC][MAX_RES], work[MAX_RES], finish[MAX_PROC];

    if (!read_matrix("Allocation", n, m, alloc) ||
        !read_matrix("Request", n, m, req) ||
        !read_vector("Available", m, avail))
        return;

    for (int j = 0; j < m; j++)
        work[j] = avail[j];
    for (int i = 0; i < n; i++) {
        finish[i] = 1;
        for (int j = 0; j < m; j++)
            if (alloc[i][j] != 0) { finish[i] = 0; break; }
    }

    int progress = 1;
    while (progress) {
        progress = 0;
        for (int i = 0; i < n; i++) {
            if (finish[i])
                continue;
            int ok = 1;
            for (int j = 0; j < m; j++)
                if (req[i][j] > work[j]) { ok = 0; break; }
            if (ok) {
                for (int j = 0; j < m; j++)
                    work[j] += alloc[i][j];
                finish[i] = 1;
                progress = 1;
            }
        }
    }

    int deadlocked = 0;
    printf("Deadlocked processes: ");
    for (int i = 0; i < n; i++)
        if (!finish[i]) {
            printf("P%d ", i);
            deadlocked = 1;
        }
    printf(deadlocked ? "\n" : "none (no deadlock detected)\n");
}

int main(void)
{
    int choice;

    printf("Enter number of processes (1-%d): ", MAX_PROC);
    if (scanf("%d", &n) != 1 || n < 1 || n > MAX_PROC) {
        fprintf(stderr, "Invalid number of processes.\n");
        return 1;
    }
    printf("Enter number of resource types (1-%d): ", MAX_RES);
    if (scanf("%d", &m) != 1 || m < 1 || m > MAX_RES) {
        fprintf(stderr, "Invalid number of resource types.\n");
        return 1;
    }

    printf("\n1. Deadlock avoidance (Banker's algorithm)\n");
    printf("2. Deadlock detection\n");
    printf("Choose (1 or 2): ");
    if (scanf("%d", &choice) != 1) {
        fprintf(stderr, "Invalid choice.\n");
        return 1;
    }

    if (choice == 1)
        avoidance();
    else if (choice == 2)
        detection();
    else
        fprintf(stderr, "Invalid choice.\n");
    return 0;
}
