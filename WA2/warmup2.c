#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdbool.h>
#include <unistd.h>
#include <math.h>
#include <time.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <pthread.h>
#include <signal.h>

#include "my402list.h"

#define MAX_LINE_LENGTH 1025
#define MAX_VALUE 2147483647
#define MAX_INTERVAL_MS 10000
#define MAX_TIMESTAMP_MS 100000000 

typedef struct 
{
    int id;
    int tokens_needed;
    long long service_time_requested;
    long long inter_arrival_time;
    struct timeval arrival_time;
    struct timeval q1_enter_time;
    struct timeval q1_leave_time;
    struct timeval q2_enter_time;
    struct timeval q2_leave_time;
    struct timeval service_start_time;
    struct timeval service_end_time;
} Packet;

My402List q1, q2;
pthread_t packet_thread, token_thread, s1_thread, s2_thread, sig_thread;
pthread_mutex_t m;
pthread_cond_t cv;

int num;
int B;
int P;
double lambda;
double mu;
double r;

int produced;
int remained;
int served;
int dropped;
int tokens_in_bucket;
int tokens_produced;
int tokens_dropped;
char time_str[15];

int is_deterministic;
FILE* fp;

struct timeval start_time;
struct timeval end_time;
struct timeval diff;

double average_inter_arrival_time;
double average_service_time;
struct timeval total_time_in_q1;
struct timeval total_time_in_q2;
struct timeval total_time_in_s1;
struct timeval total_time_in_s2;
double average_time_in_system;
double average_square_time;

sigset_t mask;

void get_time(struct timeval* start, struct timeval* end, struct timeval* diff, char* time_str, bool is_timestamp)
{
    timersub(end, start, diff);
    if(diff->tv_sec >= MAX_TIMESTAMP_MS / 1000 || diff->tv_sec * 1000 + diff->tv_usec / 1000 >= MAX_TIMESTAMP_MS)
    {
        sprintf(time_str, "????????.???ms");
    }
    else
    {
        if(is_timestamp)
            sprintf(time_str, "%08ld.%03ldms", diff->tv_sec * 1000 + diff->tv_usec / 1000, diff->tv_usec % 1000);
        else
            sprintf(time_str, "%ld.%03ldms", diff->tv_sec * 1000 + diff->tv_usec / 1000, diff->tv_usec % 1000);
    }
}

void readintopacket(FILE* fp, Packet* packet)
{
    char buf[MAX_LINE_LENGTH];
    if(fgets(buf, sizeof(buf), fp) == NULL)
    {
        if(ferror(fp))
        {
            perror("Error reading the file");
            fclose(fp);
            free(packet);
            exit(1);
        }
        else
        {
            fprintf(stderr, "Error: Line %d missing.\n", packet->id+1);
            fclose(fp);
            free(packet);
            exit(1);
        }
    }
    if(buf[strlen(buf) - 1] != '\n')
    {
        fprintf(stderr, "Error: Line %d is too long.\n", packet->id+1);
        fclose(fp);
        free(packet);
        exit(1);
    }
    buf[strlen(buf) - 1] = '\0';
    if(strlen(buf) == 0 || isspace(buf[0]) || isspace(buf[strlen(buf) - 1]))
    {
        fprintf(stderr, "Error: Line %d is not in the correct format.\n", packet->id+1);
        fclose(fp);
        free(packet);
        exit(1);
    }
    if(sscanf(buf, "%lld %d %lld", &packet->inter_arrival_time, &packet->tokens_needed, &packet->service_time_requested) != 3)
    {
        fprintf(stderr, "Error: Line %d cannot be converted to three intergers.\n", packet->id+1);
        fclose(fp);
        free(packet);
        exit(1);
    }
    if(packet->inter_arrival_time < 0 || packet->inter_arrival_time < 0 || packet->service_time_requested < 0)
    {
        fprintf(stderr, "Error: Line %d has negative values.\n", packet->id+1);
        fclose(fp);
        free(packet);
        exit(1);
    }
}

