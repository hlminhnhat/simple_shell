#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <stdlib.h>

#define MAX_LENGTH 255
#define MAX_ARGS MAX_LENGTH / 2 + 1
#define DELIMITER " \t\n"

#define EXIT -1
#define FAIL 0
#define SUCCESS 1
#define INVALID_CMD 2

/* Global string to store history */
char history[MAX_LENGTH];

int execCmd(char *cmd);
int execRedir(char *cmd, int mode);
int execPipe(char *cmd);
void splitCmd(char *cmd, char **parts, const char *delimiter);
void parseArgs(char *cmd, char **args, const char *delimiter);
void saveHistory(char *cmd);

int main()
{
    char cmd[MAX_LENGTH];
    char backupCmd[MAX_LENGTH];
    int shouldRun = 1;
    int isSuccess = 0;

    /* Loop until user type "exit" */
    while (shouldRun)
    {
        /* Get input from user and store in cmd */

        printf("osh>");
        fflush(stdout);
        fgets(cmd, MAX_LENGTH, stdin);

        /* Backup cmd for saving history */
        strcpy(backupCmd, cmd);

        isSuccess = execCmd(cmd);

        /* Save cmd to history if it is a valid command */
        if (isSuccess != INVALID_CMD)
            saveHistory(backupCmd);

        shouldRun = (isSuccess != -1);
    }
    return 0;
}

/* Parses a command into an array of arguments 
 * cmd - input command
 * args - array of pointer to arguments
 * delimiter - array of chars used for spliting command
 */
void parseArgs(char *cmd, char **args, const char *delimiter)
{
    args[0] = strtok(cmd, delimiter);
    int count = 0;
    while (args[count] != NULL && count < MAX_ARGS - 1)
    {
        args[++count] = strtok(NULL, delimiter);
    }
}

/* Split command into two parts - used for REDIRECTION and PIPE
 * cmd - input command
 * parts - store address of two parts
 * delimiter - array of chars to split command
 */
void splitCmd(char *cmd, char **parts, const char *delimiter)
{
    parts[0] = strtok(cmd, delimiter);
    parts[1] = strtok(NULL, delimiter);
}

/* Save cmd to history */
void saveHistory(char *cmd)
{
    strcpy(history, cmd);
}

/* Execute standard command
 * Transfer 'redirection-command' or 'pipe-command'
 */
int execCmd(char *cmd)
{
    /* Pipe-command */
    if (strchr(cmd, '|') != NULL)
    {
        return execPipe(cmd);
    }
    /* Redirection-command with INPUT from file */
    else if (strchr(cmd, '<') != NULL)
    {
        return execRedir(cmd, 0);
    }
    /* Redirection-command with OUTPUT to file */
    else if (strchr(cmd, '>') != NULL)
    {
        return execRedir(cmd, 1);
    }

    char *args[MAX_ARGS];
    /* Parse command into array of argument */
    parseArgs(cmd, args, DELIMITER);

    /*Check invalid command */
    if (args[0] == NULL)
    {
        return INVALID_CMD;
    }

    /* Check built-in command (cd, exit and history feature) */
    if (strcmp(args[0], "cd") == 0)
    {
        if (args[1] == NULL)
            return INVALID_CMD;
        chdir(args[1]);
        return SUCCESS;
    }

    if (strcmp(args[0], "exit") == 0)
    {
        return EXIT;
    }

    if (strcmp(args[0], "!!") == 0)
    {
        if (strlen(history) == 0)
            printf("No commands in history.\n");
        else
        {
            /* Use tempCmd to execCmd
             * because parseArgs may change history
             */
            char tempCmd[MAX_LENGTH];
            strcpy(tempCmd, history);

            execCmd(tempCmd);
        }

        /* Don't need to save history */
        return INVALID_CMD;
    }

    /* Determine whether or not the parent should wait for the child to exit */
    int shouldWait = 1;
    for (int i = 0; i < MAX_ARGS; i++)
    {
        if (args[i] == NULL)
            break;
        if (strcmp(args[i], "&") == 0)
        {
            args[i] = NULL;
            shouldWait = 0;
            break;
        }
    }

    pid_t pid = fork();
    if (pid == -1)
    {
        printf("Error: Cannot create new process.\n");
        return FAIL;
    }
    else if (pid == 0)
    {
        if (execvp(args[0], args) < 0)
        {
            printf("Error: Cannot execute command.\n");
            exit(1);
        }
        exit(0);
    }
    else
    {
        /* Wait for child process to exit */
        if (shouldWait)
        {
            waitpid(pid, NULL, 0);
        }

        return SUCCESS;
    }
}

