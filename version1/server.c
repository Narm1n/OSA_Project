// server.c — Version 1
// Build: gcc -Wall -O2 -o server server.c
// Run: ./server [dictionary_folder]


#define _XOPEN_SOURCE 700
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>
#include <errno.h>
#include <limits.h>

#define WORD_LEN 50
#define LINE_BUF 256

typedef struct {
    char english[WORD_LEN];
    char french[WORD_LEN];
} WordPair;

// dynamic array helpers
typedef struct {
    WordPair *data;
    size_t size, cap;
} Dict;

static void dict_init(Dict *d){ d->data=NULL; d->size=0; d->cap=0; }
static void dict_push(Dict *d, WordPair wp){
    if(d->size==d->cap){
        d->cap = d->cap ? d->cap*2 : 128;
        d->data = (WordPair*)realloc(d->data, d->cap*sizeof(WordPair));
        if(!d->data){ perror("realloc"); exit(1); }
    }
    d->data[d->size++] = wp;
}

// Track which files already loaded to avoid duplicates 
typedef struct {
    char **names;
    size_t size, cap;
} LoadedSet;

static void set_init(LoadedSet *s){ s->names=NULL; s->size=0; s->cap=0; }
static int  set_contains(LoadedSet *s, const char *name){
    for(size_t i=0;i<s->size;i++) if(strcmp(s->names[i], name)==0) return 1;
    return 0;
}
static void set_add(LoadedSet *s, const char *name){
    if(set_contains(s,name)) return;
    if(s->size==s->cap){
        s->cap = s->cap ? s->cap*2 : 64;
        s->names = (char**)realloc(s->names, s->cap*sizeof(char*));
        if(!s->names){ perror("realloc"); exit(1); }
    }
    s->names[s->size] = strdup(name);
    if(!s->names[s->size]){ perror("strdup"); exit(1); }
    s->size++;
}

// globals
static Dict g_dict;
static LoadedSet g_loaded;

static volatile sig_atomic_t g_req_eng2fr = 0;
static volatile sig_atomic_t g_req_fr2eng = 0;

// utilities
static void rtrim(char *s){
    size_t n=strlen(s);
    while(n>0 && (s[n-1]=='\n' || s[n-1]=='\r' || s[n-1]==' ' || s[n-1]=='\t')) s[--n]='\0';
}

static int is_regular_file(const char *path){
    struct stat st;
    if(stat(path, &st)==0) return S_ISREG(st.st_mode);
    return 0;
}

// Parse one file of "english;french" pairs and append to dictionary
static void load_file_into_dict(const char *filepath){
    FILE *f = fopen(filepath, "r");
    if(!f){
        fprintf(stderr, "Cannot open %s: %s\n", filepath, strerror(errno));
        return;
    }
    char line[LINE_BUF];
    while(fgets(line, sizeof(line), f)){
        rtrim(line);
        if(line[0]=='\0') continue;
        char *semi = strchr(line, ';');
        if(!semi) continue; // skip malformed
        *semi = '\0';
        char *eng = line;
        char *fr  = semi+1;

        WordPair wp;
        // ensure bounds; strncpy ensures null-termination
        strncpy(wp.english, eng, WORD_LEN-1); wp.english[WORD_LEN-1]='\0';
        strncpy(wp.french,  fr, WORD_LEN-1);  wp.french [WORD_LEN-1]='\0';

        if(strlen(wp.english)>0 && strlen(wp.french)>0)
            dict_push(&g_dict, wp);
    }
    fclose(f);
}

// Scan directory for new files and load them
static void scan_and_load(const char *folder){
    DIR *dir = opendir(folder);
    if(!dir){
        fprintf(stderr, "opendir(%s) failed: %s\n", folder, strerror(errno));
        return;
    }
    struct dirent *ent;
    char pathbuf[PATH_MAX];
    while((ent=readdir(dir))){
        if(strcmp(ent->d_name,".")==0 || strcmp(ent->d_name,"..")==0) continue;
        snprintf(pathbuf, sizeof(pathbuf), "%s/%s", folder, ent->d_name);
        if(!is_regular_file(pathbuf)) continue;
        if(set_contains(&g_loaded, ent->d_name)) continue;

        fprintf(stdout, "[loader] New file detected: %s — loading...\n", ent->d_name);
        load_file_into_dict(pathbuf);
        set_add(&g_loaded, ent->d_name);
        fprintf(stdout, "[loader] Dictionary size: %zu\n", g_dict.size);
        fflush(stdout);
    }
    closedir(dir);
}

// signal handlers
static void sigusr1_handler(int signo){ (void)signo; g_req_eng2fr = 1; }
static void sigusr2_handler(int signo){ (void)signo; g_req_fr2eng = 1; }

// main
int main(int argc, char **argv){
    const char *folder = (argc>=2 ? argv[1] : "./dict");

    dict_init(&g_dict);
    set_init(&g_loaded);

    // Seed RNG for random picks
    srand((unsigned)time(NULL) ^ (unsigned)getpid());

    // Install signal handlers (use sigaction for reliability)
    struct sigaction sa1 = {0}, sa2 = {0};
    sa1.sa_handler = sigusr1_handler;
    sa2.sa_handler = sigusr2_handler;
    sigemptyset(&sa1.sa_mask); sigemptyset(&sa2.sa_mask);
    sa1.sa_flags = SA_RESTART; sa2.sa_flags = SA_RESTART;
    if(sigaction(SIGUSR1, &sa1, NULL)==-1){ perror("sigaction SIGUSR1"); return 1; }
    if(sigaction(SIGUSR2, &sa2, NULL)==-1){ perror("sigaction SIGUSR2"); return 1; }

    printf("=== Dictionary Server ===\n");
    printf("PID: %d\n", getpid());
    printf("Watching folder: %s\n", folder);
    printf("Signals: SIGUSR1=EN->FR, SIGUSR2=FR->EN\n");
    fflush(stdout);

    // Initial load
    scan_and_load(folder);

    // Main loop: check for requests & rescan folder periodically
    int tick = 0;
    const int RESCAN_SECS = 5;

    for(;;){
        // Handle any pending translation requests 
        if(g_req_eng2fr){
            g_req_eng2fr = 0;
            if(g_dict.size==0){
                printf("[translate] Dictionary empty.\n");
            }else{
                size_t i = (size_t) (rand() % g_dict.size);
                printf("[EN->FR]  %s  ->  %s\n", g_dict.data[i].english, g_dict.data[i].french);
            }
            fflush(stdout);
        }
        if(g_req_fr2eng){
            g_req_fr2eng = 0;
            if(g_dict.size==0){
                printf("[translate] Dictionary empty.\n");
            }else{
                size_t i = (size_t) (rand() % g_dict.size);
                printf("[FR->EN]  %s  ->  %s\n", g_dict.data[i].french, g_dict.data[i].english);
            }
            fflush(stdout);
        }

        // Periodic rescan for new files 
        if(tick % RESCAN_SECS == 0){
            scan_and_load(folder);
        }

        // Sleep 1s to keep CPU low, but still responsive 
        sleep(1);
        tick++;
    }

    return 0;
}

