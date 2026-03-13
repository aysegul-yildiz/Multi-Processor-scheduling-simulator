// Beyza Ural-22003194
// Ayşegül Yıldız-22002591
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/time.h>
#include <getopt.h>
#include <string.h>
#include <math.h>

#define MAX_PROCESSES 100
#define MAX_PROCESSORS 64
FILE *output_fp = NULL;

typedef struct Burst
{
    int pid;
    int burst_time;
    long long arrival_time;
    long long finish_time;
    long long turnaround_time;
    long long waiting_time;
    unsigned long cpu_id;
    struct Burst *next;
} Burst;

typedef struct ReadyQueue
{
    Burst *queue_head;
    int queue_size;
    pthread_mutex_t lock;
    pthread_cond_t cond;
} ReadyQueue;

ReadyQueue ready_queue[MAX_PROCESSORS];
ReadyQueue single_queue;
Burst *finished_bursts[MAX_PROCESSES];
int select_sch_algorithm = 0; // 0 FCFS, 1  SJF
int check_multiQueue = 0;     // multi 0 single 1
int queueu_count = 0;
int finished_count = 0;
pthread_mutex_t finished_lock;
int jobs_remaining;
pthread_mutex_t jobs_lock = PTHREAD_MUTEX_INITIALIZER;
int numberofprocessors = 1;
char scheduling_approach = 'S';
char queue_selection_method[3] = "NA";
char input_file[50] = "";
int output_selection = 1;
char output_file[50] = "";
int mean_interarrival_time, min_interarrival_time, max_interarrival_time;
int mean_burst_length, min_burst_length, max_burst_length;
int num_bursts_to_generate = 0;

int generate_exponential_random(int mean, int min, int max)
{
    double lambda = 1.0 / mean;
    int value;

    do
    {
        double rand_num = (double)rand() / RAND_MAX;
        value = (int)(-log(1 - rand_num) / lambda);
    } while (value < min || value > max);

    return value;
}

long long current_time_ms()
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (long long)(tv.tv_sec) * 1000 + (long long)(tv.tv_usec) / 1000;
}

void cleanup()
{
    pthread_mutex_destroy(&single_queue.lock);
    pthread_cond_destroy(&single_queue.cond);
    for (int i = 0; i < numberofprocessors; i++)
    {
        pthread_mutex_destroy(&ready_queue[i].lock);
        pthread_cond_destroy(&ready_queue[i].cond);
    }
    if (output_fp != stdout)
    {
        fclose(output_fp);
    }
}

void *threads_function(void *arg)
{
    ReadyQueue *queue;
    if (scheduling_approach == 'S')
    {
        queue = &single_queue;
    }
    else
    {
        queue = (ReadyQueue *)arg;
    }

    while (1)
    {
        pthread_mutex_lock(&queue->lock);

        while (queue->queue_head == NULL && jobs_remaining > 0)
        {
            pthread_cond_wait(&queue->cond, &queue->lock);
        }
        // tüm işler bittiyse çık
        if (jobs_remaining == 0 && queue->queue_head == NULL)
        {
            pthread_mutex_unlock(&queue->lock);
            break;
        }
        // printf("BURADAYIM 2\n");

        Burst *bursts_selected = queue->queue_head;
        queue->queue_head = queue->queue_head->next;
        pthread_mutex_unlock(&queue->lock);

        // printf("BURADAYIM 3\n");

        bursts_selected->cpu_id = pthread_self();

        bursts_selected->arrival_time = current_time_ms();
        if (output_selection == 2 || output_selection == 3)
        {
            fprintf(output_fp, "Busrt picked: time=%lld ms, cpu=%lu, pid=%d, burstlen=%d\n",
                    current_time_ms(), pthread_self(), bursts_selected->pid, bursts_selected->burst_time);
        }
        usleep(bursts_selected->burst_time * 1000);
        bursts_selected->finish_time = current_time_ms();
        bursts_selected->turnaround_time = bursts_selected->finish_time - bursts_selected->arrival_time;
        bursts_selected->waiting_time = bursts_selected->turnaround_time - bursts_selected->burst_time;

        if (output_selection == 3)
        {
            fprintf(output_fp, "Burst finished: CPU=%lu, PID=%d, Arrival=%lld, Finish=%lld, Waiting=%lld, Turnaround=%lld\n",
                    pthread_self(), bursts_selected->pid, bursts_selected->arrival_time, bursts_selected->finish_time,
                    bursts_selected->waiting_time, bursts_selected->turnaround_time);
        }

        // printf("BURADAYIM 4\n");

        pthread_mutex_lock(&finished_lock);
        finished_bursts[finished_count++] = bursts_selected;
        pthread_mutex_unlock(&finished_lock);

        // printf("BURADAYIM 5\n");

        pthread_mutex_lock(&jobs_lock);
        jobs_remaining--;
        if (jobs_remaining == 0)
        {
            if (scheduling_approach == 'S')
            {
                pthread_cond_broadcast(&single_queue.cond);
            }
            else
            {
                for (int i = 0; i < numberofprocessors; i++)
                {
                    pthread_cond_broadcast(&ready_queue[i].cond);
                }
            }
        }

        pthread_mutex_unlock(&jobs_lock);
        // printf("BURADAYIM 6\n");
    }
    return NULL;
}

