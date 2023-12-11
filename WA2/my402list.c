#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/time.h>

#include "cs402.h"

#include "my402list.h"

int My402ListLength(My402List* list)
{
    return list->num_members;
}

int My402ListEmpty(My402List* list)
{
    if(list->num_members == 0)
        return TRUE;
    else
        return FALSE;
}

int My402ListAppend(My402List* list, void* obj)
{
    My402ListElem* new_elem = (My402ListElem*)malloc(sizeof(My402ListElem));
    if(new_elem == NULL)
        return FALSE;
    new_elem->obj = obj;
    if(list->num_members == 0)
    {
        list->anchor.next = new_elem;
        list->anchor.prev = new_elem;
        new_elem->next = &(list->anchor);
        new_elem->prev = &(list->anchor);
    }
    else
    {
        My402ListElem* last_elem = list->anchor.prev;
        last_elem->next = new_elem;
        new_elem->prev = last_elem;
        new_elem->next = &(list->anchor);
        list->anchor.prev = new_elem;
    }
    list->num_members++;
    return TRUE;
}

int My402ListPrepend(My402List* list, void* obj)
{
    My402ListElem* new_elem = (My402ListElem*)malloc(sizeof(My402ListElem));
    if(new_elem == NULL)
        return FALSE;
    new_elem->obj = obj;
    if(list->num_members == 0)
    {
        list->anchor.next = new_elem;
        list->anchor.prev = new_elem;
        new_elem->next = &(list->anchor);
        new_elem->prev = &(list->anchor);
    }
    else
    {
        My402ListElem* first_elem = list->anchor.next;
        first_elem->prev = new_elem;
        new_elem->next = first_elem;
        new_elem->prev = &(list->anchor);
        list->anchor.next = new_elem;
    }
    list->num_members++;
    return TRUE;
}

void My402ListUnlink(My402List* list, My402ListElem* elem)
{
    if(elem == NULL)
        return;
    My402ListElem* prev_elem = elem->prev;
    My402ListElem* next_elem = elem->next;
    prev_elem->next = next_elem;
    next_elem->prev = prev_elem;
    free(elem);
    list->num_members--;
}

void My402ListUnlinkAll(My402List* list)
{
    My402ListElem* elem = list->anchor.next;
    while(elem != &(list->anchor))
    {
        My402ListElem* next_elem = elem->next;
        free(elem);
        elem = next_elem;
    }
    list->anchor.next = &(list->anchor);
    list->anchor.prev = &(list->anchor);
    list->num_members = 0;
}

int My402ListInsertAfter(My402List* list, void* obj, My402ListElem* elem)
{
    if(elem == NULL)
        return My402ListAppend(list, obj);
    My402ListElem* new_elem = (My402ListElem*)malloc(sizeof(My402ListElem));
    if(new_elem == NULL)
        return FALSE;
    new_elem->obj = obj;
    My402ListElem* next_elem = elem->next;
    elem->next = new_elem;
    new_elem->prev = elem;
    new_elem->next = next_elem;
    next_elem->prev = new_elem;
    list->num_members++;
    return TRUE;
}

int My402ListInsertBefore(My402List* list, void* obj, My402ListElem* elem)
{
    if(elem == NULL)
        return My402ListPrepend(list, obj);
    My402ListElem* new_elem = (My402ListElem*)malloc(sizeof(My402ListElem));
    if(new_elem == NULL)
        return FALSE;
    new_elem->obj = obj;
    My402ListElem* prev_elem = elem->prev;
    elem->prev = new_elem;
    new_elem->next = elem;
    new_elem->prev = prev_elem;
    prev_elem->next = new_elem;
    list->num_members++;
    return TRUE;
}

My402ListElem* My402ListFirst(My402List* list)
{
    if(list->num_members == 0)
        return NULL;
    else
        return list->anchor.next;
}

My402ListElem* My402ListLast(My402List* list)
{
    if(list->num_members == 0)
        return NULL;
    else
        return list->anchor.prev;
}

My402ListElem* My402ListNext(My402List* list, My402ListElem* elem)
{
    // if(elem == NULL)
    //     return NULL;
    if(elem->next == &(list->anchor))
        return NULL;
    else
        return elem->next;
}

My402ListElem* My402ListPrev(My402List* list, My402ListElem* elem)
{
    // if(elem == NULL)
    //     return NULL;
    if(elem->prev == &(list->anchor))
        return NULL;
    else
        return elem->prev;
}

My402ListElem* My402ListFind(My402List* list, void* obj)
{
    My402ListElem* elem = list->anchor.next;
    while(elem != &(list->anchor))
    {
        if(elem->obj == obj)
            return elem;
        elem = elem->next;
    }
    return NULL;
}

int My402ListInit(My402List* list)
{   if(list == NULL)
        return FALSE;
    list->anchor.obj = NULL;
    list->anchor.next = &(list->anchor);
    list->anchor.prev = &(list->anchor);
    list->num_members = 0;
    // My402ListUnlinkAll(list);
    return TRUE;
}