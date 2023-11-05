#include "server.h"
#include "protocol.h"
#include "linkedlist.h"
#include <pthread.h>
#include <semaphore.h>
#include <signal.h>
#include <errno.h>

int listen_fd;
int sigFlag = 0;
int readCnt = 0;
char buffer[BUFFER_SIZE];
FILE *l_file;
stats_t curStats;
poll_t pollArray[32];
list_t *userList;
sem_t bufferMutex, statsMutex, userMutexR, userMutexW, pollMutex, voteMutex, flagMutex;
sem_t pollLocks[32];

void Sem_init(sem_t *sem, int pshared, int value) {
    if (sem_init(sem, pshared, value) < 0) {
        printf("sem_init error\n");
        exit(EXIT_FAILURE);
    }
}

void P(sem_t *sem) {
    if (sem_wait(sem) < 0) {
        printf("sem_wait error\n");
        exit(EXIT_FAILURE);
    }
}

void V(sem_t *sem) {
    if (sem_post(sem) < 0) {
        printf("sem_post error \n");
        exit(EXIT_FAILURE);
    }
}

void sigint_handler(int sig) {
    if (sig == SIGINT) {
        P(&flagMutex);
        sigFlag = 1;
        V(&flagMutex);
    }
}

void addUserReader() {
    P(&userMutexR);
    readCnt++;
    if (readCnt == 1) {
        P(&userMutexW);
    }
    V(&userMutexR);
}

void removeUserReader() {
    P(&userMutexR);
    readCnt--;
    if (readCnt == 0) {
        V(&userMutexW);
    }
    V(&userMutexR);
}

FILE *Fopen(char *filename, char *mode) {
    FILE *file = fopen(filename, mode);
    if (file == NULL) {
        printf("fopen error on file %s\n", filename);
        exit(2);
    }
    return file;
}

void Close(int fd) {
    if (close(fd) < 0) {
        printf("close error on file descriptor %d\n", fd);
        exit(2);
    }
}

void handleSigFlag() {
    addUserReader();
    node_t *current = userList->head;
    while (current != NULL) {
        if (((user_t *) current->data)->socket_fd >= 0) {
            pthread_t ctid = ((user_t *) current->data)->tid;
            pthread_kill(ctid, SIGINT);
            pthread_join(ctid, NULL);
        }
        current = current->next;
    }
    removeUserReader();

    for (int i = 0; i < 32; i++) {
        P(&pollLocks[i]);
        P(&pollMutex);
        if (pollArray[i].question != NULL) {
            int numChoices = 0;
            for (int j = 0; j < 4; j++) {
                if (pollArray[i].options[j].text != NULL) {
                    numChoices++;
                }
            }
            printf("%s;%d", pollArray[i].question, numChoices);
            for (int j = 0; j < 4; j++) {
                if (pollArray[i].options[j].text != NULL) {
                    printf(";%s,%d", pollArray[i].options[j].text, pollArray[i].options[j].voteCnt);
                }
            }
            printf("\n");
        }
        V(&pollMutex);
        V(&pollLocks[i]);
    }

    addUserReader();
    current = userList->head;
    while (current != NULL) {
        fprintf(stderr, "%s, %d\n", ((user_t *) current->data)->username, ((user_t *) current->data)->pollVotes);
        current = current->next;
    }
    removeUserReader();

    P(&statsMutex);
    fprintf(stderr, "%d, %d, %d\n", curStats.clientCnt, curStats.threadCnt, curStats.totalVotes);
    V(&statsMutex);
    P(&userMutexW);
    DestroyList(&userList);
    V(&userMutexW);

    sigFlag = 0;
}

int User_Comparator(const void* u1, const void* u2) {
    user_t *user1 = (user_t *) u1;
    user_t *user2 = (user_t *) u2;
    if (strcmp(user1->username, user2->username) < 0) {
        return -1;
    }
    else if (strcmp(user1->username, user2->username) > 0) {
        return 1;
    }
    else {
        return 0;
    }
}

