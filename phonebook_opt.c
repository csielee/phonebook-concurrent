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

/* Atomic */
#define barrier()                 (__sync_synchronize())
#define AO_GET(ptr)               ({ __typeof__(*(ptr)) volatile *_val = (ptr); barrier(); (*_val); })
#define AO_SET(ptr, value)        ((void)__sync_lock_test_and_set((ptr), (value)))
#define AO_ADD_F(ptr, value)      ((__typeof__(*(ptr)))__sync_add_and_fetch((ptr), (value)))
#define AO_CAS(ptr, comp, value)  ((__typeof__(*(ptr)))__sync_val_compare_and_swap((ptr), (comp), (value)))

static entry *entryHead,*entry_pool;
static pthread_t threads[THREAD_NUM];
static thread_arg *thread_args[THREAD_NUM];
static char *map;
static off_t file_size;

/* memory pool */
#define MPSIZE 1000
typedef struct {
    void *next;
    int index;
} mp_node_t;
typedef struct {
    mp_node_t *head,*curr;
    size_t mp_size;
} mp_list_t;

mp_list_t *mp_init(size_t s)
{
    mp_list_t *n = malloc(sizeof(mp_list_t));
    n->head = malloc(sizeof(mp_node_t)+s*MPSIZE);
    n->head->next = NULL;
    n->head->index = 0;
    n->curr = n->head;
    n->mp_size = s;
    return n;
}

void *mp_alloc(mp_list_t *mp)
{
    void *r;
    int tmp;
    while (1) {
        /* wait malloc */
        while (!(r = AO_GET(&mp->curr)));
        tmp = AO_GET(&((mp_node_t *)r)->index);
        if (tmp == MPSIZE) {
            if (AO_CAS(&mp->curr,r,NULL)) {
                //set
                mp_node_t *t = malloc(sizeof(mp_node_t)+mp->mp_size*MPSIZE);
                t->next = mp->head;
                t->index = 0;
                mp->head = t;
                while (AO_CAS(&mp->curr,NULL,t));
            } else {
                // no set
                while (!(r = AO_GET(&mp->curr)));
            }
        } else {
            if (AO_CAS(&((mp_node_t *)r)->index,tmp,tmp+1)!=tmp+1) {
                //get
                break;
            }
        }
    }
    // tmp , r;
    return (r+sizeof(mp_node_t)+(tmp*mp->mp_size));
}

void mp_free(mp_list_t *mp)
{
    mp_node_t *tmp = mp->head;
    while (mp->head->next) {
        mp->head = mp->head->next;
        free(tmp);
        tmp = mp->head;
    }
    free(mp->head);
    free(mp);
}

static mp_list_t *entry_mp;

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
    //entry *j = t_arg->lEntryPool_begin;
    for (char *i = t_arg->data_begin; i < t_arg->data_end; count++) {
        //j += t_arg->numOfThread, count++) {
        /* Append the new at the end of the local linked list */
        //t_arg->lEntry_tail->pNext = j;
        t_arg->lEntry_tail->pNext = (entry *)mp_alloc(entry_mp);
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
    entry_mp = mp_init(sizeof(entry));
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
    mp_free(entry_mp);
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