void* packet_func(void* arg)
{
    pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);
    Packet* packet;
    struct timeval prev_arrival_time = start_time;
    int id = 1;
    while(remained > 0)
    {
        packet = (Packet*)malloc(sizeof(Packet));
        packet->id = id;
        if(!is_deterministic)
        {
            readintopacket(fp, packet);
        }
        else
        {
            if(1000 / lambda > MAX_INTERVAL_MS)
                packet->inter_arrival_time = MAX_INTERVAL_MS;
            else
                packet->inter_arrival_time = (int)round(1000 / lambda);
            if(1000 / mu > MAX_INTERVAL_MS)
                packet->service_time_requested = MAX_INTERVAL_MS;
            else
                packet->service_time_requested = (int)round(1000 / mu);
            packet->tokens_needed = P;
        }
        pthread_cleanup_push(free, packet);
        pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
        if(packet->inter_arrival_time > 0)
            usleep(packet->inter_arrival_time * 1000);
        pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);
        pthread_cleanup_pop(0);
        pthread_mutex_lock(&m);
        if(remained == 0)
        {
            pthread_mutex_unlock(&m);
            free(packet);
            break;
        }
        gettimeofday(&packet->arrival_time, NULL);
        get_time(&start_time, &packet->arrival_time, &diff, time_str, true);
        fprintf(stdout, "%s: p%d arrives, needs %d ", time_str, packet->id, packet->tokens_needed);
        if(packet->tokens_needed == 1 || packet->tokens_needed == 0)
            fprintf(stdout, "token, inter-arrival time = ");
        else
            fprintf(stdout, "tokens, inter-arrival time = ");
        get_time(&prev_arrival_time, &packet->arrival_time, &diff, time_str, false);
        fprintf(stdout, "%s", time_str);
        prev_arrival_time = packet->arrival_time;
        produced++;
        remained--;
        id++;
        average_inter_arrival_time = (average_inter_arrival_time * (produced - 1) + diff.tv_sec + (double)diff.tv_usec / 1e6) / produced;
        if(packet->tokens_needed > B)
        {
            fprintf(stdout, ", dropped\n");
            free(packet);
            dropped++;
            if(remained == 0 && My402ListEmpty(&q1))
            {
                pthread_cond_broadcast(&cv);
            }
        }
        else
        {
            fprintf(stdout, "\n");
            gettimeofday(&packet->q1_enter_time, NULL);
            get_time(&start_time, &packet->q1_enter_time, &diff, time_str, true);
            fprintf(stdout, "%s: p%d enters Q1\n", time_str, packet->id);
            My402ListAppend(&q1, packet);
            if(My402ListLength(&q1) == 1 && packet->tokens_needed <= tokens_in_bucket)
            {
                My402ListUnlink(&q1, My402ListFirst(&q1));
                tokens_in_bucket -= packet->tokens_needed;
                gettimeofday(&packet->q1_leave_time, NULL);
                get_time(&start_time, &packet->q1_leave_time, &diff, time_str, true);
                fprintf(stdout, "%s: p%d leaves Q1, time in Q1 = ", time_str, packet->id);
                get_time(&packet->q1_enter_time, &packet->q1_leave_time, &diff, time_str, false);
                fprintf(stdout, "%s, token bucket now has %d ", time_str, tokens_in_bucket);
                if(tokens_in_bucket == 1 || tokens_in_bucket == 0)
                    fprintf(stdout, "token\n");
                else
                    fprintf(stdout, "tokens\n");
                My402ListAppend(&q2, packet);
                gettimeofday(&packet->q2_enter_time, NULL);
                get_time(&start_time, &packet->q2_enter_time, &diff, time_str, true);
                fprintf(stdout, "%s: p%d enters Q2\n", time_str, packet->id);
                pthread_cond_broadcast(&cv);
            }
        }
        pthread_mutex_unlock(&m);
    }
    return 0;
}

