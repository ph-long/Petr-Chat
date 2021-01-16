#include "server.h"
#include "protocol.h"
#include <pthread.h>
#include <signal.h>
#include "linkedList.h"
#include "roomlist.h"
#include "sbuf.h"
#include "job.h"
#include <time.h>

const char exit_str[] = "exit";

char buffer[BUFFER_SIZE];
pthread_mutex_t buffer_lock;
pthread_mutex_t room_lock;
pthread_mutex_t user_lock;
pthread_mutex_t audit_lock;

int total_num_msg = 0;
int listen_fd;

char* file_name;
FILE* audit_file;

//J:Make a Llist of names
List_t *list_of_names;

List_t *list_of_rooms;

sbuf_t sbuf;


void sigint_handler(int sig) {
    printf("shutting down server\n");
    close(listen_fd);
    exit(0);
}

int server_init(int server_port) {
    int sockfd;
    struct sockaddr_in servaddr;

    // socket create and verification
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd == -1) {
        printf("socket creation failed...\n");
        exit(EXIT_FAILURE);
    } 
    // else
    //     printf("Socket successfully created\n");

    bzero(&servaddr, sizeof(servaddr));

    // assign IP, PORT
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    servaddr.sin_port = htons(server_port);

    int opt = 1;
    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEPORT, (char *)&opt, sizeof(opt)) < 0) {
        perror("setsockopt");
        exit(EXIT_FAILURE);
    }

    // Binding newly created socket to given IP and verification
    if ((bind(sockfd, (SA *)&servaddr, sizeof(servaddr))) != 0) {
        printf("socket bind failed\n");
        exit(EXIT_FAILURE);
    } 
    // else
    //     printf("Socket successfully binded\n");

    // Now server is ready to listen and verification
    if ((listen(sockfd, 1)) != 0) {
        printf("Listen failed\n");
        exit(EXIT_FAILURE);
    } 
    else
        printf("Currently listening on port %d.\n", server_port);
        // printf("Server listening on port: %d.. Waiting for connection\n", server_port);

    return sockfd;
}

