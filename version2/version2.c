// Build: gcc -Wall -O2 -pthread -o version2 version2.c
// Run: ./version2 [dictionary_folder]

// What it does:
// Writer thread scans folder for new files, reads pairs, and sends messages
// (mtype=1 for EN->FR, mtype=2 for FR->EN) via System V message queue.
// Reader thread receives messages and stores pairs into a shared memory array.
// Shared memory is used just to meet the requirement (both threads are in one process).


#define _XOPEN_SOURCE 700
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/shm.h>
#include <unistd.h>
#include <pthread.h>
#include <signal.h>
#include <time.h>
#include <limits.h>

#define WORD_LEN   50
#define LINE_BUF   256
#define RESCAN_SEC 5
#define SHM_CAP    4096   // max WordPairs storable in shared memory

enum { EN_FR = 1, FR_EN = 2 };

typedef struct {
    char english[WORD_LEN];
    char french [WORD_LEN];
} WordPair;

typedef struct {
    long mtype;               // EN_FR or FR_EN
    char english[WORD_LEN];
    char french [WORD_LEN];
} Msg;

typedef struct {            // lives in shared memory
    size_t count;
    size_t cap;
    WordPair data[SHM_CAP];
} ShmDict;

// globals 
static int g_msq = -1;         // message queue id
static int g_shmid = -1;       // shared memory id
static ShmDict *g_shm = NULL;  // attached shared memory
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static volatile sig_atomic_t g_running = 1;
static const char *g_folder = "./dict";

// track loaded files to avoid duplicates 
typedef struct { char **names; size_t n, cap; } Loaded;
static Loaded g_loaded = {0};

static void loaded_add(const char *name){
    for(size_t i=0;i<g_loaded.n;i++) if(strcmp(g_loaded.names[i],name)==0) return;
    if(g_loaded.n==g_loaded.cap){
        g_loaded.cap = g_loaded.cap? g_loaded.cap*2 : 64;
        g_loaded.names = realloc(g_loaded.names, g_loaded.cap*sizeof(char*));
        if(!g_loaded.names){ perror("realloc"); exit(1); }
    }
    g_loaded.names[g_loaded.n++] = strdup(name);
}

static int loaded_has(const char *name){
    for(size_t i=0;i<g_loaded.n;i++) if(strcmp(g_loaded.names[i],name)==0) return 1;
    return 0;
}

// utilities 
static void rtrim(char *s){ size_t n=strlen(s); while(n && (s[n-1]=='\n'||s[n-1]=='\r'||s[n-1]==' '||s[n-1]=='\t')) s[--n]='\0'; }
static int is_regular(const char *p){ struct stat st; return stat(p,&st)==0 && S_ISREG(st.st_mode); }

// writer: scan folder -> send messages
static void send_pair(const char *en, const char *fr) {
    Msg m;

    // EN->FR
    m.mtype = EN_FR;
    strncpy(m.english, en, WORD_LEN - 1);
    strncpy(m.french, fr, WORD_LEN - 1);
    m.english[WORD_LEN - 1] = m.french[WORD_LEN - 1] = '\0';
    if (msgsnd(g_msq, &m, sizeof(Msg) - sizeof(long), 0) == -1)
        perror("msgsnd EN_FR");

    // FR->EN (swap positions)
    m.mtype = FR_EN;
    strncpy(m.english, fr, WORD_LEN - 1);
    strncpy(m.french, en, WORD_LEN - 1);
    m.english[WORD_LEN - 1] = m.french[WORD_LEN - 1] = '\0';
    if (msgsnd(g_msq, &m, sizeof(Msg) - sizeof(long), 0) == -1)
        perror("msgsnd FR_EN");
}


static void load_file(const char *path){
    FILE *f = fopen(path, "r");
    if(!f){ fprintf(stderr,"[writer] open %s: %s\n", path, strerror(errno)); return; }
    char line[LINE_BUF];
    int sent=0;
    while(fgets(line,sizeof(line),f)){
        rtrim(line);
        if(!*line) continue;
        char *semi = strchr(line,';');
        if(!semi) continue;
        *semi = '\0';
        const char *en = line;
        const char *fr = semi+1;
        if(*en && *fr){ send_pair(en, fr); sent++; }
    }
    fclose(f);
    printf("[writer] %s -> %d messages\n", path, sent*2);
    fflush(stdout);
}