void User_Printer(void* data, void* fp) {
    user_t *user = data;
    FILE *file = fp;
    printf("%s, %d, %ld, %d\n", user->username, user->socket_fd, user->tid, user->pollVotes);
}

void User_Deleter(void* data) {
    if (data != NULL) {
        user_t *user = (user_t *) data;
        free(user->username);
        free(user);
    }
}

void buildPList(uint32_t localVotes) {
    for (int i = 0; i < 32; i++) {
        P(&pollLocks[i]);
        P(&pollMutex);
        if (pollArray[i].question != NULL) {
            strcat(buffer, "Poll ");
            char num[3];
            sprintf(num, "%d", i);
            strcat(buffer, num);
            strcat(buffer, " - ");
            strcat(buffer, pollArray[i].question);
            if (((localVotes >> i) & 1) == 0) {
                strcat(buffer, " -");
                for (int j = 0; j < 4; j++) {
                    if (pollArray[i].options[j].text != NULL) {
                        if (j != 0) {
                            strcat(buffer, ",");
                        }
                        strcat(buffer, " ");
                        char num2[2];
                        sprintf(num2, "%d", j);
                        strcat(buffer, num2);
                        strcat(buffer, ":");
                        strcat(buffer, pollArray[i].options[j].text);
                    }
                }
            }
            strcat(buffer, "\n");
        }
        V(&pollMutex);
        V(&pollLocks[i]);
    }
}

int handleStats(uint32_t localVotes) {
    if (localVotes == 0) {
        return -1;
    }
    int i = atoi(buffer);
    bzero(buffer, BUFFER_SIZE);
    if (i >= 0 && i <= 31) {
        P(&pollLocks[i]);
        P(&pollMutex);
        if (pollArray[i].question != NULL) {
            strcat(buffer, "Poll ");
            char num[3];
            sprintf(num, "%d", i);
            strcat(buffer, num);
            strcat(buffer, " - ");
            for (int j = 0; j < 4; j++) {
                if (pollArray[i].options[j].text != NULL) {
                    if (j != 0) {
                        strcat(buffer, ",");
                    }
                    strcat(buffer, pollArray[i].options[j].text);
                    strcat(buffer, ":");
                    char num2[2];
                    sprintf(num2, "%d", pollArray[i].options[j].voteCnt);
                    strcat(buffer, num2);
                }
            }
            strcat(buffer, "\n");
        }
        else {
            V(&pollMutex);
            V(&pollLocks[i]);
            return -1;
        }
        V(&pollMutex);
        V(&pollLocks[i]);
    }
    else if (i == -1) {
        for (int j = 0; j < 31; j++) {
            P(&pollLocks[j]);
            P(&pollMutex);
            if (pollArray[j].question != NULL && ((localVotes >> j) & 1) == 1) {
                strcat(buffer, "Poll ");
                char num[3];
                sprintf(num, "%d", j);
                strcat(buffer, num);
                strcat(buffer, " - ");
                for (int k = 0; k < 4; k++) {
                    if (pollArray[j].options[k].text != NULL) {
                        if (k != 0) {
                            strcat(buffer, ",");
                        }
                        strcat(buffer, pollArray[j].options[k].text);
                        strcat(buffer, ":");
                        char num2[2];
                        sprintf(num2, "%d", pollArray[j].options[k].voteCnt);
                        strcat(buffer, num2);
                    }
                }
                strcat(buffer, "\n");
            }
            V(&pollMutex);
            V(&pollLocks[j]);
        }
    }
    else {
        return -1;
    }
    return 0;
}

int getSubstrings(char *str,  char delim, char ** array, int maxSize) {
    if (str == NULL || array == NULL || maxSize < 1) {
        return -1;
    }
    else if (*str == '\0' || delim == '\0') {
        return 0;
    }
    
    char *current = str;
    int numSubs = 0;
    while (numSubs < maxSize) {
        if (*current == '\0') {
            *array = str;
            numSubs++;
            break;
        }
        else if (*current == delim) {
            *current = '\0';
            *array = str;
            current++;
            array++;
            str = current;
            numSubs++;
        }
        else {
            current++;
        }
    }
    return numSubs;
}