void* token_func(void* arg)
{
    pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);
    Packet* packet;
    int id = 1;
    int interval;
    if(1000 / r > MAX_INTERVAL_MS)
        interval = MAX_INTERVAL_MS;
    else
        interval = (int)round(1000 / r);
    struct timeval token_arrival_time;
    while(remained > 0 || !My402ListEmpty(&q1))
    {
        pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
        if(interval > 0)
            usleep(interval * 1000);
        pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);
        pthread_mutex_lock(&m);
        if(remained == 0 && My402ListEmpty(&q1))
        {
            pthread_mutex_unlock(&m);
            break;
        }
        gettimeofday(&token_arrival_time, NULL);
        get_time(&start_time, &token_arrival_time, &diff, time_str, true);
        fprintf(stdout, "%s: token t%d arrives, ", time_str, id);
        tokens_produced++;
        id++;
        if(tokens_in_bucket < B)
        {
            tokens_in_bucket++;
            fprintf(stdout, "token bucket now has %d ", tokens_in_bucket);
            if(tokens_in_bucket == 1 || tokens_in_bucket == 0)
                fprintf(stdout, "token\n");
            else
                fprintf(stdout, "tokens\n");
        }
        else
        {
            tokens_dropped++;
            fprintf(stdout, "dropped\n");
        }
        if(!My402ListEmpty(&q1))
        {
            packet = (Packet*)My402ListFirst(&q1)->obj;
            if(packet->tokens_needed <= tokens_in_bucket)
            {
                My402ListUnlink(&q1, My402ListFirst(&q1));
                tokens_in_bucket -= packet->tokens_needed;
                gettimeofday(&packet->q1_leave_time, NULL);
                get_time(&start_time, &packet->q1_leave_time, &diff, time_str, true);
                fprintf(stdout, "%s: p%d leaves Q1, time in Q1 = ", time_str, packet->id);
                get_time(&packet->q1_enter_time, &packet->q1_leave_time, &diff, time_str, false);
                fprintf(stdout, "%s, token bucket now has %d ", time_str, tokens_in_bucket);
                if(tokens_in_bucket == 1 || tokens_in_bucket == 0)
                    fprintf(stdout, "token\n");
                else
                    fprintf(stdout, "tokens\n");
                My402ListAppend(&q2, packet);
                gettimeofday(&packet->q2_enter_time, NULL);
                get_time(&start_time, &packet->q2_enter_time, &diff, time_str, true);
                fprintf(stdout, "%s: p%d enters Q2\n", time_str, packet->id);
                pthread_cond_broadcast(&cv);
            }
        }
        pthread_mutex_unlock(&m);
    }
    return 0;
}

void* server_func(void* arg)
{
    int server_id = (int)arg;
    while(!My402ListEmpty(&q2) || !My402ListEmpty(&q1) || remained > 0)
    {
        pthread_mutex_lock(&m);
        while(My402ListEmpty(&q2) && (!My402ListEmpty(&q1) || remained > 0))
        {
            pthread_cond_wait(&cv, &m);
        }
        Packet* packet;
        if(My402ListEmpty(&q2))
        {
            pthread_mutex_unlock(&m);
        }
        else
        {
            packet = (Packet*)My402ListFirst(&q2)->obj;
            My402ListUnlink(&q2, My402ListFirst(&q2));
            gettimeofday(&packet->q2_leave_time, NULL);
            get_time(&start_time, &packet->q2_leave_time, &diff, time_str, true);
            fprintf(stdout, "%s: p%d leaves Q2, time in Q2 = ", time_str, packet->id);
            get_time(&packet->q2_enter_time, &packet->q2_leave_time, &diff, time_str, false);
            fprintf(stdout, "%s\n", time_str);
            gettimeofday(&packet->service_start_time, NULL);
            get_time(&start_time, &packet->service_start_time, &diff, time_str, true);
            fprintf(stdout, "%s: p%d begins service at S%d, requesting %lldms of service\n", time_str, packet->id, server_id, packet->service_time_requested);
            if(packet->service_time_requested > 0)
            {
                pthread_mutex_unlock(&m);
                usleep(packet->service_time_requested * 1000);
                pthread_mutex_lock(&m);
            }
            served++;
            gettimeofday(&packet->service_end_time, NULL);
            get_time(&start_time, &packet->service_end_time, &diff, time_str, true);
            fprintf(stdout, "%s: p%d departs from S%d, service time = ", time_str, packet->id, server_id);
            get_time(&packet->service_start_time, &packet->service_end_time, &diff, time_str, false);
            fprintf(stdout, "%s", time_str);
            get_time(&packet->arrival_time, &packet->service_end_time, &diff, time_str, false);
            fprintf(stdout, ", time in system = %s\n", time_str);
            pthread_mutex_unlock(&m);

            average_time_in_system = (average_time_in_system * (served - 1) + diff.tv_sec + (double)diff.tv_usec / 1e6) / served;
            average_square_time = (average_square_time * (served - 1) + pow(diff.tv_sec + (double)diff.tv_usec / 1e6, 2)) / served;
            timersub(&packet->q1_leave_time, &packet->q1_enter_time, &diff);
            timeradd(&total_time_in_q1, &diff, &total_time_in_q1);
            timersub(&packet->q2_leave_time, &packet->q2_enter_time, &diff);
            timeradd(&total_time_in_q2, &diff, &total_time_in_q2);
            timersub(&packet->service_end_time, &packet->service_start_time, &diff);
            average_service_time = (average_service_time * (served - 1) + diff.tv_sec + (double)diff.tv_usec / 1e6) / served;
            if(server_id == 1)
            {
                timeradd(&total_time_in_s1, &diff, &total_time_in_s1);
            }
            else
            {
                timeradd(&total_time_in_s2, &diff, &total_time_in_s2);
            }
        }
    }
    return 0;
}