void addBurst_toQueue(Burst *burst)
{

    ReadyQueue *queue;
    int selectedQueue = 0;

    if (scheduling_approach == 'S')
    {
        queue = &single_queue;
    }
    else if (scheduling_approach == 'M')
    {
        if (strcmp(queue_selection_method, "RM") == 0)
        {
            selectedQueue = (burst->pid - 1) % numberofprocessors;
        }
        else if (strcmp(queue_selection_method, "LM") == 0)
        {
            int least_load = ready_queue[0].queue_size;
            selectedQueue = 0;
            for (int i = 1; i < numberofprocessors; i++)
            {
                if (ready_queue[i].queue_size < least_load)
                {
                    least_load = ready_queue[i].queue_size;
                    selectedQueue = i;
                }
            }
        }
        queue = &ready_queue[selectedQueue];
    }

    pthread_mutex_lock(&queue->lock);
    if (queue->queue_head == NULL)
    {
        queue->queue_head = burst;
    }
    else
    {
        Burst *tmp = queue->queue_head;
        while (tmp->next != NULL)
            tmp = tmp->next;
        tmp->next = burst;
    }
    queue->queue_size++;
    if (output_selection == 3)
    {
        fprintf(output_fp, "Added burst with PID %d, burst time %d to processor %d queue\n", burst->pid, burst->burst_time, selectedQueue);
    }
    pthread_cond_signal(&queue->cond);
    pthread_mutex_unlock(&queue->lock);
}
void createThreads(int num_processors)
{
    int i;
    pthread_t threads[MAX_PROCESSORS];

    if (scheduling_approach == 'S')
    {
        pthread_mutex_init(&single_queue.lock, NULL);
        pthread_cond_init(&single_queue.cond, NULL);
    }
    else
    {
        for (i = 0; i < num_processors; i++)
        {
            pthread_mutex_init(&ready_queue[i].lock, NULL);
            pthread_cond_init(&ready_queue[i].cond, NULL);
        }
    }

    for (i = 0; i < num_processors; i++)
    {
        int result;
        if (scheduling_approach == 'S')
        {
            result = pthread_create(&threads[i], NULL, threads_function, &single_queue);
        }
        else
        {
            result = pthread_create(&threads[i], NULL, threads_function, &ready_queue[i]);
        }
        if (result != 0)
        {
            fprintf(stderr, "error creating thread %d\n", i);
            cleanup();
            exit(EXIT_FAILURE);
        }
    }

    for (i = 0; i < num_processors; i++)
    {
        int result = pthread_join(threads[i], NULL);
        if (result != 0)
        {
            fprintf(stderr, "Error joining thread %d\n", i);
            exit(EXIT_FAILURE);
        }
    }
}

void readFile(const char *filename)
{
    FILE *fp = fopen(filename, "r");
    if (fp == NULL)
    {
        perror("file not opened");
        exit(EXIT_FAILURE);
    }
    char line[256];
    int pid_counter = 1;
    int burst_count = 0;
    while (fgets(line, sizeof(line), fp))
    {
        if (strncmp(line, "PL", 2) == 0)
        {
            int burst_time;
            sscanf(line, "PL %d", &burst_time);

            Burst *created_burst = (Burst *)malloc(sizeof(Burst));
            created_burst->pid = pid_counter++;
            created_burst->burst_time = burst_time;
            created_burst->arrival_time = 0;
            created_burst->finish_time = 0;
            created_burst->turnaround_time = 0;
            created_burst->waiting_time = 0;
            created_burst->next = NULL;

            addBurst_toQueue(created_burst);

            burst_count++;
        }
        else if (strncmp(line, "IAT", 3) == 0)
        {
            int interarrival_time;
            sscanf(line, "IAT %d", &interarrival_time);
            usleep(interarrival_time * 1000);
        }
    }
    fclose(fp);

    pthread_mutex_lock(&jobs_lock);
    jobs_remaining = burst_count;
    pthread_mutex_unlock(&jobs_lock);
    if (output_selection == 3)
    {
        fprintf(output_fp, "total jobs read: %d\n", burst_count);
    }
}
void show_output()
{
    for (int i = 0; i < finished_count - 1; i++)
    {
        for (int j = i + 1; j < finished_count; j++)
        {
            if (finished_bursts[i]->pid > finished_bursts[j]->pid)
            {
                Burst *temp = finished_bursts[i];
                finished_bursts[i] = finished_bursts[j];
                finished_bursts[j] = temp;
            }
        }
    }

    fprintf(output_fp, "%-5s %-18s %-10s %-12s %-12s %-15s %-12s\n",
            "pid", "cpu", "burstlen", "arv", "finish", "waitingtime", "turnaround");

    long long total_turnaround = 0;
    for (int i = 0; i < finished_count; i++)
    {
        Burst *burst = finished_bursts[i];
        fprintf(output_fp, "%-5d %-18lu %-10d %-12lld %-12lld %-15lld %-12lld\n",
                burst->pid, burst->cpu_id, burst->burst_time, burst->arrival_time,
                burst->finish_time, burst->waiting_time, burst->turnaround_time);
        total_turnaround += burst->turnaround_time;
    }

    fprintf(output_fp, "average turnaround time: %lld ms\n", total_turnaround / finished_count);
}
void generate_random_bursts()
{
    for (int i = 0; i < num_bursts_to_generate; i++)
    {
        int burst_length = generate_exponential_random(mean_burst_length, min_burst_length, max_burst_length);

        if (output_selection == 3)
        {
            fprintf(output_fp, "generated Burst %d length: %d ms\n", i + 1, burst_length);
        }

        Burst *created_burst = (Burst *)malloc(sizeof(Burst));
        created_burst->pid = i + 1;
        created_burst->burst_time = burst_length;
        created_burst->arrival_time = current_time_ms();
        created_burst->finish_time = 0;
        created_burst->turnaround_time = 0;
        created_burst->waiting_time = 0;
        created_burst->next = NULL;

        addBurst_toQueue(created_burst);

        if (i < num_bursts_to_generate - 1)
        {
            int interarrival_time = generate_exponential_random(mean_interarrival_time, min_interarrival_time, max_interarrival_time);
            usleep(interarrival_time * 1000);
        }
    }

    pthread_mutex_lock(&jobs_lock);
    jobs_remaining = num_bursts_to_generate;

    pthread_mutex_unlock(&jobs_lock);
}

