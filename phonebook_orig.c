#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "phonebook_orig.h"

static entry* entryHead;

/* original version */
static entry *findName(char lastname[], entry *pHead)
{
    while (pHead) {
        if (strcasecmp(lastname, pHead->lastName) == 0)
            return pHead;
        pHead = pHead->pNext;
    }
    return NULL;
}

static entry *append(char lastName[], entry *e)
{
    /* allocate memory for the new entry and put lastName */
    e->pNext = (entry *) malloc(sizeof(entry));
    e = e->pNext;
    strcpy(e->lastName, lastName);
    e->pNext = NULL;

    return e;
}

static void phonebook_create()
{
    entryHead = (entry *) malloc(sizeof(entry));
    entryHead->pNext = NULL;
}

static entry *phonebook_appendByFile(char *fileName)
{
    FILE *fp = fopen(fileName,"r");
    if (!fp) {
        printf("cannot open the file\n");
        return NULL;
    }
    entry *e = entryHead;
    char line[MAX_LAST_NAME_SIZE];
    int i=0;

    while (fgets(line, sizeof(line), fp)) {
        while (line[i] != '\0')
            i++;
        line[i - 1] = '\0';
        i = 0;
        e = append(line, e);
    }

    fclose(fp);
    return entryHead;
}

static entry *phonebook_append(char *lastName)
{
    entry *e = entryHead;
    while (e->pNext)
        e = e->pNext;
    append(lastName, e);

    return entryHead;
}

static entry *phonebook_findName(char *lastName)
{
    return findName(lastName, entryHead);
}

static void phonebook_free()
{
    entry *e;
    while (entryHead) {
        e = entryHead;
        entryHead = entryHead->pNext;
        free(e);
    }
}

/* API */
struct __PHONEBOOK_API Phonebook = {
    .create = phonebook_create,
    .appendByFile = phonebook_appendByFile,
    .append = phonebook_append,
    .findName = phonebook_findName,
    .free = phonebook_free,
};