static void *writer_thread(void *arg){
    (void)arg;
    while(g_running){
        DIR *d = opendir(g_folder);
        if(!d){ fprintf(stderr,"[writer] opendir(%s): %s\n", g_folder, strerror(errno)); sleep(RESCAN_SEC); continue; }
        struct dirent *e; char path[PATH_MAX];
        while((e=readdir(d))){
            if(strcmp(e->d_name,".")==0 || strcmp(e->d_name,"..")==0) continue;
            if(loaded_has(e->d_name)) continue;
            snprintf(path,sizeof(path), "%s/%s", g_folder, e->d_name);
            if(!is_regular(path)) continue;
            printf("[writer] new file: %s\n", e->d_name);
            load_file(path);
            loaded_add(e->d_name);
        }
        closedir(d);
        sleep(RESCAN_SEC);
    }
    return NULL;
}

// reader: receive messages -> write shared memory 
static void store_in_shm(const Msg *m){
    pthread_mutex_lock(&g_lock);
    if(g_shm->count < g_shm->cap){
        WordPair *wp = &g_shm->data[g_shm->count++];
        strncpy(wp->english, m->english, WORD_LEN-1); wp->english[WORD_LEN-1]='\0';
        strncpy(wp->french,  m->french,  WORD_LEN-1); wp->french [WORD_LEN-1]='\0';
        size_t idx = g_shm->count-1;
        printf("[reader] stored (%s) #%zu: %s <-> %s\n",
               (m->mtype==EN_FR? "EN->FR":"FR->EN"), idx, wp->english, wp->french);
    }else{
        fprintf(stderr,"[reader] SHM full (%zu)\n", g_shm->cap);
    }
    pthread_mutex_unlock(&g_lock);
}

static void *reader_thread(void *arg){
    (void)arg;
    Msg m;
    while(g_running){
        ssize_t r = msgrcv(g_msq, &m, sizeof(Msg)-sizeof(long), 0, 0); // any type
        if(r==-1){
            if(errno==EINTR) continue;
            perror("[reader] msgrcv");
            break;
        }
        store_in_shm(&m);
    }
    return NULL;
}

// cleanup & signals 
static void on_sigint(int sig){
    (void)sig;
    g_running = 0;
    // Unblock msgrcv by removing queue:
    if(g_msq!=-1) msgctl(g_msq, IPC_RMID, NULL);
}

static void cleanup(void){
    if(g_shm){ shmdt(g_shm); g_shm=NULL; }
    if(g_shmid!=-1){ shmctl(g_shmid, IPC_RMID, NULL); g_shmid=-1; }
}

// main 
int main(int argc, char **argv){
    if(argc>=2) g_folder = argv[1];

    // Create keys (ftok needs an existing path)
    key_t key_q = ftok(".", 'Q');
    key_t key_s = ftok(".", 'D');
    if(key_q==-1 || key_s==-1){ perror("ftok"); return 1; }

    // Message queue
    g_msq = msgget(key_q, IPC_CREAT | 0666);
    if(g_msq==-1){ perror("msgget"); return 1; }

    // Shared memory
    size_t shm_size = sizeof(ShmDict);
    g_shmid = shmget(key_s, shm_size, IPC_CREAT | 0666);
    if(g_shmid==-1){ perror("shmget"); msgctl(g_msq, IPC_RMID, NULL); return 1; }
    g_shm = (ShmDict*)shmat(g_shmid, NULL, 0);
    if(g_shm==(void*)-1){ perror("shmat"); msgctl(g_msq, IPC_RMID, NULL); return 1; }
    g_shm->count = 0; g_shm->cap = SHM_CAP;

    struct sigaction sa = {0}; sa.sa_handler=on_sigint; sigemptyset(&sa.sa_mask);
    sigaction(SIGINT,&sa,NULL); sigaction(SIGTERM,&sa,NULL);

    printf("=== V2 Translation Server ===\n");
    printf("Folder: %s\n", g_folder);
    printf("MsgQueue id: %d | Shm id: %d | Capacity: %d pairs\n", g_msq, g_shmid, SHM_CAP);
    printf("Press Ctrl+C to stop. Add files to the folder to stream pairs...\n");
    fflush(stdout);

    pthread_t th_writer, th_reader;
    if(pthread_create(&th_writer, NULL, writer_thread, NULL)!=0){ perror("pthread_create writer"); return 1; }
    if(pthread_create(&th_reader, NULL, reader_thread, NULL)!=0){ perror("pthread_create reader"); return 1; }

    pthread_join(th_writer, NULL);
    pthread_kill(th_reader, SIGINT); // in case blocked after queue removal
    pthread_join(th_reader, NULL);

    cleanup();
    return 0;
}
