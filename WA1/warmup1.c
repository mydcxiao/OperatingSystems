#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/time.h>
#include <time.h>
#include <ctype.h>
#include <math.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdbool.h>
#include <sys/stat.h>
#include <errno.h>

#include "my402list.h"

#define MAX_LINE_LENGTH 1025
#define MAX_TIME_LENGTH 11
#define MAX_AMOUNT_DIGIT_BEFORE 7
#define MAX_AMOUNT_DIGIT_AFTER 2
#define MAX_DESCRIPTION_LENGTH 25

typedef struct {
    char type;
    time_t time;
    int amount;
    char description[MAX_DESCRIPTION_LENGTH];
    unsigned line;
}Transaction;

//process tfile
static 
bool process(FILE* fp, My402List* list)
{
    char buf[MAX_LINE_LENGTH];
    unsigned line = 0;
    while(fgets(buf, sizeof(buf), fp) != NULL)
    {
        line++;
        if(buf[strlen(buf) - 1] != '\n')
        {
            fprintf(stderr, "Error: line %d is too long(At most 1024 chars).\n", line);
            return false;
        }
        buf[strlen(buf) - 1] = '\0';

        Transaction* trans = (Transaction*)malloc(sizeof(Transaction));
        trans->line = line;
        // process transaction type
        char* cur = buf;
        char* tab = strchr(cur, '\t');
        if(tab == NULL)
        {
            fprintf(stderr, "Error: line %d has 0 <TAB>(3 expected), missing time, amount, description fields.\n", line);
            free(trans);
            return false;
        }
        *tab++ = '\0';
        if(strlen(cur) != 1 || (cur[0] != '+' && cur[0] != '-'))
        {
            fprintf(stderr, "Error: line %d Transaction type must be '+' or '-'.\n", line);
            free(trans);
            return false;
        }
        else 
        {
            if(cur[0] == '+')
            {
                trans->type = '+';
            }
            else {
                trans->type = '-';
            }
        }
        // process transaction time
        if(*tab == '\0')
        {
            fprintf(stderr, "Error: line %d Missing time, amount, description fields.\n", line);
            free(trans);
            return false;
        }
        cur = tab;
        tab = strchr(cur, '\t');
        if(tab == NULL)
        {
            fprintf(stderr, "Error: line %d has 1 <TAB>(3 expected), missing amount, description fields.\n", line);
            free(trans);
            return false;
        }
        *tab++ = '\0';
        if(strlen(cur) == 0 || strlen(cur) >= MAX_TIME_LENGTH)
        {
            fprintf(stderr, "Error: line %d Transaction time is bad.\n", line);
            free(trans);
            return false;
        }
        if(cur[0] == '0')
        {
            fprintf(stderr, "Error: line %d The first digit of transaction time must not be zero.\n", line);
            free(trans);
            return false;
        }

        int inttime;
        if(sscanf(cur, "%d", &inttime) != 1)
        {
            fprintf(stderr, "Error: line %d Transaction time must be a postive number.\n", line);
            free(trans);
            return false;
        }
        time_t trans_time = inttime;
        time_t curr_time = time(NULL);

        if(trans_time >= curr_time)
        {
            fprintf(stderr, "Error: line %d Transaction time should be smaller than current time.\n", line);
            free(trans);
            return false;
        }
        if(trans_time < 0)
        {
            fprintf(stderr, "Error: line %d Transaction time should not be negative.\n", line);
            free(trans);
            return false;
        }

        trans->time = trans_time;
        // process transaction amount
        if(*tab == '\0')
        {
            fprintf(stderr, "Error: line %d Missing amount, description fields.\n", line);
            free(trans);
            return false;
        }
        cur = tab;
        tab = strchr(cur, '\t');
        if(tab == NULL)
        {
            fprintf(stderr, "Error: line %d has 2 <TAB>(3 expected), missing description field.\n", line);
            free(trans);
            return false;
        }
        *tab++ = '\0';
        if(strlen(cur) < 4 || strlen(cur) > MAX_AMOUNT_DIGIT_BEFORE + MAX_AMOUNT_DIGIT_AFTER + 1)
        {
            fprintf(stderr, "Error: line %d The length of transaction amount is bad(4 <= len <= 10 expected).\n", line);
            free(trans);
            return false;
        }

        char* dot = strchr(cur, '.');
        if(dot == NULL)
        {
            fprintf(stderr, "Error: line %d Transaction amount has no period.\n", line);
            free(trans);
            return false;
        }
        *dot = '\0';
        char* before = cur;
        char* after = dot + 1;
        if(strlen(before) > MAX_AMOUNT_DIGIT_BEFORE)
        {
            fprintf(stderr, "Error: line %d Transaction amount should have at most 7 digits before period.\n", line);
            free(trans);
            return false;
        }
        if(strlen(after) != MAX_AMOUNT_DIGIT_AFTER)
        {
            fprintf(stderr, "Error: line %d Transaction amount should have exactly 2 digits after period.\n", line);
            free(trans);
            return false;
        }
        if(strlen(before) > 1 && before[0] == '0')
        {
            fprintf(stderr, "Error: line %d There must be no leading zero in transaction amount.\n", line);
            free(trans);
            return false;
        }
        int intbefore;
        if(sscanf(before, "%d", &intbefore) != 1)
        {
            fprintf(stderr, "Error: line %d Transaction amount should be a positive number.\n", line);
            free(trans);
            return false;
        }
        if(intbefore < 0)
        {
            fprintf(stderr, "Error: line %d Transaction amount should be a positive number.\n", line);
            free(trans);
            return false;
        }
        int intafter;
        if(sscanf(after, "%d", &intafter) != 1)
        {
            fprintf(stderr, "Error: line %d The last two digits of amount should be a number.\n", line);
            free(trans);
            return false;
        }
        if(intafter < 0)
        {
            fprintf(stderr, "Error: line %d The last two digits of amount should not be negative.\n", line);
            free(trans);
            return false;
        }
        trans->amount = intbefore * 100 + intafter;
        // process transaction description
        if(*tab == '\0')
        {
            fprintf(stderr, "Error: line %d Missing description field.\n", line);
            free(trans);
            return false;
        }
        cur = tab;
        tab = strchr(cur, '\t');
        if(tab != NULL)
        {
            fprintf(stderr, "Error: line %d has more than 3 <TAB>(3 expected), too many fields.\n", line);
            free(trans);
            return false;
        }
        while(isspace(*cur))
        {
            cur++;
        }
        if(strlen(cur) == 0)
        {
            fprintf(stderr, "Error: line %d Transaction description should not be empty.\n", line);
            free(trans);
            return false;
        }
        strncpy(trans->description, cur, MAX_DESCRIPTION_LENGTH - 1);
        trans->description[MAX_DESCRIPTION_LENGTH - 1] = '\0';
        My402ListAppend(list, trans);
    }
    if(My402ListEmpty(list))
    {
        fprintf(stderr, "Error: Transaction file is empty.\n");
        return false;
    }
    return true;
}