void* sig_func(void* arg)
{
    int sig;
    // int is_caught = 0;
    // while(1)
    // {
        sigwait(&mask, &sig);
        // if(is_caught)
        //     continue;
        pthread_mutex_lock(&m);
        remained = 0;
        pthread_cancel(packet_thread);
        pthread_cancel(token_thread);
        struct timeval cur_time;
        gettimeofday(&cur_time, NULL);
        get_time(&start_time, &cur_time, &diff, time_str, true);
        fprintf(stdout, "%s: SIGINT caught, no new packets or tokens will be allowed\n", time_str);
        Packet* packet;
        while(!My402ListEmpty(&q1))
        {
            packet = (Packet*)My402ListFirst(&q1)->obj;
            gettimeofday(&cur_time, NULL);
            get_time(&start_time, &cur_time, &diff, time_str, true);
            fprintf(stdout, "%s: p%d removed from Q1\n", time_str, packet->id);
            My402ListUnlink(&q1, My402ListFirst(&q1));
            free(packet);
        }
        while(!My402ListEmpty(&q2))
        {
            packet = (Packet*)My402ListFirst(&q2)->obj;
            gettimeofday(&cur_time, NULL);
            get_time(&start_time, &cur_time, &diff, time_str, true);
            fprintf(stdout, "%s: p%d removed from Q2\n", time_str, packet->id);
            My402ListUnlink(&q2, My402ListFirst(&q2));
            free(packet);
        }
        pthread_cond_broadcast(&cv);
        pthread_mutex_unlock(&m);
        // is_caught = 1;
    // }
    return 0;
}

void Usage()
{
    fprintf(stderr, "Error: malformed command\nUsage: warmup2 [-lambda=positive real number] [-mu=positive real number] [-r=positive real number] [-B=positive integer] [-P=positive integer] [-n=positive integer] [-t=tsfile]\n");
    exit(1);
}

void readfromcmd(int argc, char* argv[])
{
    argc--, argv++;
    if(argc % 2 != 0 || argc > 14 || argc < 0)
        Usage();
    for(int i = 0; i < argc; i += 2)
    {
        if(argv[i][0] != '-')
            Usage();
        if(strcmp(argv[i], "-lambda") == 0)
        {
            if(sscanf(argv[i+1], "%lf", &lambda) != 1 || lambda <= 0)
                Usage();
        }
        else if(strcmp(argv[i], "-mu") == 0)
        {
            if(sscanf(argv[i+1], "%lf", &mu) != 1 || mu <= 0)
                Usage();
        }
        else if(strcmp(argv[i], "-r") == 0)
        {
            if(sscanf(argv[i+1], "%lf", &r) != 1 || r <= 0)
                Usage();
        }
        else if(strcmp(argv[i], "-B") == 0)
        {
            if(sscanf(argv[i+1], "%d", &B) != 1 || B <= 0)
                Usage();
        }
        else if(strcmp(argv[i], "-P") == 0)
        {
            if(sscanf(argv[i+1], "%d", &P) != 1 || P <= 0)
                Usage();
        }
        else if(strcmp(argv[i], "-n") == 0)
        {
            if(sscanf(argv[i+1], "%d", &num) != 1 || num <= 0)
                Usage();
        }
        else if(strcmp(argv[i], "-t") == 0)
        {
            struct stat s;
            if (stat(argv[i+1], &s) == 0) 
            {
                if(S_ISDIR(s.st_mode)) 
                {
                    fprintf(stderr, "Error: %s is a directory.\n", argv[i+1]);
                    exit(1);
                }
            }
            if((fp = fopen(argv[i+1], "r")) == NULL)
            {
                fprintf(stderr, "Error: Cannot open file %s.\n", argv[i+1]);
                perror("fopen");
                exit(1);
            }
            is_deterministic = 0;
        }
        else
            Usage();
    }
}