int main(int argc, char *argv[])
{
    int letters;

    while ((letters = getopt(argc, argv, "n:a:s:i:r:m:o:")) != -1)
    {
        switch (letters)
        {
        case 'n': // process sayısı
            numberofprocessors = atoi(optarg);
            if (numberofprocessors < 1 || numberofprocessors > MAX_PROCESSES)
            {
                return EXIT_FAILURE;
            }
            break;
        case 'a':
            if (optarg[0] == 'S')
            {
                scheduling_approach = 'S';
                strcpy(queue_selection_method, "NA");
            }
            else if (optarg[0] == 'M')
            {
                scheduling_approach = 'M';
                if (optind < argc && (strcmp(argv[optind], "RM") == 0 || strcmp(argv[optind], "LM") == 0))
                {
                    strcpy(queue_selection_method, argv[optind]);
                    optind++;
                }
                else
                {
                    fprintf(stderr, "error: Expected queue selection method 'RM' or 'LM' after -a M\n");
                    return EXIT_FAILURE;
                }
            }
            break;

        case 's':
            if (strcmp(optarg, "FCFS") == 0)
            {
                select_sch_algorithm = 0;
            }
            else if (strcmp(optarg, "SJF") == 0)
            {
                select_sch_algorithm = 1;
            }
            break;

        case 'i':
            strcpy(input_file, optarg);
            break;
        case 'r':

            if (optind + 6 < argc)
            {
                optind--;
                mean_interarrival_time = atoi(argv[optind]);

                optind++;
                min_interarrival_time = atoi(argv[optind]);

                optind++;
                max_interarrival_time = atoi(argv[optind]);

                optind++;
                mean_burst_length = atoi(argv[optind]);

                optind++;
                min_burst_length = atoi(argv[optind]);

                optind++;
                max_burst_length = atoi(argv[optind]);

                optind++;
                num_bursts_to_generate = atoi(argv[optind]);

                optind++;
            }
            else
            {
                fprintf(stderr, "error: missing parameters for -r \n");
                return EXIT_FAILURE;
            }
            break;

        case 'm':
            output_selection = atoi(optarg);
            break;

        case 'o':
            strcpy(output_file, optarg);
            output_fp = fopen(output_file, "w");
            if (output_fp == NULL)
            {
                perror("failed to open output file");
                return EXIT_FAILURE;
            }
            break;

        default:
            fprintf(stderr, "Usage: %s -n N -a SAP QS -s ALG [-i INFILE | -r T T1 T2 L L1 L2 PC] [-m OUTMODE] [-o OUTFILE]\n", argv[0]);
            return EXIT_FAILURE;
        }
    }
    if (output_fp == NULL)
    {
        output_fp = stdout;
    }
    if (num_bursts_to_generate > 0)
    {
        generate_random_bursts();
    }
    else if (strlen(input_file) > 0)
    {
        readFile(input_file);
    }

    createThreads(numberofprocessors);
    show_output();
    fprintf(output_fp, "Simulation completed\n");
    cleanup();
    for (int i = 0; i < finished_count; i++)
    {
        free(finished_bursts[i]);
    }

    return EXIT_SUCCESS;
}