int pollArray_init(char *poll_file) {
    FILE *p_file = Fopen(poll_file, "r");
    char *line = NULL;
    size_t len = 0;
    int pollCount = 0;

    while (getline(&line, &len, p_file) != -1) {
        char *dLine = malloc((len+1)*sizeof(char));
        strcpy(dLine, line);
        char **substrings = malloc(6*sizeof(char *));
        getSubstrings(dLine, ';', substrings, 6);
        
        char *pQuestion = malloc((strlen(substrings[0])+1)*sizeof(char));
        strcpy(pQuestion, substrings[0]);
        int numChoices = atoi(substrings[1]);

        choice_t pOptions[4];
        for (int j = 0; j < numChoices; j++) {
            char *cLine = malloc((strlen(substrings[2+j])+1)*sizeof(char));
            strcpy(cLine, substrings[2+j]);
            char **choiceSub = malloc(2*sizeof(char *));
            getSubstrings(cLine, ',', choiceSub, 2);

            char *oText = malloc((strlen(choiceSub[0])+1)*sizeof(char));
            strcpy(oText, choiceSub[0]);
            pOptions[j] = (choice_t) {oText, atoi(choiceSub[1])};
            pollArray[pollCount].options[j] = pOptions[j];

            free(choiceSub);
            free(cLine);
        }
        for (int j = numChoices; j < 4; j++) {
            pOptions[j] = (choice_t) {NULL, 0};
            pollArray[pollCount].options[j] = pOptions[j];
        }
        pollArray[pollCount].question = pQuestion;
        pollCount++;
        
        free(substrings);
        free(dLine);
    }
    for (int j = pollCount; j < 32; j++) {
        choice_t emptyOptions[4];
        for (int k = 0; k < 4; k++) {
            emptyOptions[k] = (choice_t) {NULL, 0};
            pollArray[pollCount].options[k] = emptyOptions[k];
        }
        pollArray[j].question = NULL;
    }

    if (fclose(p_file) != 0) {
        printf("fclose error on file %s\n", poll_file);
        exit(2);
    }
    free(line);
    return pollCount;
}

int sock_init(int server_port) {
    int sockfd;
    struct sockaddr_in servaddr;

    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd == -1) {
        printf("socket creation error\n");
        exit(EXIT_FAILURE);
    }

    bzero(&servaddr, sizeof(servaddr));

    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    servaddr.sin_port = htons(server_port);

    int opt = 1;
    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEPORT, (char *)&opt, sizeof(opt)) < 0) {
	    perror("setsockopt error\n");
        exit(EXIT_FAILURE);
    }

    if ((bind(sockfd, (SA*)&servaddr, sizeof(servaddr))) != 0) {
        printf("socket bind error\n");
        exit(EXIT_FAILURE);
    }

    if ((listen(sockfd, 1)) != 0) {
        printf("listen error\n");
        exit(EXIT_FAILURE);
    }

    return sockfd;
}

int server_init(char *poll_file) {
    curStats.clientCnt = 0;
    curStats.threadCnt = 0;
    curStats.totalVotes = 0;

    int numPolls = pollArray_init(poll_file);
    userList = CreateList(&User_Comparator, &User_Printer, &User_Deleter);

    Sem_init(&bufferMutex, 0, 1);
    Sem_init(&statsMutex, 0, 1);
    Sem_init(&userMutexR, 0, 1);
    Sem_init(&userMutexW, 0, 1);
    Sem_init(&pollMutex, 0, 1);
    for (int i = 0; i < 32; i++) {
        Sem_init(&pollLocks[i], 0, 1);
    }
    Sem_init(&voteMutex, 0, 1);
    Sem_init(&flagMutex, 0, 1);

    struct sigaction myaction = {{0}};
    myaction.sa_handler = sigint_handler;
    if (sigaction(SIGINT, &myaction, NULL) == -1) {
        printf("signal handler failed to install\n");
    }

    return numPolls;
}