void initdefault()
{
    lambda = (double)1;
    mu = (double)0.35;
    r = (double)1.5;
    B = 10;
    P = 3;
    num = 20;
    is_deterministic = 1;
}

void readfirstline(FILE* fp)
{
    char buf[MAX_LINE_LENGTH];
    if(fgets(buf, sizeof(buf), fp) == NULL)
    {
        if(ferror(fp))
        {
            perror("fgets");
            fclose(fp);
            exit(1);
        }
        else
        {
            fprintf(stderr, "Error: Line 1 missing.\n");
            fclose(fp);
            exit(1);
        }
    }
    if(buf[strlen(buf) - 1] != '\n')
    {
        fprintf(stderr, "Error: Line 1 is too long.\n");
        fclose(fp);
        exit(1);
    }
    buf[strlen(buf) - 1] = '\0';
    if(strlen(buf) == 0 || isspace(buf[0]) || isspace(buf[strlen(buf) - 1]))
    {
        fprintf(stderr, "Error: Line 1 is not in the correct format.\n");
        fclose(fp);
        exit(1);
    }
    if(sscanf(buf, "%d", &num) != 1 || num <= 0)
    {
        fprintf(stderr, "Error: Line 1 is not a positive interger.\n");
        fclose(fp);
        exit(1);
    }
}

void initemulation()
{
    remained = num;
    produced = 0;
    served = 0;
    dropped = 0;
    tokens_in_bucket = 0;
    tokens_produced = 0;
    tokens_dropped = 0;
    average_inter_arrival_time = 0;
    average_service_time = 0;
    average_time_in_system = 0;
    average_square_time = 0;
    memset(&total_time_in_q1, 0, sizeof(struct timeval));
    memset(&total_time_in_q2, 0, sizeof(struct timeval));
    memset(&total_time_in_s1, 0, sizeof(struct timeval));
    memset(&total_time_in_s2, 0, sizeof(struct timeval));
    memset(&start_time, 0, sizeof(struct timeval));
    memset(&end_time, 0, sizeof(struct timeval));
    memset(&diff, 0, sizeof(struct timeval));
    memset(&time_str, 0, sizeof(char) * 15);
    memset(&q1, 0, sizeof(My402List));
    memset(&q2, 0, sizeof(My402List));
    My402ListInit(&q1);
    My402ListInit(&q2);
    pthread_mutex_init(&m, NULL);
    pthread_cond_init(&cv, NULL);
    sigemptyset(&mask);
    sigaddset(&mask, SIGINT);
    sigprocmask(SIG_BLOCK, &mask, NULL);
}

void endemulation()
{
    pthread_cancel(sig_thread);
    pthread_mutex_destroy(&m);
    pthread_cond_destroy(&cv);
    if(!is_deterministic)
        fclose(fp);
}

void printparams(int argc, char* argv[])
{
    fprintf(stdout, "Emulation Parameters:\n");
    fprintf(stdout, "\tnumber to arrive = %d\n", num);
    if(is_deterministic)
    {
        fprintf(stdout, "\tlambda = %.6g\n", lambda);
        fprintf(stdout, "\tmu = %.6g\n", mu);
    }
    fprintf(stdout, "\tr = %.6g\n", r);
    fprintf(stdout, "\tB = %d\n", B);
    if(is_deterministic)
        fprintf(stdout, "\tP = %d\n", P);
    else
        fprintf(stdout, "\ttsfile = %s\n", argv[argc-1]);
}

