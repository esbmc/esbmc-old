//FormAI DATASET v1.0 Category: Basic Unix-like Shell ; Style: multi-threaded
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/wait.h>

#define MAX_INPUT_LENGTH 1024
#define MAX_ARGS 64

void executeCommand(char *args[])
{
    pid_t pid = fork();
    if (pid == 0) 
    {
        // Child process
        char *envp[] = { NULL };
        execvp(args[0], args);
        printf("%s: command not found\n", args[0]);
        exit(EXIT_FAILURE);
    } 
    else if (pid < 0) 
    {
        // Error 
        printf("Error occurred while forking\n");
    } 
    else 
    {
        // Parent process
        wait(NULL); 
    }
}

void* readAndExecuteCommands(void *arg)
{
    char input[MAX_INPUT_LENGTH];
    while (1) 
    {
        printf("Enter command: ");
        fgets(input, MAX_INPUT_LENGTH, stdin);
        input[strcspn(input, "\n")] = '\0'; // Remove trailing \n

        char *args[MAX_ARGS];
        char *token = strtok(input, " ");
        int i = 0;
        while (token != NULL && i < MAX_ARGS) 
        {
            args[i] = token;
            token = strtok(NULL, " ");
            i++;
        }
        args[i] = NULL;

        if (strcmp(args[0], "exit") == 0) 
        {
            exit(EXIT_SUCCESS);
        }

        executeCommand(args);
    }
}

int main(void) 
{
    pthread_t thread_id;
    pthread_create(&thread_id, NULL, &readAndExecuteCommands, NULL);
    pthread_join(thread_id, NULL);
    return 0;
}