void *client_thread(void *clientInfo) {
    if (pthread_detach(pthread_self()) != 0) {
        printf("thread detach error\n");
        exit(EXIT_FAILURE);
    }
    addUserReader();
    user_t *user = clientInfo;
    int client_fd = user->socket_fd;
    uint32_t localVotes = user->pollVotes;
    removeUserReader();
    P(&userMutexW);
    user->tid = pthread_self();
    V(&userMutexW);

    while (1) {
        petrV_header h;
        if (rd_msgheader(client_fd, &h) < 0) {
            P(&userMutexW);
            user->socket_fd = -1;
            user->pollVotes = localVotes;
            V(&userMutexW);
            P(&flagMutex);
            if (errno == EINTR && sigFlag) {
                sigFlag = 0;
            }
            V(&flagMutex);
            break;
        }
        if (h.msg_type == LOGOUT) {
            P(&userMutexW);
            user->socket_fd = -1;
            user->pollVotes = localVotes;
            V(&userMutexW);
            addUserReader();
            P(&voteMutex);
            fprintf(l_file, "%s LOGOUT\n", user->username);
            V(&voteMutex);
            removeUserReader();

            petrV_header *wh = calloc(1, sizeof(petrV_header));
            *wh = (petrV_header) {0, OK};
            wr_msg(client_fd, wh, "");
            free(wh);
            break;
        }
        else if (h.msg_type == PLIST) {
            P(&bufferMutex);
            bzero(buffer, BUFFER_SIZE);
            buildPList(localVotes);
            petrV_header *wh = calloc(1, sizeof(petrV_header));
            *wh = (petrV_header) {(strlen(buffer)+1)*sizeof(char), PLIST};
            wr_msg(client_fd, wh, buffer);
            free(wh);
            V(&bufferMutex);

            addUserReader();
            P(&voteMutex);
            fprintf(l_file, "%s PLIST\n", user->username);
            V(&voteMutex);
            removeUserReader();

            P(&flagMutex);
            if (sigFlag) {
                P(&userMutexW);
                user->socket_fd = -1;
                user->pollVotes = localVotes;
                V(&userMutexW);
                sigFlag = 0;
                V(&flagMutex);
                break;
            }
            V(&flagMutex);
            continue;
        }

        P(&bufferMutex);
        bzero(buffer, BUFFER_SIZE);
        if (read(client_fd, buffer, h.msg_len*sizeof(char)) < 0) {
            V(&bufferMutex);
            P(&userMutexW);
            user->socket_fd = -1;
            user->pollVotes = localVotes;
            V(&userMutexW);
            P(&flagMutex);
            if (errno == EINTR && sigFlag) {
                sigFlag = 0;
            }
            V(&flagMutex);
            break;
        }
        if (h.msg_type == VOTE) {
            char *msg = malloc((strlen(buffer)+1)*sizeof(char));
            strcpy(msg, buffer);
            char **sub = malloc(2*sizeof(char *));
            getSubstrings(msg, ' ', sub, 2);
            int p = atoi(sub[0]);
            int o = atoi(sub[1]);
            free(sub);
            free(msg);

            petrV_header *wh = calloc(1, sizeof(petrV_header));
            if (p >= 0 && p <= 31) {
                P(&pollLocks[p]);
                P(&pollMutex);
                if (pollArray[p].question != NULL) {
                    if (o >= 0 && o <= 3 && pollArray[p].options[o].text != NULL) {
                        if (((localVotes >> p) & 1) == 0) {
                            pollArray[p].options[o].voteCnt++;
                            P(&userMutexW);
                            uint32_t num = 1 << p;
                            localVotes |= num;
                            user->pollVotes = localVotes;
                            V(&userMutexW);
                            P(&statsMutex);
                            curStats.totalVotes++;
                            V(&statsMutex);

                            *wh = (petrV_header) {0, OK};
                            wr_msg(client_fd, wh, "");
                            addUserReader();
                            P(&voteMutex);
                            fprintf(l_file, "%s VOTE %d %d %d\n", user->username, p, o, user->pollVotes);
                            V(&voteMutex);
                            removeUserReader();
                        }
                        else {
                            *wh = (petrV_header) {0, EPDENIED};
                            wr_msg(client_fd, wh, "");
                        }
                    }
                    else {
                        *wh = (petrV_header) {0, ECNOTFOUND};
                        wr_msg(client_fd, wh, "");
                    }
                }
                else {
                    *wh = (petrV_header) {0, EPNOTFOUND};
                    wr_msg(client_fd, wh, "");
                }
                V(&pollMutex);
                V(&pollLocks[p]);
            }
            else {
                *wh = (petrV_header) {0, EPNOTFOUND};
                wr_msg(client_fd, wh, "");
            }
            free(wh);
            V(&bufferMutex);

            P(&flagMutex);
            if (sigFlag) {
                P(&userMutexW);
                user->socket_fd = -1;
                user->pollVotes = localVotes;
                V(&userMutexW);
                sigFlag = 0;
                V(&flagMutex);
                break;
            }
            V(&flagMutex);
            continue;
        }
        else if (h.msg_type == STATS) {
            petrV_header *wh = calloc(1, sizeof(petrV_header));
            if (handleStats(localVotes) < 0) {
                *wh = (petrV_header) {0, EPDENIED};
                wr_msg(client_fd, wh, "");
            }
            else {
                *wh = (petrV_header) {(strlen(buffer)+1)*sizeof(char), STATS};
                wr_msg(client_fd, wh, buffer);
                addUserReader();
                P(&voteMutex);
                fprintf(l_file, "%s STATS %d\n", user->username, user->pollVotes);
                V(&voteMutex);
                removeUserReader();
            }
            free(wh);
            V(&bufferMutex);

            P(&flagMutex);
            if (sigFlag) {
                P(&userMutexW);
                user->socket_fd = -1;
                user->pollVotes = localVotes;
                V(&userMutexW);
                sigFlag = 0;
                V(&flagMutex);
                break;
            }
            V(&flagMutex);
            continue;
        }
        V(&bufferMutex);

        P(&flagMutex);
        if (sigFlag) {
            P(&userMutexW);
            user->socket_fd = -1;
            user->pollVotes = localVotes;
            V(&userMutexW);
            sigFlag = 0;
            V(&flagMutex);
            break;
        }
        V(&flagMutex);
    }
    bzero(buffer, BUFFER_SIZE);
    Close(client_fd);
    return NULL;
}

