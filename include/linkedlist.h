#ifndef LINKEDLIST_H
#define LINKEDLIST_H

#include <stdio.h>

#include <stdlib.h>
#include <assert.h>
#include <stdbool.h>
#include <string.h>

/*
 * Structure for each node of the linkedList
 *
 * value - a pointer to the data of the node. 
 * next - a pointer to the next node in the list. 
 */
typedef struct node {
    void* data;
    struct node* next;
} node_t;

/*
 * Structure for the base linkedList
 * 
 * head - a pointer to the first node in the list. NULL if length is 0.
 * length - the current length of the linkedList. Must be initialized to 0.
 * comparator - function pointer to linkedList comparator. Must be initialized!
 */
typedef struct list {
    node_t* head;
    int length;
    /* the comparator uses the values of the nodes directly (i.e function has to be type aware) */
    int (*comparator)(const void*, const void*);
    void (*printer)(void*, void*);  // function pointer for printing the data stored
    void (*deleter)(void*);              // function pointer for deleting any dynamically 
                                         // allocated items within the data stored

} list_t;

// Functions implemented/provided in linkedList.c
list_t* CreateList(int (*compare)(const void*, const void*), void (*print)(void*,void*),
                   void (*delete)(void*));
void InsertAtHead(list_t* list, void* val_ref);

node_t* FindInList(list_t* list, void* token);
void DestroyList(list_t** list);

/*
 * Traverse the list printing each node in the current order.
 * @param list pointer to the linkedList struct
 * @param fp open file pointer to print output to
 * 
 */
void PrintLinkedList(list_t* list, FILE* fp);

#endif