void printstats()
{
    fprintf(stdout, "Statistics:\n");
    fprintf(stdout, "\n");
    if(produced == 0)
        fprintf(stdout, "\taverage packet inter-arrival time = N/A (no packet arrived)\n");
    else
        fprintf(stdout, "\taverage packet inter-arrival time = %.6gs\n", average_inter_arrival_time);
    if(served == 0)
        fprintf(stdout, "\taverage packet service time = N/A (no packet served)\n");
    else
        fprintf(stdout, "\taverage packet service time = %.6gs\n", average_service_time);
    fprintf(stdout, "\n");
    timersub(&end_time, &start_time, &diff);
    double total_time = diff.tv_sec + (double)diff.tv_usec / 1e6;
    if(total_time == 0)
    {
        fprintf(stdout, "\taverage number of packets in Q1 = N/A (zero emulation time)\n");
        fprintf(stdout, "\taverage number of packets in Q2 = N/A (zero emulation time)\n");
        fprintf(stdout, "\taverage number of packets at S1 = N/A (zero emulation time)\n");
        fprintf(stdout, "\taverage number of packets at S2 = N/A (zero emulation time)\n");
    }
    else
    {
        fprintf(stdout, "\taverage number of packets in Q1 = %.6g\n", (total_time_in_q1.tv_sec + (double)total_time_in_q1.tv_usec / 1e6) / total_time);
        fprintf(stdout, "\taverage number of packets in Q2 = %.6g\n", (total_time_in_q2.tv_sec + (double)total_time_in_q2.tv_usec / 1e6) / total_time);
        fprintf(stdout, "\taverage number of packets at S1 = %.6g\n", (total_time_in_s1.tv_sec + (double)total_time_in_s1.tv_usec / 1e6) / total_time);
        fprintf(stdout, "\taverage number of packets at S2 = %.6g\n", (total_time_in_s2.tv_sec + (double)total_time_in_s2.tv_usec / 1e6) / total_time);
    }
    fprintf(stdout, "\n");
    if(served == 0)
    {
        fprintf(stdout, "\taverage time a packet spent in system = N/A (no packet served)\n");
        fprintf(stdout, "\tstandard deviation for time spent in system = N/A (no packet served)\n");
    }
    else
    {
        fprintf(stdout, "\taverage time a packet spent in system = %.6gs\n", average_time_in_system);
        fprintf(stdout, "\tstandard deviation for time spent in system = %.6gs\n", sqrt(average_square_time - pow(average_time_in_system, 2)));
    }    
    fprintf(stdout, "\n");
    if(tokens_produced == 0)
        fprintf(stdout, "\ttoken drop probability = N/A (no token produced)\n");
    else
        fprintf(stdout, "\ttoken drop probability = %.6g\n", (double)tokens_dropped / tokens_produced);
    if(produced == 0)
        fprintf(stdout, "\tpacket drop probability = N/A (no packet produced)\n");
    else
        fprintf(stdout, "\tpacket drop probability = %.6g\n", (double)dropped / produced);
}

int main(int argc, char* argv[])
{
    initdefault();
    readfromcmd(argc, argv);
    if(!is_deterministic)
    {
        readfirstline(fp);
    }
    printparams(argc, argv);
    fprintf(stdout, "\n");
    initemulation();
    gettimeofday(&start_time, NULL);
    get_time(&start_time, &start_time, &diff, time_str, true);
    fprintf(stdout, "%s: emulation begins\n", time_str);
    pthread_create(&sig_thread, NULL, sig_func, NULL);
    // pthread_detach(sig_thread);
    pthread_create(&packet_thread, NULL, packet_func, NULL);
    pthread_create(&token_thread, NULL, token_func, NULL);
    pthread_create(&s1_thread, NULL, server_func, (void*)1);
    pthread_create(&s2_thread, NULL, server_func, (void*)2);
    pthread_join(packet_thread, NULL);
    pthread_join(token_thread, NULL);
    pthread_join(s1_thread, NULL);
    pthread_join(s2_thread, NULL);
    gettimeofday(&end_time, NULL);
    get_time(&start_time, &end_time, &diff, time_str, true);
    fprintf(stdout, "%s: emulation ends\n", time_str);
    fprintf(stdout, "\n");
    printstats();
    endemulation();
    return 0;
}


