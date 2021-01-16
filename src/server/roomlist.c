#include "roomlist.h"
#include "linkedList.h"
#include <pthread.h>
#include "protocol.h"
pthread_mutex_t buffer_lock;
pthread_mutex_t user_lock;

void getPeopleInRoom (roomInfo* room, char ** roomBuffer)
{
    pthread_mutex_lock(&buffer_lock);
    node_t * roomListHead = room->peopleInRoom.head;
    strcat(*roomBuffer, room->creator);
   
    while (roomListHead != NULL)
    {   
        char * name = (char *)roomListHead->value;
        strcat(*roomBuffer,",");
        strcat(*roomBuffer, name);
        roomListHead = roomListHead->next;
    }
    pthread_mutex_unlock(&buffer_lock);
}

void addToRoom (List_t * room, char * clientName)
{
    node_t * oldHead = room->head;
    node_t * newClientNode = malloc(sizeof(node_t));
    newClientNode->value = clientName;
    newClientNode->next = oldHead;
    room->head = newClientNode;
    room->length++;
}

void removeFromRoom (roomInfo* room, char * clientName)
{
    if (strcmp(clientName, room->creator) == 0)
    {
        return;
    } else {
        int i = 0;
        node_t * roomListHead = room->peopleInRoom.head;
        while (roomListHead->next != NULL)
        {
            char * name = (char *)roomListHead->next->value;
            if (strcmp(name, clientName) == 0)
            {
                roomListHead->next = roomListHead->next->next;
                room->peopleInRoom.length--;
            }
            roomListHead = roomListHead->next;
        }

        if (roomListHead != NULL)
        {
            char * name = (char *)roomListHead->value;
            if (strcmp(name, clientName) == 0)
            {
                roomListHead = NULL;
                room->peopleInRoom.length--;
            }
            if (strcmp((char *)(room->peopleInRoom.head->value), clientName) == 0)
            {
                room->peopleInRoom.head = room->peopleInRoom.head->next;
                room->peopleInRoom.length--;
            }
        }

        roomListHead = room->peopleInRoom.head;
        while (roomListHead != NULL)
        {
            char * name = (char *)roomListHead->value;
            roomListHead = roomListHead->next;
        }
    }
}

int SearchForRoom (List_t * listOfRooms, char * roomName)
{
    node_t * currRoom = listOfRooms->head;;
    while (currRoom != NULL)
    {
        roomInfo * room = (roomInfo *)(currRoom->value);
        if (strcmp(room->roomName, roomName) == 0)
        {
            return 1;
        }
        currRoom = currRoom->next;
    }
    return 0;
}

void InsertRoom(List_t* list, roomInfo * room) {
    if (list->length == 0)
        list->head = NULL;

    node_t** head = &(list->head);
    node_t* new_node = malloc(sizeof(node_t));

    new_node->value = room;

    new_node->next = *head;

    *head = new_node;
    list->length++; 
}

void deleteRoom(List_t* list, roomInfo * room) {
    node_t * currRoom = list->head;
    while (currRoom->next != NULL)
    {
        roomInfo * nextRoomInfo = (roomInfo *)(currRoom->next->value);
        if (strcmp(nextRoomInfo->roomName, room->roomName) == 0) {
            free(nextRoomInfo->roomName);
            deleteList(&(nextRoomInfo->peopleInRoom));
            free(nextRoomInfo);
            currRoom->next = currRoom->next->next;
            list->length--;
            return;
        }
        currRoom = currRoom->next;
    }
    if (currRoom != NULL){
        roomInfo * currRoomInfo = (roomInfo *)(currRoom->value);
        if (strcmp(currRoomInfo->roomName, room->roomName) == 0) {
            free(currRoomInfo->roomName);
            deleteList(&(currRoomInfo->peopleInRoom));
            free(currRoomInfo);
            currRoom = NULL;
            list->length--;
        } else {
            roomInfo * headRoom = (roomInfo *)(list->head->value);
            currRoom = list->head;
            if (strcmp(headRoom->roomName, room->roomName) == 0) {
                list->head = list->head->next;
                free(headRoom->roomName);
                deleteList(&(headRoom->peopleInRoom));
                free(headRoom);
                list->length--;
            }
        }
    }
    if (list->length == 0){
        list->head = NULL;
    }

    currRoom = list->head;
}

int roomLengthcount (List_t *list){
    pthread_mutex_lock(&buffer_lock);
    int i = 0;
    node_t * currRoom = list->head;
    while (currRoom != NULL)
    {
        roomInfo * room = (roomInfo *)(currRoom->value);
        i += strlen(room->roomName) + 3;
        pthread_mutex_lock(&user_lock);
        node_t * roomListHead = room->peopleInRoom.head;
        while (roomListHead != NULL)
        {   
            char * name = (char *)roomListHead->value;
            i += strlen(name) + 1;
            roomListHead = roomListHead->next;
        }
        pthread_mutex_unlock(&user_lock);
        currRoom = currRoom->next;
    }
    i -= 1;
    return i;
    pthread_mutex_unlock(&buffer_lock);
}