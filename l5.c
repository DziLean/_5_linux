#include <stdio.h>
#include <libgen.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <pthread.h>
#include <dirent.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <syscall.h>

#define IN   1  /* inside a word */
#define OUT  0  /* outside a word */


typedef enum
{
    stNull,
    stBusy,
    stFree
} statusThread_t;

typedef struct
{
    statusThread_t *thread_status;
    char *filename;
} paramsThread_t;

typedef struct
{
    int bytes_count;
    ssize_t words_count;
} resultFind_t;

char *PROG_NAME;
pthread_t *threadsMass;
statusThread_t *statusThreadsMass;

int counter(const char *filename, resultFind_t *resultFind);
int waitThread(int countThreads);


void errorPrint(const char *PROG_NAME, const char *error_msg, const char *file_name) {
    fprintf(stderr, "%s: %s %s\n", PROG_NAME, error_msg, file_name ? file_name : "");
}
void resultPrint(const char *filename, const resultFind_t resultFind) {
    printf("%ld: %s %d %d\n", syscall(SYS_gettid), filename,  resultFind.bytes_count, resultFind.words_count);
}

void *user_function(void *args)
{
    paramsThread_t *threadParams = (paramsThread_t *) args;
    resultFind_t resultFind;

    if (counter(threadParams->filename, &resultFind) != -1)
    {
        if (resultFind.bytes_count != 0)
        {
            resultPrint(threadParams->filename, resultFind);
        }
    };

    *(threadParams->thread_status) = stFree;

    while (*(threadParams->thread_status) != stNull);

    free(threadParams->filename);
    free(threadParams);

    return NULL;
}

int counter(const char *filename, resultFind_t *resultFind)
{
    FILE *f ;
    int c, state;
    state = OUT;

    int words_count = 0;
    int bytes_count = 0;
    if ((f = fopen(filename, "rb")) != NULL)
    {
   	 while ((c = fgetc(f)) != EOF)
  	  {
  	      if (c == ' ' || c == '\n' || c == '\t')
  	          state = OUT;
  	      else if (state == OUT)
  	      {
  	          state = IN;
  	          ++words_count;
  	      }
              ++bytes_count;
  	  }
   	  if (fclose(f) == -1) {
        	errorPrint(PROG_NAME, strerror(errno), filename);
        	return -1;
    	}
    }
    else
    {
       errorPrint(PROG_NAME, strerror(errno), filename);
    }


    resultFind->bytes_count = bytes_count;
    resultFind->words_count = words_count;

    return 0;
}

void filePath(char *dest, const char *const path, const char *const name)
{
    strcpy(dest, path);
    strcat(dest, "/");
    strcat(dest, name);
}

void walkDir(const char *path, const int countThreads)
{
    DIR *dir;
    struct dirent *dir_entry;
    if (!(dir = opendir(path)))
    {
        errorPrint(PROG_NAME, strerror(errno), path);
        return;
    }

    paramsThread_t *threadParams;
    pthread_t thread;

    errno = 0;
    while ((dir_entry = readdir(dir)) != NULL)
    {
        char *entry_name = dir_entry->d_name;

        if (!strcmp(".", entry_name) || !strcmp("..", entry_name))
            continue;

        char *fullPath = malloc(strlen(entry_name) + strlen(path) + 2);
        filePath(fullPath, path, entry_name);
        struct stat entry_info;
        if (lstat(fullPath, &entry_info) == -1)
        {
            errorPrint(PROG_NAME, strerror(errno), fullPath);
            errno = 0;
            continue;
        }

        if (S_ISDIR(entry_info.st_mode))
        {
            walkDir(fullPath, countThreads);
        }
        else
        {
            if (S_ISREG(entry_info.st_mode))
            {
                int thread_id = waitThread(countThreads);
                statusThreadsMass[thread_id] = stNull;
                if (pthread_join(threadsMass[thread_id], NULL) == -1)
                {
                    errorPrint(PROG_NAME, strerror(errno), NULL);
                    return;
                };

                threadParams = malloc(sizeof(paramsThread_t));
                threadParams->thread_status = &(statusThreadsMass[thread_id]);
                threadParams->filename = fullPath;

                statusThreadsMass[thread_id] = stBusy;
                if (pthread_create(&thread, NULL, &user_function, threadParams) == -1)
                {
                    errorPrint(PROG_NAME, strerror(errno), NULL);
                    return;
                };

                threadsMass[thread_id] = thread;
            }
        }
    }
    if (errno)
    {
        errorPrint(PROG_NAME, strerror(errno), path);
    }

    if (closedir(dir) == -1)
    {
        errorPrint(PROG_NAME, strerror(errno), path);
    }

}

int waitThread(int countThreads)
{
    int i = 0;
    while (statusThreadsMass[i] == stBusy)
    {
        i++;
        if (i == countThreads)
        {
            i = 0;
        }
    }
    return i;
}

char finish(int countThreads)
{
    int i;
    for (i = 0; (i < countThreads); i++)
    {
        if (statusThreadsMass[i] != stFree)
        {
            return 0;
        }
    }
    return 1;
}

int main(int argc, char *argv[])
{
    int countThreads, i;
    PROG_NAME = basename(argv[0]);

    if (argc != 3) {
        errorPrint(PROG_NAME, "Wrong number of parameters", NULL);
        return 1;
    }

    if ((countThreads = atoi(argv[2])) < 1) {
        errorPrint(PROG_NAME, "Thread number must be bigger than 1.", NULL);
        return 1;
    }

    statusThreadsMass = calloc(sizeof(statusThread_t), (size_t) countThreads);
    for (i = 0; i < countThreads; i++)
    {
        statusThreadsMass[i] = stNull;
    }
    threadsMass = calloc(sizeof(pthread_t), (size_t) countThreads);

    walkDir(argv[1], countThreads);
    while (!finish(countThreads));
}


