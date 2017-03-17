#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "phonebook_orig.h"

static entry* entryHead;

/* original version */
entry *findName(char lastname[], entry *pHead)
{
    while (pHead) {
        if (strcasecmp(lastname, pHead->lastName) == 0)
            return pHead;
        pHead = pHead->pNext;
    }
    return NULL;
}

entry *append(char lastName[], entry *e)
{
    /* allocate memory for the new entry and put lastName */
    e->pNext = (entry *) malloc(sizeof(entry));
    e = e->pNext;
    strcpy(e->lastName, lastName);
    e->pNext = NULL;

    return e;
}

void phonebook_init(void *option)
{
    if (!option) {
    }
    entryHead = (entry *) malloc(sizeof(entry));
    entryHead->pNext = NULL;
}

entry *phonebook_append(char *s)
{
    FILE *fp = fopen(s,"r");
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

entry *phonebook_findName(char *s)
{
    return findName(s, entryHead);
}

void phonebook_free()
{
    entry *e;
    while (entryHead) {
        e = entryHead;
        entryHead = entryHead->pNext;
        free(e);
    }
}