void *process_job() {
    pthread_detach(pthread_self());
    // sleep(1);
    while (1){
        job * newJob = sbuf_remove(&sbuf);
        petr_header* client_message = newJob->client_message;
        int client_fd = newJob->client_fd;
        char * read_buffer = newJob->message;
        printf("%s sent message %d length: %d %s\n", getName(list_of_names, client_fd), client_message->msg_type, client_message->msg_len, read_buffer);

        petr_header *server_response = malloc(sizeof(petr_header));
        server_response->msg_len = 0;

        if(client_message->msg_type == LOGOUT)
        {
             //J: Find all the rooms that the user is a creator of
            pthread_mutex_lock(&room_lock);
            pthread_mutex_lock(&user_lock);
            node_t * current_room = list_of_rooms->head;
            while (current_room != NULL)
            {
                roomInfo * room = (roomInfo *)(current_room->value);                     
                //J: If the the current user is the creator of the room than access it
                if (strcmp(room->creator, getName(list_of_names, client_fd)) == 0) 
                {
                    // J: Look through the room and tell all the users that the room will close
                    char errorMessage[strlen(room->roomName) + 1];
                    bzero(&errorMessage, sizeof(errorMessage));
                    strcat(errorMessage, room->roomName);
                    strcat(errorMessage, "\0");
                    node_t * roomListHead = room->peopleInRoom.head;
                    while (roomListHead != NULL)
                    {
                        char * name = (char *)roomListHead->value;
                        if (strcmp(name, room->creator)  != 0)
                        {
                            server_response->msg_type = RMCLOSED;
                            server_response->msg_len = strlen(errorMessage) + 1;
                            int other_fd = getFD(list_of_names, name);
                            wr_msg(other_fd, server_response, errorMessage);
                        }
                        roomListHead = roomListHead->next;
                    }
                    deleteRoom(list_of_rooms, room);
                }
                current_room = current_room->next;
            }
            //J: Find all the rooms the current user is a part of
            node_t * current_room2 = list_of_rooms->head;
            while (current_room2 != NULL)
            {
                roomInfo * room = (roomInfo *)(current_room2->value);
                char * user = getName(list_of_names, client_fd);
                //J: If the current user is found inside the room than remove the user
                if (SearchForName(&(room->peopleInRoom), user)) {
                    removeFromRoom(room, user);
                }
                current_room2 = current_room2->next;
            }
            pthread_mutex_unlock(&room_lock);
            //J: Finally remove the current user from the list of users
            int len1 = strlen(getName(list_of_names, client_fd)) + 1;
            list_of_names->names_size -= strlen(getName(list_of_names, client_fd)) + 1;
            RemoveById(list_of_names,client_fd);
            pthread_mutex_unlock(&user_lock);

            pthread_mutex_lock(&audit_lock);
            audit_file = fopen(file_name,"a");
            time_t now;
            time(&now);
            fprintf(audit_file,"%s",ctime(&now));
            fprintf(audit_file," - Logged out client %d\n",client_fd);
            fclose(audit_file);
            pthread_mutex_unlock(&audit_lock);
            printf("Close current client connection\n");
            free(client_message);
            close(client_fd);
        }
         //J: Check to see what type of request the client is asking for
        else if(client_message->msg_type == USRLIST)
        {
            //J: names_size is the total bytes needed for the a single string to
            //   hold all of the user names. Had to multiply by 4 becuase a strange
            //   conversion error occurs if the size isnt atleast 4 times as big.
            pthread_mutex_lock(&user_lock);
            int names_size = list_of_names->names_size - strlen(getName(list_of_names, client_fd));
            char user_names[names_size];
            if(list_of_names->length > 1)
            {
                bzero(&user_names, sizeof(user_names));
                StringOfNames(list_of_names,user_names,client_fd);
            }
            else
            {   names_size = 0; }
            pthread_mutex_unlock(&user_lock);
            strcat(user_names,"\0");
            petr_header *server_response2 = malloc(sizeof(petr_header));
            server_response2->msg_type = USRLIST;
            server_response2->msg_len = names_size;
            wr_msg(client_fd, server_response2, user_names);
             printf("%s recievces message %d length: %d %s\n", getName(list_of_names, client_fd), server_response2->msg_type, server_response2->msg_len, user_names);
            pthread_mutex_lock(&audit_lock);
            audit_file = fopen(file_name,"a");
            time_t now;
            time(&now);
            fprintf(audit_file,"%s",ctime(&now));
            if(names_size == 0) {
                fprintf(audit_file," - Message sent to client %d for USRLIST request: \n\t- %s",client_fd,"No other users logged in on server\n");
            } else {
                fprintf(audit_file," - Message sent to client %d for USRLIST request: \n%s",client_fd,user_names);
            }
            fclose(audit_file);
            pthread_mutex_unlock(&audit_lock);
        }
        /* P: Creates a room and add it to the list_of_rooms linked list*/
        else if (client_message->msg_type == RMCREATE) {
            /* P: Checks if room already exists in linked list */
            pthread_mutex_lock(&room_lock);
            if (SearchForRoom(list_of_rooms, read_buffer)) {
                server_response->msg_type = ERMEXISTS;
                server_response->msg_len = 0;
                wr_msg(client_fd, server_response, NULL);
                pthread_mutex_lock(&audit_lock);
                audit_file = fopen(file_name,"a");
                time_t now;
                time(&now);
                fprintf(audit_file,"%s",ctime(&now));
                fprintf(audit_file," - Message sent to client %d for RMCREATE:\n\t- Room '%s' already exists",client_fd,read_buffer);
                fclose(audit_file);
                pthread_mutex_unlock(&audit_lock);
            } else {
                /* P: Malloc an empty roomInfo object and initialize the struct's values */
                roomInfo * newRoom = malloc(sizeof(roomInfo));
                char * maker = malloc(sizeof(char));
                pthread_mutex_lock(&user_lock);
                maker = getName(list_of_names, client_fd);
                pthread_mutex_unlock(&user_lock);
                newRoom->creator = maker;
                newRoom->roomName = malloc(strlen(read_buffer));
                strcpy(newRoom->roomName, read_buffer);
                List_t * listOfPeople;
                listOfPeople = malloc(sizeof(List_t));
                listOfPeople->head = NULL;
                listOfPeople->length = 0;
                newRoom->peopleInRoom = *listOfPeople;
                /* P: Add the room maker to whose in the room */
                addToRoom(&(newRoom->peopleInRoom), newRoom->creator);
                /* P: Insert the created room to the linked list */
                InsertRoom(list_of_rooms, newRoom);
                /* P: Server tells the client OK */
                server_response->msg_type = OK;
                server_response->msg_len = 0;
                printf("%s recieves message %d length: %d\n", getName(list_of_names, client_fd), server_response->msg_type, server_response->msg_len);
                wr_msg(client_fd, server_response, NULL);
                pthread_mutex_lock(&audit_lock);
                audit_file = fopen(file_name,"a");
                time_t now;
                time(&now);
                fprintf(audit_file,"%s",ctime(&now));
                fprintf(audit_file," - Message sent to client %d for RMCREATE:\n\t- Room '%s' created\n",client_fd,read_buffer);
                fclose(audit_file);
                pthread_mutex_unlock(&audit_lock);
            }
            pthread_mutex_unlock(&room_lock);
        } 
         /* P: Deletes a room from the list_of_rooms linked list and clients
        in the room exit the room */
        else if (client_message->msg_type == RMDELETE) {       
            /* P: Checks if the room is in the linked list */
            pthread_mutex_lock(&room_lock);
            if (!SearchForRoom(list_of_rooms, read_buffer)) {
                server_response->msg_type = ERMNOTFOUND;
                server_response->msg_len = 0;
                wr_msg(client_fd, server_response, NULL);
                pthread_mutex_lock(&audit_lock);
                audit_file = fopen(file_name,"a");
                time_t now;
                time(&now);
                fprintf(audit_file,"%s",ctime(&now));
                fprintf(audit_file," - Message sent to client %d for RMDELETE:\n\t- Room '%s' not found",client_fd,read_buffer);
                fclose(audit_file);
                pthread_mutex_unlock(&audit_lock);
            } else {              
                /* P: Iterate through the rooms to find the room to be deleted */
                node_t * currRoom = list_of_rooms->head;
                while (currRoom != NULL)
                {
                    roomInfo * room = (roomInfo *)(currRoom->value);
                    if (strcmp(room->roomName, read_buffer) == 0) {                       
                        /* P: If the client is the creator, access to remove the room */
                        pthread_mutex_lock(&user_lock);
                        if (strcmp(room->creator, getName(list_of_names, client_fd)) == 0) {
                            /* P: Iterate through the people in the room and have the clients
                            exit the room */
                            pthread_mutex_unlock(&user_lock);
                             char errorMessage[strlen(read_buffer) + 1];
                            bzero(&errorMessage, sizeof(errorMessage));
                            strcat(errorMessage, read_buffer);
                            strcat(errorMessage, "\0");
                            node_t * roomListHead = room->peopleInRoom.head;
                            while (roomListHead != NULL)
                            {
                                char * name = (char *)roomListHead->value;
                                if (strcmp(name, room->creator)  != 0)
                                {
                                    /* P: Server tells clients that the room has closed */

                                    server_response->msg_type = RMCLOSED;
                                    server_response->msg_len = strlen(errorMessage) + 1;
                                    pthread_mutex_lock(&user_lock);
                                    int other_fd = getFD(list_of_names, name);
                                    pthread_mutex_unlock(&user_lock);
                                    wr_msg(other_fd, server_response, errorMessage);
                                    pthread_mutex_lock(&audit_lock);
                                    audit_file = fopen(file_name,"a");
                                    time_t now;
                                    time(&now);
                                    fprintf(audit_file,"%s",ctime(&now));
                                    fprintf(audit_file," - Message sent to client %d for RMCLOSE:\n\t- Room '%s' is closed\n",other_fd,read_buffer);
                                    fclose(audit_file);
                                    pthread_mutex_unlock(&audit_lock);
                                }
                                roomListHead = roomListHead->next;
                            }
                            /* P: The server tells the room creator's client OK */
                            deleteRoom(list_of_rooms, room);
                            server_response->msg_type = OK;
                            server_response->msg_len = 0;
                            wr_msg(client_fd, server_response, NULL);
                            pthread_mutex_lock(&audit_lock);
                             audit_file = fopen(file_name,"a");
                            time_t now;
                            time(&now);
                            fprintf(audit_file,"%s",ctime(&now));
                            fprintf(audit_file," - Message sent to client %d for RMDELETE:\n\t- Room '%s' deleted\n",client_fd,read_buffer);
                            fclose(audit_file);
                            pthread_mutex_unlock(&audit_lock);
                        } else {
                            /* P: If the client is not the room's creator, they do not have access to delete room */
                            pthread_mutex_unlock(&user_lock);
                            server_response->msg_type = ERMDENIED;
                            server_response->msg_len = 0;
                            wr_msg(client_fd, server_response, NULL);
                            pthread_mutex_lock(&audit_lock);
                            audit_file = fopen(file_name,"a");
                            time_t now;
                            time(&now);
                            fprintf(audit_file,"%s",ctime(&now));
                            fprintf(audit_file," - Message sent to client %d for RMDELETE:\n Delete failed, not the creator of room %s",client_fd,read_buffer);
                            fclose(audit_file);
                            pthread_mutex_unlock(&audit_lock);
                        }
                    }
                    currRoom = currRoom->next;
                }
            }
            pthread_mutex_unlock(&room_lock);   
        }
        /* P: Server writes to client a list of all rooms and the clients
        inside of those rooms */
        else if (client_message->msg_type == RMLIST) {
            /* P: If there are no rooms */
            if (list_of_rooms->length == 0) {
                server_response->msg_type = RMLIST;
                server_response->msg_len = 0;
                wr_msg(client_fd, server_response, NULL);
                pthread_mutex_lock(&audit_lock);
                audit_file = fopen(file_name,"a");
                time_t now;
                time(&now);
                fprintf(audit_file,"%s",ctime(&now));
                fprintf(audit_file," - Message sent to client %d:\n\t- No rooms available on server\n",client_fd);
                fclose(audit_file);
                pthread_mutex_unlock(&audit_lock);
            } else
            {
                pthread_mutex_lock(&room_lock);
                node_t * currRoom = list_of_rooms->head;
                /* P: Allocates space for a string to be returned */
                char allRooms[BUFFER_SIZE];
                 bzero(&allRooms, sizeof(allRooms));
                /* P: Traverse through all rooms in list_of_rooms */
                while (currRoom != NULL)
                {
                    roomInfo * room = (roomInfo *)(currRoom->value);
                    /* P: concatenate to the string the name of the room and the creator */
                    strcat(allRooms, room->roomName);
                    strcat(allRooms, ": ");
                    strcat(allRooms, room->creator);
                    node_t * roomListHead = room->peopleInRoom.head;
                    /* P: Traverse through the client in a room */
                    while (roomListHead != NULL)
                    {
                        char * name = (char *)roomListHead->value;
                        /* P: if the client is not the creator, concatenate the name to the string */
                        if (strcmp(name, room->creator)  != 0)
                        {
                            strcat(allRooms, ",");
                            strcat(allRooms, name);
                        }
                        roomListHead = roomListHead->next;
                    }
                    strcat(allRooms, "\n");
                    currRoom = currRoom->next;
                }
                pthread_mutex_unlock(&room_lock);
                /* P: Concatenate null terminating char */
                strcat(allRooms, "\0");
                /* P: Server writes to client the string created from traversing all rooms
                and the clients inside said rooms */
                server_response->msg_type = RMLIST;
                server_response->msg_len = strlen(allRooms) + 1;
                printf("%s recieves message %d length: %d %s\n", getName(list_of_names, client_fd), server_response->msg_type, server_response->msg_len, allRooms);
                wr_msg(client_fd, server_response, allRooms);
                pthread_mutex_lock(&audit_lock);
                audit_file = fopen(file_name,"a");
                time_t now;
                time(&now);
                fprintf(audit_file,"%s",ctime(&now));
                fprintf(audit_file," - Message sent to client %d for RMLIST:\n %s",client_fd,allRooms);
                fclose(audit_file);
                pthread_mutex_unlock(&audit_lock);
                /* P: Reset the string to 0 */
                memset(allRooms, 0, strlen(allRooms));
                
            }
        }
        /* P: Adds a client to the people inside of a room */
        else if (client_message->msg_type == RMJOIN) {
            pthread_mutex_lock(&room_lock);
            /* P: Check if room exists */
            if (!SearchForRoom(list_of_rooms, read_buffer)) {
                server_response->msg_type = ERMNOTFOUND;
                server_response->msg_len = 0;
                wr_msg(client_fd, server_response, NULL);
                pthread_mutex_lock(&audit_lock);
                audit_file = fopen(file_name,"a");
                time_t now;
                time(&now);
                fprintf(audit_file,"%s",ctime(&now));
                fprintf(audit_file," - Message sent to client %d for RMJOIN:\n\t- Room '%s' not found\n",client_fd,read_buffer);
                fclose(audit_file);
                pthread_mutex_unlock(&audit_lock);
            } else {
                node_t * currRoom = list_of_rooms->head;
                while (currRoom != NULL)
                {
                    roomInfo * room = (roomInfo *)(currRoom->value);
                    /* P: Find the room and the client's user to the people in the room */
                    if (strcmp(room->roomName, read_buffer) == 0)
                    {
                        pthread_mutex_lock(&user_lock);
                        char * user = getName(list_of_names, client_fd);
                        pthread_mutex_unlock(&user_lock);
                        addToRoom(&(room->peopleInRoom), user);
                        break;
                    }
                    currRoom= currRoom->next;
                }
                server_response->msg_type = OK;
                server_response->msg_len = 0;
                wr_msg(client_fd, server_response, NULL);
                pthread_mutex_lock(&audit_lock);
                audit_file = fopen(file_name,"a");
                time_t now;
                time(&now);
                fprintf(audit_file,"%s",ctime(&now));
                fprintf(audit_file," - Message sent to client %d for RMJOIN:\n\t- Room '%s' joined\n",client_fd,read_buffer);
                fclose(audit_file);  
                pthread_mutex_unlock(&audit_lock);    
            } 
            pthread_mutex_unlock(&room_lock);  
        }
        /* P: Leave from a room if you are not the creator */
        else if (client_message->msg_type == RMLEAVE) {
            pthread_mutex_lock(&room_lock);
            if (!SearchForRoom(list_of_rooms, read_buffer)) {
                server_response->msg_type = ERMNOTFOUND;
                server_response->msg_len = 0;
                wr_msg(client_fd, server_response, NULL);
                pthread_mutex_lock(&audit_lock);
                audit_file = fopen(file_name,"a");
                time_t now;
                time(&now);
                fprintf(audit_file,"%s",ctime(&now));
                fprintf(audit_file," - Message sent to client %d for RMLEAVE:\n\t- Room '%s' not found\n",client_fd,read_buffer);
                fclose(audit_file);
                pthread_mutex_unlock(&audit_lock);
            } else {
                node_t * currRoom = list_of_rooms->head;
                while (currRoom != NULL)
                {
                    roomInfo * room = (roomInfo *)(currRoom->value);
                    if (strcmp(room->roomName, read_buffer) == 0)
                    {
                        pthread_mutex_lock(&user_lock);
                        char * user = getName(list_of_names, client_fd);
                        pthread_mutex_unlock(&user_lock);
                        /* P: If client is the creator deney them access of leaving */
                        if (strcmp(room->creator, user) == 0) {
                            server_response->msg_type = ERMDENIED;
                            server_response->msg_len = 0;
                            wr_msg(client_fd, server_response, NULL);
                            pthread_mutex_lock(&audit_lock);
                            audit_file = fopen(file_name,"a");
                            time_t now;
                            time(&now);
                            fprintf(audit_file,"%s",ctime(&now));
                            fprintf(audit_file," - Message sent to client %d for RMLEAVE:\n\t- Cannot leave room '%s' not as creator\n",client_fd,read_buffer);
                            fclose(audit_file);
                            pthread_mutex_unlock(&audit_lock);
                            break;
                        /* P: If client is not in room, server writes ok protocal */
                        } else if (!SearchForName(&(room->peopleInRoom), user)) {
                            server_response->msg_type = OK;
                            server_response->msg_len = 0;
                            wr_msg(client_fd, server_response, NULL);
                            pthread_mutex_lock(&audit_lock);
                            audit_file = fopen(file_name,"a");
                            time_t now;
                            time(&now);
                            fprintf(audit_file,"%s",ctime(&now));
                            fprintf(audit_file," - Message sent to client %d for RMLEAVE:\n\t- Left room '%s' but not in room\n",client_fd,read_buffer);
                            fclose(audit_file);
                            pthread_mutex_unlock(&audit_lock);
                            break;
                        } else {
                            /* P: Traverse through the people in a room to find the index */
                            removeFromRoom(room, user);
                            server_response->msg_type = OK;
                            server_response->msg_len = 0;
                            wr_msg(client_fd, server_response, NULL);
                            pthread_mutex_lock(&audit_lock);
                            audit_file = fopen(file_name,"a");
                            time_t now;
                            time(&now);
                            fprintf(audit_file,"%s",ctime(&now));
                            fprintf(audit_file," - Message sent to client %d for RMLEAVE:\n\t- Left room '%s' \n",client_fd,read_buffer);
                            fclose(audit_file);
                            pthread_mutex_unlock(&audit_lock);
                            break;
                        }
                    
                    }
                    currRoom= currRoom->next;
                }
            }
            pthread_mutex_unlock(&room_lock);   
        }
        /* P: Handles sending and recieving messages from rooms*/
        else if (client_message->msg_type == RMSEND) {
            /* P: Parse buffer to get the room name */
            char * roomName = strtok(read_buffer, "\r");
            char * message = strtok(NULL, "\r");
            pthread_mutex_lock(&room_lock);
            if (!SearchForRoom(list_of_rooms, roomName)) {
                server_response->msg_type = ERMNOTFOUND;
                server_response->msg_len = 0;
                wr_msg(client_fd, server_response, NULL);
                pthread_mutex_lock(&audit_lock);
                audit_file = fopen(file_name,"a");
                time_t now;
                time(&now);
                fprintf(audit_file,"%s",ctime(&now));
                fprintf(audit_file," - Message sent to client %d for RMSEND:\n\t- Room does not exist \n",client_fd);
                fclose(audit_file);
                pthread_mutex_unlock(&audit_lock);
            } else {
                node_t * currRoom = list_of_rooms->head;
                pthread_mutex_lock(&user_lock);
                char * user_name = getName(list_of_names, client_fd);
                pthread_mutex_unlock(&user_lock);
                while (currRoom != NULL)
                {
                    roomInfo * room = (roomInfo *)(currRoom->value);
                    if (strcmp(room->roomName, roomName) == 0)
                    {
                        /* P: Create a string to create a new message that can be concatenated on */
                        int msgSize = strlen(roomName) + 2 + strlen(user_name) + 1 + strlen(message) + 1;
                        char newMessage[msgSize];
                        bzero(&newMessage, sizeof(newMessage));
                        strcat(newMessage, roomName);
                        strcat(newMessage, "\r\n");
                        strcat(newMessage, user_name);
                        strcat(newMessage, "\r");
                        strcat(newMessage, message);
                        newMessage[msgSize-1] = '\0';
                        if (!SearchForName(&(room->peopleInRoom), user_name)) {
                            server_response->msg_type = ERMDENIED;
                            server_response->msg_len = 0;
                            wr_msg(client_fd, server_response, NULL);
                            bzero(&newMessage, sizeof(newMessage));
                            break;
                        } else {
                            petr_header *server_response2 = malloc(sizeof(petr_header));      
                            server_response2->msg_type = OK;
                            server_response2->msg_len = 0;
                            wr_msg(client_fd, server_response2, NULL);
                            pthread_mutex_lock(&audit_lock);
                            audit_file = fopen(file_name,"a");
                            time_t now;
                            time(&now);
                            fprintf(audit_file,"%s",ctime(&now));
                            fprintf(audit_file," - Message sent to client %d for RMSEND:\n\t- Message to room '%s' sent\n",client_fd,roomName);
                            fclose(audit_file);
                            pthread_mutex_unlock(&audit_lock);
                            printf("%s receives message %d length: %d\n", getName(list_of_names, client_fd), server_response2->msg_type, server_response2->msg_len);
                            pthread_mutex_lock(&user_lock);
                            node_t * roomListHead = room->peopleInRoom.head;
                            while (roomListHead != NULL)
                            {
                                /* P: send all clients that is not current the message */
                                char * name = (char *)roomListHead->value;
                                if (strcmp(name, user_name) != 0) {
                                    int other_fd = getFD(list_of_names, name);
                                    server_response2->msg_type = RMRECV;
                                    server_response2->msg_len = msgSize;
                                    wr_msg(other_fd, server_response2, newMessage);
                                    pthread_mutex_lock(&audit_lock);
                                    audit_file = fopen(file_name,"a");
                                    time_t now;
                                    time(&now);
                                    fprintf(audit_file,"%s",ctime(&now));
                                    fprintf(audit_file," - Message sent from room '%s':\n%s\n",roomName,newMessage);
                                    fclose(audit_file);
                                    pthread_mutex_unlock(&audit_lock);
                                    printf("%s receives message %d length: %d %s\n", getName(list_of_names, other_fd), server_response2->msg_type, server_response2->msg_len, newMessage);
                                }
                            
                                roomListHead = roomListHead->next;
                            }
                            pthread_mutex_unlock(&user_lock);
                            bzero(&newMessage, sizeof(newMessage));
                            break;
                        }
                    }
                    currRoom = currRoom->next;
                }
            }
            pthread_mutex_unlock(&room_lock);
        }
        // J: Handles the /chat <username> <message> for when a user wants
        // to send a private message to another user.
        else if(client_message->msg_type == USRSEND) {
            char * to_user = strtok(read_buffer, "\r");
            char * message = strtok(NULL, "\r");
            pthread_mutex_lock(&user_lock);
            char * from_user = getName(list_of_names, client_fd);
            pthread_mutex_unlock(&user_lock);
            int len1 = strlen(from_user);
            int len2 = strlen(message);
            int message_len = strlen(from_user) + 1 + strlen(message) + 1;
            char from_message[message_len];
            bzero(&from_message, sizeof(from_message));
            memset(from_message, 0, sizeof(from_message));
            strcat(from_message, from_user);
            strcat(from_message, "\r");
            strcat(from_message, message);
            strcat(from_message, "\0");
            pthread_mutex_lock(&user_lock);
            //J: If to_user not found than send back EUSRNOTFOUND back to client
            if(!SearchForName(list_of_names,to_user))
            {
                server_response->msg_type = EUSRNOTFOUND;
                server_response->msg_len = 0;
                wr_msg(client_fd, server_response, NULL);
                pthread_mutex_lock(&audit_lock);
                audit_file = fopen(file_name,"a");
                time_t now;
                time(&now);
                fprintf(audit_file,"%s",ctime(&now));
                fprintf(audit_file," - Message sent to client %d for USRSEND:\n\t- User '%s' not found\n",client_fd,to_user);
                fclose(audit_file);
                pthread_mutex_unlock(&audit_lock);
            }
            else 
            {
                //J: If user found than respond back with OK message and then send text message
                bzero(server_response, sizeof(petr_header));
                server_response->msg_type = OK;
                server_response->msg_len = 0;
                wr_msg(client_fd, server_response, NULL);
                pthread_mutex_lock(&audit_lock);
                audit_file = fopen(file_name,"a");
                time_t now;
                time(&now);
                fprintf(audit_file,"%s",ctime(&now));
                fprintf(audit_file," - Message sent to client %d for USRSEND:\n\t- Message to user '%s' sent\n",client_fd,to_user);
                fclose(audit_file);
                pthread_mutex_unlock(&audit_lock);
                printf("%s recieves message %d length: %d\n", getName(list_of_names, client_fd), server_response->msg_type, server_response->msg_len);

                char * from_user = getName(list_of_names, client_fd);
                server_response->msg_type = USRRECV;
                server_response->msg_len = message_len; // + 10
                int to_client_fd = getFD(list_of_names, to_user);
                wr_msg(to_client_fd, server_response, from_message);
                pthread_mutex_lock(&audit_lock);
                audit_file = fopen(file_name,"a");
                time_t now2;
                time(&now2);
                fprintf(audit_file,"%s",ctime(&now2));
                fprintf(audit_file," - Message sent to client %d for USRSEND:\n\t- Message sent to user '%s' from user '%s'\n %s\n",to_client_fd,to_user,from_user,from_message);
                fclose(audit_file);
                pthread_mutex_unlock(&audit_lock);
                printf("%s recieves message %d length: %d %s\n", getName(list_of_names, to_client_fd), server_response->msg_type, server_response->msg_len, from_message);
                memset(from_message, 0, sizeof(from_message));
            }
            pthread_mutex_unlock(&user_lock);
        }
        free(server_response);
        free(read_buffer);
    }
    return NULL;
}

