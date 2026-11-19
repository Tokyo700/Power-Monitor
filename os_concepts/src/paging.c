/*
 * paging.c - Page replacement simulator: FIFO, LRU and Optimal.
 * Counts page faults and prints a frame-by-frame trace.
 * OS concept: Unit 4 - Virtual memory, paging, demand paging, page
 * replacement algorithms, frame allocation.
 */
#include <stdio.h>

#define MAX_REFS   200
#define MAX_FRAMES 20

enum { FIFO, LRU, OPTIMAL, ALGO_COUNT };
static const char *algo_name[ALGO_COUNT] = {"FIFO", "LRU", "Optimal"};

static int refs[MAX_REFS];
static int nrefs;
static int nframes;

/* Index of the next reference to 'page' after time t, or nrefs if never used. */
static int next_use(int page, int t)
{
    for (int s = t + 1; s < nrefs; s++)
        if (refs[s] == page)
            return s;
    return nrefs;
}

static int simulate(int algo, int verbose)
{
    int frame[MAX_FRAMES], last_used[MAX_FRAMES];
    int fifo_next = 0, faults = 0;

    for (int f = 0; f < nframes; f++) {
        frame[f] = -1;
        last_used[f] = -1;
    }

    if (verbose) {
        printf("\n--- %s ---\n", algo_name[algo]);
        printf("Ref ");
        for (int f = 0; f < nframes; f++)
            printf(" F%-3d", f);
        printf(" Result\n");
    }

    for (int t = 0; t < nrefs; t++) {
        int page = refs[t], hit = -1, fault = 0;

        for (int f = 0; f < nframes; f++)
            if (frame[f] == page)
                hit = f;

        if (hit >= 0) {
            last_used[hit] = t;
        } else {
            fault = 1;
            faults++;

            int victim = -1;
            for (int f = 0; f < nframes; f++)     /* free frame first */
                if (frame[f] == -1) { victim = f; break; }

            if (victim < 0) {
                if (algo == FIFO) {
                    victim = fifo_next;
                    fifo_next = (fifo_next + 1) % nframes;
                } else if (algo == LRU) {
                    victim = 0;
                    for (int f = 1; f < nframes; f++)
                        if (last_used[f] < last_used[victim])
                            victim = f;
                } else {                            /* OPTIMAL */
                    int farthest = -1;
                    victim = 0;
                    for (int f = 0; f < nframes; f++) {
                        int nu = next_use(frame[f], t);
                        if (nu > farthest) {
                            farthest = nu;
                            victim = f;
                        }
                    }
                }
            }
            frame[victim] = page;
            last_used[victim] = t;
        }

        if (verbose) {
            printf("%3d ", page);
            for (int f = 0; f < nframes; f++) {
                if (frame[f] < 0)
                    printf("  -   ");
                else
                    printf("  %-4d", frame[f]);
            }
            printf(" %s\n", fault ? "FAULT" : "hit");
        }
    }
    return faults;
}

int main(void)
{
    printf("Enter number of page references (1-%d): ", MAX_REFS);
    if (scanf("%d", &nrefs) != 1 || nrefs < 1 || nrefs > MAX_REFS) {
        fprintf(stderr, "Invalid number of references.\n");
        return 1;
    }
    printf("Enter the reference string (%d page numbers): ", nrefs);
    for (int i = 0; i < nrefs; i++)
        if (scanf("%d", &refs[i]) != 1 || refs[i] < 0) {
            fprintf(stderr, "Page numbers must be non-negative integers.\n");
            return 1;
        }
    printf("Enter number of frames (1-%d): ", MAX_FRAMES);
    if (scanf("%d", &nframes) != 1 || nframes < 1 || nframes > MAX_FRAMES) {
        fprintf(stderr, "Invalid number of frames.\n");
        return 1;
    }

    int faults[ALGO_COUNT];
    for (int a = 0; a < ALGO_COUNT; a++)
        faults[a] = simulate(a, 1);

    printf("\n--- Summary (page faults) ---\n");
    for (int a = 0; a < ALGO_COUNT; a++)
        printf("%-8s %d\n", algo_name[a], faults[a]);
    return 0;
}