/* Execute redirection-command 
 * mode = 0 - INPUT from file
 * mode = 1 - OUTPUT to file
 */
int execRedir(char *cmd, int mode)
{
    char *parts[2];
    splitCmd(cmd, parts, "<>");

    char *args[MAX_ARGS];
    char *fileParse[MAX_ARGS];

    /* Parse command */
    parseArgs(parts[0], args, DELIMITER);

    /* Parse file directory */
    parseArgs(parts[1], fileParse, DELIMITER);

    /* Command not containing file directory */
    if (fileParse[0] == NULL)
        return INVALID_CMD;

    pid_t pid = fork();
    if (pid == -1)
    {
        printf("Error: Cannot create new process.\n");
        return FAIL;
    }
    else if (pid == 0)
    {
        if (mode == 0)
        {
            int fd = open(fileParse[0], O_RDONLY);
            if (fd < 0)
            {
                printf("Error: Cannot open file.\n");
                exit(0);
            }

            /* Redirect file input */
            dup2(fd, STDIN_FILENO);
            close(fd);
        }
        else
        {
            int fd = open(fileParse[0], O_WRONLY | O_CREAT | O_APPEND, S_IRWXU | S_IRWXG | S_IRWXO);
            if (fd < 0)
            {
                printf("Error: Cannot open file.\n");
                exit(0);
            }

            /* Redirect file output */
            dup2(fd, STDOUT_FILENO);
            close(fd);
        }

        if (execvp(args[0], args) < 0)
        {
            printf("Error: Cannot execute command.\n");
            exit(1);
        }
        exit(0);
    }
    else
    {
        waitpid(pid, NULL, 0);
        return SUCCESS;
    }
}

/* Execute pipe-command 
 * Create two child processes that communicating via a Pipe
 */
int execPipe(char *cmd)
{
    char *parts[2];
    splitCmd(cmd, parts, "|");

    char *argsL[MAX_ARGS];
    char *argsR[MAX_ARGS];

    /* Parse left command */
    parseArgs(parts[0], argsL, DELIMITER);

    /* Parse left command */
    parseArgs(parts[1], argsR, DELIMITER);

    if (argsR[0] == NULL || argsL[0] == NULL)
        return INVALID_CMD;

    pid_t pid1, pid2;
    int pfd[2]; /* file description of pipe */
    if (pipe(pfd) < 0)
    {
        printf("Error: Cannot create pipe.\n");
        return FAIL;
    }

    pid1 = fork();
    if (pid1 == -1)
    {
        printf("Error: Cannot create new process.\n");
        return FAIL;
    }
    else if (pid1 == 0)
    {
        /* Output from process 1 taken into pipe */
        close(pfd[0]);
        dup2(pfd[1], STDOUT_FILENO);

        if (execvp(argsL[0], argsL) < 0)
        {
            printf("Error: Cannot execute command.\n");
            exit(1);
        }
        exit(0);
    }
    else
    {
        pid2 = fork();
        if (pid2 == -1)
        {
            printf("Error: Cannot create new process.\n");
            exit(1);
        }
        else if (pid2 == 0)
        {
            /* Input to process 2 taken from pipe */
            close(pfd[1]);
            dup2(pfd[0], STDIN_FILENO);

            if (execvp(argsR[0], argsR) < 0)
            {
                printf("Error: Cannot execute command.\n");
                exit(1);
            }
            exit(0);
        }
        else
        {
            /* Close the pipe */
            close(pfd[0]);
            close(pfd[1]);

            /* Wait for two processes to exit */
            waitpid(pid1, NULL, 0);
            waitpid(pid2, NULL, 0);
        }
    }
    return SUCCESS;
}