//Function running in thread
void *process_client(void *clientfd_ptr) {
    int client_fd = *(int *)clientfd_ptr;
    free(clientfd_ptr);
    int received_size;
    fd_set read_fds;
    int retval;
    
    while (1) {
 

        job* newJob = malloc(sizeof(newJob));
        newJob->client_message = malloc(sizeof(petr_header));
        newJob->client_fd = client_fd;
        rd_msgheader(client_fd, newJob->client_message);
        char* read_buffer = malloc(newJob->client_message->msg_len);
        read(client_fd, read_buffer, newJob->client_message->msg_len);
        newJob->message = malloc(newJob->client_message->msg_len); 
        newJob->message = read_buffer;
        if (newJob->client_message->msg_type == OK)
        {
            newJob->client_message->msg_type = LOGOUT;
        }


        total_num_msg++;
        // print buffer which contains the client contents
        printf("Receive message from client: %s\n", getName(list_of_names, client_fd));
        printf("Total number of received messages: %d\n", total_num_msg);
        sbuf_insert(&sbuf, newJob);
        pthread_mutex_lock(&audit_lock);
        audit_file = fopen(file_name,"a");
        time_t now;
        time(&now);
        fprintf(audit_file,"%s",ctime(&now));
        fprintf(audit_file," - Inserting job %d to the buffer by thread %ld\n",newJob->client_message->msg_type,pthread_self());
        fclose(audit_file);
        pthread_mutex_unlock(&audit_lock);
        if (newJob->client_message->msg_type == LOGOUT)
        {
            petr_header *server_response2 = malloc(sizeof(petr_header));
            server_response2->msg_type = OK;
            server_response2->msg_len = 0;
            wr_msg(client_fd, server_response2, NULL);
            break;
        }
    }
    // Close the socket at the enk
    pthread_mutex_lock(&audit_lock);
    audit_file = fopen(file_name,"a");
    time_t now;
    time(&now);
    fprintf(audit_file,"%s",ctime(&now));
    fprintf(audit_file," - Terminated client thread %ld for client %d\n",pthread_self(),client_fd);
    fclose(audit_file);  
    close(client_fd);
    pthread_mutex_unlock(&audit_lock);
    return NULL;
}

