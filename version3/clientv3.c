// Build: gcc -Wall -O2 -o clientv3 clientv3
// Run:   ./clientv3

// Usage:
//   - Type:   en hello    -> translate EN→FR
//             fr bonjour  -> translate FR→EN
//   - Type:   quit        -> exit

#define _XOPEN_SOURCE 700
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <unistd.h>
#include <errno.h>

#define WORD_LEN 50

enum { REQ_EN_FR = 1, REQ_FR_EN = 2 };

typedef struct {
    long mtype;         // 1 or 2
    pid_t reply_to;     // my PID
    char word[WORD_LEN];
} RequestMsg;

typedef struct {
    long mtype;         // = client PID
    int  found;
    char translation[WORD_LEN];
} ResponseMsg;

static void trimnl(char *s){ size_t n=strlen(s); while(n && (s[n-1]=='\n'||s[n-1]=='\r')) s[--n]='\0'; }

int main(void){
    key_t key_q = ftok(".", 'R');
    if(key_q==-1){ perror("ftok"); return 1; }
    int msq = msgget(key_q, 0666);
    if(msq==-1){ perror("msgget (is server running?)"); return 1; }

    printf("=== V3 Client ===\n");
    printf("Type: en <word> | fr <mot> | quit\n");

    char line[256];
    pid_t me = getpid();

    while(1){
        printf("> ");
        fflush(stdout);
        if(!fgets(line,sizeof(line),stdin)) break;
        trimnl(line);
        if(strcmp(line,"quit")==0) break;
        if(!*line) continue;

        char dir[8]={0}, word[WORD_LEN]={0};
        if(sscanf(line, "%7s %49s", dir, word) != 2){
            printf("Please use: en <word>  or  fr <mot>\n");
            continue;
        }

        RequestMsg req; memset(&req,0,sizeof(req));
        if(dir[0]=='e' || dir[0]=='E') req.mtype = REQ_EN_FR;
        else if(dir[0]=='f' || dir[0]=='F') req.mtype = REQ_FR_EN;
        else { printf("First token must be 'en' or 'fr'.\n"); continue; }

        req.reply_to = me;
        strncpy(req.word, word, WORD_LEN-1);

        if(msgsnd(msq, &req, sizeof(RequestMsg)-sizeof(long), 0)==-1){
            perror("msgsnd"); return 1;
        }

        ResponseMsg res;
        if(msgrcv(msq, &res, sizeof(ResponseMsg)-sizeof(long), me, 0)==-1){
            perror("msgrcv"); return 1;
        }

        if(res.found) printf("= %s\n", res.translation);
        else          printf("(not found)\n");
    }

    printf("Bye.\n");
    return 0;
}
