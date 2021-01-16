#ifndef ROOMLIST_H
#define ROOMLIST_H

#include "linkedList.h"
#define BUFFER_SIZE 1024

typedef struct room
{
    char * roomName;
    char * creator;
    List_t peopleInRoom;
} roomInfo;

void getPeopleInRoom (roomInfo* room, char ** roomBuffer);

void removeFromRoom (roomInfo* room, char * clientName);

void addToRoom (List_t * room, char * clientName);

int SearchForRoom (List_t * listOfRooms, char * roomName);

void InsertRoom(List_t* list, roomInfo * room);

void deleteRoom(List_t* list, roomInfo * room);

int roomLengthcount (List_t *list);
#endif