void run_server(int server_port) {
    listen_fd = server_init(server_port); // Initiate server and start listening on specified port
    int client_fd;
    struct sockaddr_in client_addr;
    int client_addr_len = sizeof(client_addr);
    pthread_t tid;

    //J: Initial messages between client and server
    petr_header *client_message = malloc(sizeof(petr_header));
    list_of_names = malloc(sizeof(List_t));
    list_of_rooms = malloc(sizeof(List_t));
    list_of_rooms->head = NULL;
    list_of_rooms->length = 0;
    audit_file = fopen(file_name, "w");
    fclose(audit_file);
    sbuf_init(&sbuf, BUFFER_SIZE);



    while (1) {
        // Wait and Accept the connection from client
        printf("Wait for new client connection\n");
        int *client_fd = malloc(sizeof(int));
        *client_fd = accept(listen_fd, (SA *)&client_addr, (socklen_t*)&client_addr_len);
        if (*client_fd < 0) {
            printf("server acccept failed\n");
            exit(EXIT_FAILURE);
        } 
        else 
        {
            printf("Client connection accepted\n");
            usleep(21000);
            //J: Read the message from the client
            char* read_buffer = malloc(sizeof(char));
            rd_msgheader(*client_fd, client_message);
            read(*client_fd,read_buffer,client_message->msg_len);
            petr_header *server_response = malloc(sizeof(petr_header));
            char* write_buffer = malloc(sizeof(char));
            pthread_mutex_lock(&user_lock);
            //J: Check to see if user name is already taken
            if(SearchForName(list_of_names,read_buffer))
            {
                //J: Respond back to the client stating that the user already exists
                petr_header *error_header = malloc(sizeof(petr_header));
                error_header->msg_type = EUSREXISTS;
                error_header->msg_len = 0;
                wr_msg(*client_fd, error_header, NULL);
                write(*client_fd,write_buffer,error_header->msg_len);
                pthread_mutex_lock(&audit_lock);
                audit_file = fopen(file_name,"a");
                time_t now;
                time(&now);
                fprintf(audit_file,"%s",ctime(&now));
                fprintf(audit_file," - Denied user: %s client_fd: %d because user already taken\n",read_buffer,*client_fd);
                fclose(audit_file);
                pthread_mutex_unlock(&audit_lock);
                free(error_header);
                free(write_buffer);
                close(*client_fd);
            }
            else
            {
                //J: If user not found than insert it into the list of users
                printf("%s sent message 16 length: %ld %s\n", read_buffer, strlen(read_buffer), read_buffer);
                InsertAtFront(list_of_names,read_buffer,*client_fd);
                //J: Write (respond) back to the client logged in
                server_response->msg_type = OK;
                server_response->msg_len = 0;
                wr_msg(*client_fd, server_response, NULL);
                write(*client_fd,write_buffer,server_response->msg_len);
                printf("Logged in new user: %s\n",read_buffer);
                pthread_mutex_lock(&audit_lock);
                audit_file = fopen(file_name,"a");
                time_t now;
                time(&now);

                fprintf(audit_file,"%s",ctime(&now));
                fprintf(audit_file," - Added user: %s client_fd: %d\n",read_buffer,*client_fd);
                fclose(audit_file);
                pthread_mutex_unlock(&audit_lock);
                pthread_create(&tid, NULL, process_client, (void *)client_fd);
                pthread_mutex_lock(&audit_lock);
                audit_file = fopen(file_name,"a");
                time_t now2;
                time(&now2);
                fprintf(audit_file,"%s",ctime(&now2));
                fprintf(audit_file," - Created client thread %ld for client %d\n",tid,*client_fd);
                fclose(audit_file); 
                pthread_mutex_unlock(&audit_lock);    
            }
            pthread_mutex_unlock(&user_lock);
        }
    } 
    bzero(buffer, BUFFER_SIZE);
    close(listen_fd);
    return;
}

