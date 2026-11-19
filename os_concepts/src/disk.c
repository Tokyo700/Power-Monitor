/*
 * disk.c - Disk head scheduling: FCFS, SSTF and SCAN.
 * OS concept: Unit 5 - I/O systems and mass storage (disk scheduling).
 */
#include <stdio.h>
#include <stdlib.h>

#define MAX_REQ 100

static int requests[MAX_REQ];
static int nreq;
static int cylinders;     /* disk has cylinders 0 .. cylinders-1 */
static int head;

static void print_path(const char *name, const int path[], int len)
{
    int total = 0;
    printf("\n--- %s ---\nHead path: %d", name, path[0]);
    for (int k = 1; k < len; k++) {
        total += abs(path[k] - path[k - 1]);
        printf(" -> %d", path[k]);
    }
    printf("\nTotal head movement: %d cylinders\n", total);
}

static void run_fcfs(void)
{
    int path[MAX_REQ + 1], len = 0;
    path[len++] = head;
    for (int i = 0; i < nreq; i++)
        path[len++] = requests[i];
    print_path("FCFS", path, len);
}

static void run_sstf(void)
{
    int visited[MAX_REQ] = {0}, path[MAX_REQ + 1], len = 0, cur = head;
    path[len++] = head;
    for (int served = 0; served < nreq; served++) {
        int best = -1;
        for (int i = 0; i < nreq; i++)
            if (!visited[i] &&
                (best < 0 || abs(requests[i] - cur) < abs(requests[best] - cur)))
                best = i;
        visited[best] = 1;
        cur = requests[best];
        path[len++] = cur;
    }
    print_path("SSTF (Shortest Seek Time First)", path, len);
}

static int cmp_asc(const void *a, const void *b)
{
    return *(const int *)a - *(const int *)b;
}

static void run_scan(void)
{
    int sorted[MAX_REQ], path[MAX_REQ + 3], len = 0;
    for (int i = 0; i < nreq; i++)
        sorted[i] = requests[i];
    qsort(sorted, nreq, sizeof(int), cmp_asc);

    /* Head moves up to the end of the disk, then reverses to the lowest request. */
    path[len++] = head;
    for (int i = 0; i < nreq; i++)
        if (sorted[i] >= head)
            path[len++] = sorted[i];
    path[len++] = cylinders - 1;
    for (int i = nreq - 1; i >= 0; i--)
        if (sorted[i] < head)
            path[len++] = sorted[i];
    print_path("SCAN (elevator, moving up first)", path, len);
}

int main(void)
{
    printf("Enter number of cylinders (e.g. 200): ");
    if (scanf("%d", &cylinders) != 1 || cylinders < 2) {
        fprintf(stderr, "Invalid number of cylinders.\n");
        return 1;
    }
    printf("Enter initial head position (0-%d): ", cylinders - 1);
    if (scanf("%d", &head) != 1 || head < 0 || head >= cylinders) {
        fprintf(stderr, "Invalid head position.\n");
        return 1;
    }
    printf("Enter number of requests (1-%d): ", MAX_REQ);
    if (scanf("%d", &nreq) != 1 || nreq < 1 || nreq > MAX_REQ) {
        fprintf(stderr, "Invalid number of requests.\n");
        return 1;
    }
    printf("Enter the %d request cylinders: ", nreq);
    for (int i = 0; i < nreq; i++)
        if (scanf("%d", &requests[i]) != 1 || requests[i] < 0 ||
            requests[i] >= cylinders) {
            fprintf(stderr, "Request cylinders must be in 0-%d.\n", cylinders - 1);
            return 1;
        }

    run_fcfs();
    run_sstf();
    run_scan();
    return 0;
}