void run_server(int server_port, char *poll_file, char *log_file) {
    listen_fd = sock_init(server_port);
    struct sockaddr_in client_addr;
    int client_addr_len = sizeof(client_addr);
    pthread_t tid;

    int numPolls = server_init(poll_file);
    l_file = Fopen(log_file, "w");
    printf("Server initialized with %d polls.\n", numPolls);
    printf("Currently listening on port %d.\n", server_port);

    while (1) {
        int client_fd = accept(listen_fd, (SA*)&client_addr, (socklen_t * restrict)&client_addr_len);
        if (client_fd < 0) {
            P(&flagMutex);
            if (errno == EINTR && sigFlag) {
                Close(listen_fd);
                handleSigFlag();
                V(&flagMutex);
                return;
            }
            V(&flagMutex);
            continue;
        }

        petrV_header h;
        if (rd_msgheader(client_fd, &h) < 0) {
            P(&flagMutex);
            if (errno == EINTR && sigFlag) {
                Close(listen_fd);
                handleSigFlag();
                V(&flagMutex);
                return;
            }
            V(&flagMutex);
            continue;
        }
        char *username = malloc(h.msg_len*sizeof(char));
        if (read(client_fd, username, h.msg_len*sizeof(char)) < 0) {
            P(&flagMutex);
            if (errno == EINTR && sigFlag) {
                Close(listen_fd);
                handleSigFlag();
                V(&flagMutex);
                return;
            }
            V(&flagMutex);
            continue;
        }
        P(&statsMutex);
        curStats.clientCnt++;
        V(&statsMutex);

        if (h.msg_type == LOGIN) {
            if (strchr(username, ' ') != NULL) {
                petrV_header *wh = calloc(1, sizeof(petrV_header));
                *wh = (petrV_header) {0, ESERV};
                wr_msg(client_fd, wh, "");
                free(wh);
                P(&voteMutex);
                fprintf(l_file, "REJECT %s\n", username);
                V(&voteMutex);
                Close(client_fd);
                free(username);

                P(&flagMutex);
                if (sigFlag) {
                    Close(listen_fd);
                    handleSigFlag();
                    V(&flagMutex);
                    return;
                }
                V(&flagMutex);
                continue;
            }

            addUserReader();
            user_t *user = malloc(sizeof(user_t));
            *user = (user_t) {username, client_fd, tid, 0};
            node_t *userNode = FindInList(userList, user);
            if (userNode != NULL && ((user_t *) userNode->data)->socket_fd >= 0) {
                removeUserReader();
                petrV_header *wh = calloc(1, sizeof(petrV_header));
                *wh = (petrV_header) {0, EUSRLGDIN};
                wr_msg(client_fd, wh, "");
                free(wh);
                P(&voteMutex);
                fprintf(l_file, "REJECT %s\n", username);
                V(&voteMutex);
                Close(client_fd);
                free(username);
                free(user);

                P(&flagMutex);
                if (sigFlag) {
                    Close(listen_fd);
                    handleSigFlag();
                    V(&flagMutex);
                    return;
                }
                V(&flagMutex);
                continue;
            }
            removeUserReader();

            P(&userMutexW);
            if (userNode != NULL) {
                ((user_t *) userNode->data)->socket_fd = client_fd;
                free(user);
                user = userNode->data;
                P(&voteMutex);
                fprintf(l_file, "RECONNECTED %s\n", username);
                V(&voteMutex);
                free(username);
            }
            else {
                InsertAtHead(userList, user);
                P(&voteMutex);
                fprintf(l_file, "CONNECTED %s\n", username);
                V(&voteMutex);
            }
            V(&userMutexW);

            petrV_header *wh = calloc(1, sizeof(petrV_header));
            *wh = (petrV_header) {0, OK};
            wr_msg(client_fd, wh, "");
            free(wh);
            if (pthread_create(&tid, NULL, client_thread, user) != 0) {
                printf("Thread create failed\n");
                exit(EXIT_FAILURE);
            }
            P(&statsMutex);
            curStats.threadCnt++;
            V(&statsMutex);

            P(&flagMutex);
            if (sigFlag) {
                Close(listen_fd);
                handleSigFlag();
                V(&flagMutex);
                return;
            }
            V(&flagMutex);
        }
        else {
            Close(client_fd);
            free(username);
        }
    }
    bzero(buffer, BUFFER_SIZE);
    Close(listen_fd);
    return;
}

int main(int argc, char *argv[]) {
    int opt;
    while ((opt = getopt(argc, argv, "h")) != -1) {
        switch (opt) {
            case 'h':
                fprintf(stderr, USAGE_MSG);
                exit(EXIT_FAILURE);
        }
    }

    // 3 positional arguments necessary
    if (argc != 4) {
        fprintf(stderr, USAGE_MSG);
        exit(EXIT_FAILURE);
    }
    unsigned int port_number = atoi(argv[1]);
    char * poll_filename = argv[2];
    char * log_filename = argv[3];
    
    run_server(port_number, poll_filename, log_filename);

    return 0;
}