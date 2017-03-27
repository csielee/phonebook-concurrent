#define  _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <assert.h>
#include <sys/mman.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>


#include "phonebook_opt.h"
#include "debug.h"
#include "text_align.h"

#define ALIGN_FILE "align.txt"

#ifndef THREAD_NUM
#define THREAD_NUM 4
#endif

static entry *entryHead,*entry_pool;
static pthread_t threads[THREAD_NUM];
static thread_arg *thread_args[THREAD_NUM];
static char *map;
static off_t file_size;

/* memory pool */

/* lockfree thread pool */
typedef struct _task {
    void (*func)(void *);
    void *arg;
    struct _task *next, *prev;
} task_t;

typedef struct {
    task_t *head, *tail;
    int task_number;
} taskqueue_t;


static entry *findName(char lastname[], entry *pHead)
{
    size_t len = strlen(lastname);
    while (pHead) {
        if (strncasecmp(lastname, pHead->lastName, len) == 0
                && (pHead->lastName[len] == '\n' ||
                    pHead->lastName[len] == '\0')) {
            pHead->lastName[len] = '\0';
            if (!pHead->dtl)
                pHead->dtl = (pdetail) malloc(sizeof(detail));
            return pHead;
        }
        DEBUG_LOG("find string = %s\n", pHead->lastName);
        pHead = pHead->pNext;
    }
    return NULL;
}

static thread_arg *createThread_arg(char *data_begin, char *data_end,
                                    int threadID, int numOfThread,
                                    entry *entryPool)
{
    thread_arg *new_arg = (thread_arg *) malloc(sizeof(thread_arg));

    new_arg->data_begin = data_begin;
    new_arg->data_end = data_end;
    new_arg->threadID = threadID;
    new_arg->numOfThread = numOfThread;
    new_arg->lEntryPool_begin = entryPool;
    new_arg->lEntry_head = new_arg->lEntry_tail = entryPool;
    return new_arg;
}

/**
 * Generate a local linked list in thread.
 */
static void append(void *arg)
{
    struct timespec start, end;
    double cpu_time;

    clock_gettime(CLOCK_REALTIME, &start);

    thread_arg *t_arg = (thread_arg *) arg;

    int count = 0;
    entry *j = t_arg->lEntryPool_begin;
    for (char *i = t_arg->data_begin; i < t_arg->data_end;
            j += t_arg->numOfThread, count++) {
        /* Append the new at the end of the local linked list */
        t_arg->lEntry_tail->pNext = j;
        t_arg->lEntry_tail = t_arg->lEntry_tail->pNext;
        t_arg->lEntry_tail->lastName = i;
        while (*i!='\n')
            i++;
        *i = '\0';
        i++;
        t_arg->lEntry_tail->pNext = NULL;
        t_arg->lEntry_tail->dtl = NULL;
        DEBUG_LOG("thread %d t_argend string = %s\n",
                  t_arg->threadID, t_arg->lEntry_tail->lastName);
    }
    clock_gettime(CLOCK_REALTIME, &end);
    cpu_time = diff_in_second(start, end);

    DEBUG_LOG("thread take %lf sec, count %d\n", cpu_time, count);

    pthread_exit(NULL);
}

static void show_entry(entry *pHead)
{
    while (pHead) {
        printf("%s", pHead->lastName);
        pHead = pHead->pNext;
    }
}

static void phonebook_create()
{
}

static entry *phonebook_appendByFile(char *fileName)
{
    int fd = open(fileName, O_RDONLY | O_NONBLOCK);
    struct stat fd_st;
    fstat(fd,&fd_st);
    file_size = fsize(ALIGN_FILE);
    /* Allocate the resource at first */
    map = mmap(NULL, fd_st.st_size, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0);
    assert(map && "mmap error");
    entry_pool = (entry *) malloc(sizeof(entry) * file_size / MAX_LAST_NAME_SIZE);
    assert(entry_pool && "entry_pool error");

    /* divide text file */
    int begin[THREAD_NUM+1];
    begin[THREAD_NUM] = fd_st.st_size;
    begin[0] = 0;
    for (int i = 1; i < THREAD_NUM; i++) {
        begin[i] = (begin[THREAD_NUM]/THREAD_NUM)*i;
        while (*(map+begin[i])!='\n')
            begin[i]++;

        begin[i]++;
    }
    file_size = fd_st.st_size;

    /* Prepare for mutli-threading */
    pthread_setconcurrency(THREAD_NUM + 1);
    for (int i = 0; i < THREAD_NUM; i++)
        thread_args[i] = createThread_arg(map + begin[i],map + begin[i+1], i,THREAD_NUM, entry_pool + i);

    /* Deliver the jobs to all thread and wait for completing  */

    for (int i = 0; i < THREAD_NUM; i++)
        pthread_create(&threads[i], NULL, (void *)&append, (void *)thread_args[i]);

    for (int i = 0; i < THREAD_NUM; i++)
        pthread_join(threads[i], NULL);

    /* Connect the linked list of each thread */
    entryHead = thread_args[0]->lEntry_head->pNext;
    DEBUG_LOG("Connect 0 head string %s %p\n", entryHead->lastName, thread_args[0]->data_begin);
    entry *e = thread_args[0]->lEntry_tail;
    DEBUG_LOG("Connect 0 tail string %s %p\n", e->lastName, thread_args[0]->data_begin);
    DEBUG_LOG("round 0\n");

    for (int i = 1; i < THREAD_NUM; i++) {
        e->pNext = thread_args[i]->lEntry_head->pNext;
        DEBUG_LOG("Connect %d head string %s %p\n", i,e->pNext->lastName, thread_args[i]->data_begin);

        e = thread_args[i]->lEntry_tail;
        DEBUG_LOG("Connect %d tail string %s %p\n", i, e->lastName, thread_args[i]->data_begin);
        DEBUG_LOG("round %d\n", i);
    }
    close(fd);
    pthread_setconcurrency(0);
    /* Return head of linked list */
    return entryHead;
}

static entry *phonebook_findName(char *lastName)
{
    return findName(lastName, entryHead);
}

static void phonebook_free()
{
    entry *e = entryHead;
    while (e) {
        free(e->dtl);
        e = e->pNext;
    }

    free(entry_pool);
    for (int i = 0; i < THREAD_NUM; i++)
        free(thread_args[i]);

    munmap(map, file_size);
}

/* API */
struct __PHONEBOOK_API Phonebook = {
    .create = phonebook_create,
    .appendByFile = phonebook_appendByFile,
    .findName = phonebook_findName,
    .free = phonebook_free,
};

static double diff_in_second(struct timespec t1, struct timespec t2)
{
    struct timespec diff;
    if (t2.tv_nsec-t1.tv_nsec < 0) {
        diff.tv_sec  = t2.tv_sec - t1.tv_sec - 1;
        diff.tv_nsec = t2.tv_nsec - t1.tv_nsec + 1000000000;
    } else {
        diff.tv_sec  = t2.tv_sec - t1.tv_sec;
        diff.tv_nsec = t2.tv_nsec - t1.tv_nsec;
    }
    return (diff.tv_sec + diff.tv_nsec / 1000000000.0);
}
