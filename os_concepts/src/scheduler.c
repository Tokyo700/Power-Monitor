/*
 * scheduler.c - CPU scheduling simulator: FCFS, SJF (non-preemptive), Round Robin.
 * OS concept: Unit 2 - Process Management (CPU scheduling algorithms).
 */
#include <stdio.h>
#include <string.h>

#define MAX_PROCESSES 20
#define MAX_SEGMENTS  500

typedef struct {
    int pid;
    int arrival;
    int burst;
    int remaining;
    int completion;
    int finished;
} Process;

typedef struct {
    int pid;
    int start;
    int end;
} Segment;

static Segment gantt[MAX_SEGMENTS];
static int gantt_count;

static void add_segment(int pid, int start, int end)
{
    /* Merge with the previous slice if the same process keeps the CPU. */
    if (gantt_count > 0 && gantt[gantt_count - 1].pid == pid &&
        gantt[gantt_count - 1].end == start) {
        gantt[gantt_count - 1].end = end;
    } else if (gantt_count < MAX_SEGMENTS) {
        gantt[gantt_count].pid = pid;
        gantt[gantt_count].start = start;
        gantt[gantt_count].end = end;
        gantt_count++;
    }
}

static void reset(Process dst[], const Process src[], int n)
{
    for (int i = 0; i < n; i++) {
        dst[i] = src[i];
        dst[i].remaining = src[i].burst;
        dst[i].finished = 0;
        dst[i].completion = 0;
    }
}

static void print_report(const char *name, const Process p[], int n)
{
    double total_wt = 0, total_tat = 0;

    printf("\n--- %s ---\n", name);
    printf("PID\tArrival\tBurst\tCompletion\tTurnaround\tWaiting\n");
    for (int i = 0; i < n; i++) {
        int tat = p[i].completion - p[i].arrival;
        int wt = tat - p[i].burst;
        total_tat += tat;
        total_wt += wt;
        printf("P%d\t%d\t%d\t%d\t\t%d\t\t%d\n", p[i].pid, p[i].arrival,
               p[i].burst, p[i].completion, tat, wt);
    }
    printf("Average waiting time:    %.2f\n", total_wt / n);
    printf("Average turnaround time: %.2f\n", total_tat / n);

    printf("Gantt chart: ");
    for (int i = 0; i < gantt_count; i++)
        printf("| P%d ", gantt[i].pid);
    printf("|\nTimeline:    ");
    for (int i = 0; i < gantt_count; i++)
        printf("%d    ", gantt[i].start);
    if (gantt_count > 0)
        printf("%d", gantt[gantt_count - 1].end);
    printf("\n");
}

static void run_fcfs(Process p[], int n)
{
    /* Sort by arrival time (bubble sort, keeps input order for ties). */
    for (int i = 0; i < n - 1; i++)
        for (int j = 0; j < n - 1 - i; j++)
            if (p[j].arrival > p[j + 1].arrival) {
                Process tmp = p[j];
                p[j] = p[j + 1];
                p[j + 1] = tmp;
            }

    int time = 0;
    gantt_count = 0;
    for (int i = 0; i < n; i++) {
        if (time < p[i].arrival)
            time = p[i].arrival;          /* CPU idle until the process arrives */
        add_segment(p[i].pid, time, time + p[i].burst);
        time += p[i].burst;
        p[i].completion = time;
    }
    print_report("FCFS (First Come First Served)", p, n);
}

static void run_sjf(Process p[], int n)
{
    int time = 0, done = 0;
    gantt_count = 0;

    while (done < n) {
        int best = -1;
        for (int i = 0; i < n; i++)
            if (!p[i].finished && p[i].arrival <= time &&
                (best < 0 || p[i].burst < p[best].burst))
                best = i;

        if (best < 0) {                   /* nothing ready: jump to next arrival */
            int next = -1;
            for (int i = 0; i < n; i++)
                if (!p[i].finished && (next < 0 || p[i].arrival < p[next].arrival))
                    next = i;
            time = p[next].arrival;
            continue;
        }

        add_segment(p[best].pid, time, time + p[best].burst);
        time += p[best].burst;
        p[best].completion = time;
        p[best].finished = 1;
        done++;
    }
    print_report("SJF (Shortest Job First, non-preemptive)", p, n);
}

/* Put every process that has arrived by 'time' into the ready queue. */
static void enqueue_arrivals(int time, int queue[], int *tail, int queued[],
                             const Process p[], int n)
{
    for (int i = 0; i < n; i++)
        if (!queued[i] && p[i].arrival <= time) {
            queue[*tail] = i;
            *tail = (*tail + 1) % (MAX_PROCESSES + 1);
            queued[i] = 1;
        }
}

static void run_rr(Process p[], int n, int quantum)
{
    int queue[MAX_PROCESSES + 1];
    int queued[MAX_PROCESSES] = {0};
    int head = 0, tail = 0, finished = 0, time = 0;
    gantt_count = 0;

    enqueue_arrivals(time, queue, &tail, queued, p, n);
    while (finished < n) {
        if (head == tail) {               /* ready queue empty: CPU idle */
            int next = -1;
            for (int i = 0; i < n; i++)
                if (!queued[i] && (next < 0 || p[i].arrival < p[next].arrival))
                    next = i;
            time = p[next].arrival;
            enqueue_arrivals(time, queue, &tail, queued, p, n);
            continue;
        }

        int i = queue[head];
        head = (head + 1) % (MAX_PROCESSES + 1);

        int run = p[i].remaining < quantum ? p[i].remaining : quantum;
        add_segment(p[i].pid, time, time + run);
        time += run;
        p[i].remaining -= run;

        /* New arrivals join the queue before the preempted process. */
        enqueue_arrivals(time, queue, &tail, queued, p, n);

        if (p[i].remaining == 0) {
            p[i].completion = time;
            p[i].finished = 1;
            finished++;
        } else {
            queue[tail] = i;
            tail = (tail + 1) % (MAX_PROCESSES + 1);
        }
    }

    char title[64];
    snprintf(title, sizeof title, "Round Robin (time quantum = %d)", quantum);
    print_report(title, p, n);
}

int main(void)
{
    Process base[MAX_PROCESSES], work[MAX_PROCESSES];
    int n, quantum;

    printf("Enter number of processes (1-%d): ", MAX_PROCESSES);
    if (scanf("%d", &n) != 1 || n < 1 || n > MAX_PROCESSES) {
        fprintf(stderr, "Invalid number of processes.\n");
        return 1;
    }

    for (int i = 0; i < n; i++) {
        base[i].pid = i + 1;
        printf("P%d arrival time: ", i + 1);
        if (scanf("%d", &base[i].arrival) != 1 || base[i].arrival < 0) {
            fprintf(stderr, "Invalid arrival time.\n");
            return 1;
        }
        printf("P%d burst time: ", i + 1);
        if (scanf("%d", &base[i].burst) != 1 || base[i].burst <= 0) {
            fprintf(stderr, "Burst time must be a positive number.\n");
            return 1;
        }
    }

    printf("Time quantum for Round Robin: ");
    if (scanf("%d", &quantum) != 1 || quantum <= 0) {
        fprintf(stderr, "Time quantum must be a positive number.\n");
        return 1;
    }

    reset(work, base, n);
    run_fcfs(work, n);

    reset(work, base, n);
    run_sjf(work, n);

    reset(work, base, n);
    run_rr(work, n, quantum);

    return 0;
}