int main(int argc, char *argv[]) {
    int opt;
    unsigned int port = 0;
    int job_threads = 2;
    int flag_j = 0;

    while ((opt = getopt(argc, argv, "hj:p:")) != -1) {
        switch (opt) {
        case 'h':
            printf("./bin/petr_server [-h][-j N] PORT_NUMBER AUDIT_FILENAME\n");
            printf("\n-h\t\tDisplays this help meny, and returns EXIT_SUCCESS.\n");
            printf("-j N\t\tNumber of job threads. Default to 2.\n");
            printf("AUDIT_FILENAME\tFile to ouput Audit Log messages to.\n");
            printf("PORT_NUMBER\tPort number to listen on.\n");
            return EXIT_SUCCESS;
        case 'j':
            job_threads = atoi(optarg);
            flag_j = 1;
            break;
        default: /* '?' */
            fprintf(stderr, "./bin/petr_server [-h][-j N] PORT_NUMBER AUDIT_FILENAME\n");
            exit(EXIT_FAILURE);
        }
    }
    //J: If the user included number of threads than check if they also included port number and audit file.
    if (flag_j)
    {
        if(argc != 5)
        {
            fprintf(stderr, "ERROR: Port Number and Audit File must be included.\n");
            fprintf(stderr, "./bin/petr_server [-h][-j N] PORT_NUMBER AUDIT_FILENAME\n");
            exit(EXIT_FAILURE);
        }
        port = atoi(argv[3]);
        file_name = argv[4];
    }
    else 
    {
        //J: If number of threads not included, check to see if they have the minimum 
        //   amount of positional arguments which include the port number and audit file. 
        if(argc != 3)
        {
            fprintf(stderr, "ERROR: Port Number and Audit File must be included.\n");
            fprintf(stderr, "./bin/petr_server [-h][-j N] PORT_NUMBER AUDIT_FILENAME\n");
            exit(EXIT_FAILURE);
        }
        port = atoi(argv[1]);
        file_name = argv[2];
    }
    
    if (port == 0) {
        fprintf(stderr, "ERROR: Port number for server to listen is not given\n");
        fprintf(stderr, "Server Application Usage: %s -p <port_number>\n",
                argv[0]);
        exit(EXIT_FAILURE);
    }

    // job_threads = 1;
    printf("Creating %d job threads\n", job_threads);
    pthread_t tid;
    for (int i = 0; i < job_threads; i++)
    {
        pthread_create(&tid, NULL, process_job, NULL);
    }


    signal(SIGINT, sigint_handler);
    run_server(port);
    
    return 0;
}