static
bool sort(My402List* list, My402ListElem* left, My402ListElem* right)
{   
    if(left == right)
    {
        return true;
    }
    My402ListElem* pivot = left;
    My402ListElem* end = right;
    while(left != right)
    {
        while(left != right && ((Transaction*)left->obj)->time <= ((Transaction*)pivot->obj)->time)
        {   
            if(left != pivot && ((Transaction*)left->obj)->time == ((Transaction*)pivot->obj)->time)
            {
                fprintf(stderr, "Error: Two transactions (line %d and line %d in tfile) have the same timestamp.\n",
                ((Transaction*)left->obj)->line, ((Transaction*)pivot->obj)->line);
                return false;
            }
            left = My402ListNext(list, left);
        }
        while(left != right && ((Transaction*)right->obj)->time > ((Transaction*)pivot->obj)->time)
        {
            right = My402ListPrev(list, right);
        }
        if(left != right)
        {
            void* temp = left->obj;
            left->obj = right->obj;
            right->obj = temp;
        }
    }
    My402ListElem* target = My402ListPrev(list, left);
    if(((Transaction*)left->obj)->time <= ((Transaction*)pivot->obj)->time)
    {
        if(((Transaction*)left->obj)->time == ((Transaction*)pivot->obj)->time)
        {
            fprintf(stderr, "Error: Two transactions (line %d and line %d in tfile) have the same timestamp.\n",
            ((Transaction*)left->obj)->line, ((Transaction*)pivot->obj)->line);
            return false;
        }
        target = left;
    }
    if(target != pivot)
    {
        void* temp = target->obj;
        target->obj = pivot->obj;
        pivot->obj = temp;
    }
    if(!sort(list, pivot, target))
        return false;
    if(!sort(list, right, end))
        return false;
    return true;
}

