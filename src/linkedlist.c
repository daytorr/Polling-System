#include "linkedlist.h"

list_t* CreateList(int (*compare)(const void*, const void*), void (*print)(void*, void*),
                   void (*delete)(void*)) {
    list_t* list = malloc(sizeof(list_t));
    list->comparator = compare;
    list->printer = print;
    list->deleter = delete;
    list->length = 0;
    list->head = NULL;
    return list;
}

void InsertAtHead(list_t* list, void* val_ref) {
    if(list == NULL || val_ref == NULL)
        return;
    if (list->length == 0) list->head = NULL;

    node_t** head = &(list->head);
    node_t* new_node;
    new_node = malloc(sizeof(node_t));

    new_node->data = val_ref;

    new_node->next = *head;

    // moves list head to the new node
    *head = new_node;
    list->length++;
}

node_t* FindInList(list_t* list, void* token) {
    if (list != NULL && token != NULL) {
        node_t *current = list->head;
        while (current != NULL) {
            if (list->comparator(current->data, token) == 0) {
                return current;
            }
            current = current->next;
        }
    }
    return NULL;
}

void DestroyList(list_t** list)  {
    if (list != NULL) {
        node_t *current = (*list)->head;
        while (current != NULL) {
            node_t *temp = current->next;
            (*list)->deleter(current->data);
            free(current);
            current = temp;
        }
        free(*list);
    }
}

void PrintLinkedList(list_t* list, FILE* fp) {
    if(list == NULL)
        return;

    node_t* head = list->head;
    while (head != NULL) {
        list->printer(head->data, fp);
        head = head->next;
    }
}