static
void printList(My402List* list)
{
    printf("+-----------------+--------------------------+----------------+----------------+\n");
    printf("|       Date      | Description              |         Amount |        Balance |\n");
    printf("+-----------------+--------------------------+----------------+----------------+\n");
    My402ListElem* elem = NULL;
    long long balance = 0;
    for(elem = My402ListFirst(list); elem != NULL; elem = My402ListNext(list, elem))
    {
        Transaction* trans = (Transaction*)elem->obj;
        //print date
        char* time = ctime(&trans->time);
        printf("| ");
        fprintf(stdout, "%.*s", 11, time);
        fprintf(stdout, "%.*s", 4, time + 20);
        printf(" | ");
        //print description
        char* desc = trans->description;
        int desc_len = strlen(desc);
        fprintf(stdout, "%.*s", desc_len, desc);
        if(desc_len < 24)
        {
            for(int i = 0; i < 24 - desc_len; i++)
            {
                printf(" ");
            }
        }
        printf(" | ");
        //print amount
        int amount = trans->amount;
        char amount_str[14];
        if(amount / 100 >= 10000000) {
            for(int i = 1; i < 13; i++)
            {
                amount_str[i] = '?';
            }
        }
        else {
            int i = 12;
            while(amount > 0)
            {
                if(i == 10)
                {
                    amount_str[i] = '.';
                    i--;
                }
                else if(i == 6 || i == 2)
                {
                    amount_str[i] = ',';
                    i--;
                }
                else 
                {
                    amount_str[i] = amount % 10 + '0';
                    amount /= 10;
                    i--;
                }
            }
            while(i >= 1)
            {
                if(i == 12 || i == 11 || i == 9)
                    amount_str[i] = '0';
                else if(i == 10)
                    amount_str[i] = '.';
                else
                amount_str[i] = ' ';
                i--;
            }
        }
        if(trans->type == '+')
        {
            amount_str[0] = ' ';
            amount_str[13] = ' ';
            fprintf(stdout, "%.*s", 14, amount_str);
            balance += trans->amount;
        }
        else 
        {
            amount_str[0] = '(';
            amount_str[13] = ')';
            fprintf(stdout, "%.*s", 14, amount_str);
            balance -= trans->amount;
        }
        printf(" | ");
        //print balance
        char balance_str[14];
        long long abs_balance = abs(balance);
        if(abs_balance / 100 >= 10000000) {
            for(int i = 1; i < 13; i++)
            {
                balance_str[i] = '?';
            }
        }
        else {
            int i = 12;
            while(abs_balance > 0)
            {
                if(i == 10)
                {
                    balance_str[i] = '.';
                    i--;
                }
                else if(i == 6 || i == 2)
                {
                    balance_str[i] = ',';
                    i--;
                }
                else 
                {
                    balance_str[i] = abs_balance % 10 + '0';
                    abs_balance /= 10;
                    i--;
                }
            }
            while(i >= 1)
            {
                if(i == 12 || i == 11 || i == 9)
                    balance_str[i] = '0';
                else if(i == 10)
                    balance_str[i] = '.';
                else
                balance_str[i] = ' ';
                i--;
            }
        }
        if(balance >= 0)
        {
            balance_str[0] = ' ';
            balance_str[13] = ' ';
            fprintf(stdout, "%.*s", 14, balance_str);
        }
        else 
        {
            balance_str[0] = '(';
            balance_str[13] = ')';
            fprintf(stdout, "%.*s", 14, balance_str);
        }
        printf(" |\n");
    }
    printf("+-----------------+--------------------------+----------------+----------------+\n");
}

int main(int argc, char* argv[])
{
    if(argc < 2 || argc > 3)
    {
        fprintf(stderr, "Error: malformed command(Usage: ./warmup1 sort [tfile]).\n");
        return -1;
    }
    else if(strcmp(argv[1], "sort") != 0)
    {
        fprintf(stderr, "Error: malformed command, %s is not a valid commandline option(Usage: ./warmup1 sort [tfile]).\n", argv[1]);
        return -1;
    }
    FILE* fp = NULL;
    if(argc == 2)
    {
        fp = stdin;
    }
    else if(argc == 3)
    {   
        struct stat s;
        if (stat(argv[2], &s) == 0) 
        {
            if(S_ISDIR(s.st_mode)) 
            {
                fprintf(stderr, "Error: %s is a directory.\n", argv[2]);
                return -1;
            }
        }
        fp = fopen(argv[2], "r");
        if(fp == NULL)
        {
            fprintf(stderr, "Error: Cannot open file %s.\n", argv[2]);
            perror("fopen");
            return -1;
        }
    }
    My402List list;
    memset(&list, 0, sizeof(My402List));
    if(!My402ListInit(&list))
    {
        fprintf(stderr, "Error: Cannot initialize My402List.\n");
        if(fp != stdin)
            fclose(fp);
        return -1;
    }
    if(!process(fp, &list))
    {
        if(fp != stdin)
            fclose(fp);
        My402ListUnlinkAll(&list);
        return -1;
    }
    if(!sort(&list, My402ListFirst(&list), My402ListLast(&list)))
    {
        if(fp != stdin)
            fclose(fp);
        My402ListUnlinkAll(&list);
        return -1;
    }
    printList(&list);
    if(fp != stdin)
        fclose(fp);
    My402ListUnlinkAll(&list);
    return 0;
}


