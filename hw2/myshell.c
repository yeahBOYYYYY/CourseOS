#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <signal.h>

#define MAX_CMDS 10
#define PERMISSIONS 0600

/// Signal handler to reap zombie background processes
static void sigchld_handler(int sig);

/**
 * Perform any setup needed before shell starts
 * @return 0 on success, non-zero on failure
 */
int prepare(void) {
    struct sigaction sa;
    sa.sa_handler = sigchld_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART | SA_NOCLDSTOP;
    if (sigaction(SIGCHLD, &sa, NULL) == -1) {
        perror("sigaction");
        return 1;
    }

    // Ignore SIGINT in the shell process
    if (signal(SIGINT, SIG_IGN) == SIG_ERR) {
        perror("signal");
        return 1;
    }

    return 0;
}

/**
 * Perform any cleanup before shell exits
 * @return 0 on success, non-zero on failure
 */
int finalize(void) {
    return 0;
}

/**
 * Check if arglist contains a special symbol
 * @param arglist argument list
 * @param symbol the symbol to search for
 * @return index of symbol or -1 if not found
 */
int find_symbol(char** arglist, const char* symbol) {
    for (int i = 0; arglist[i] != NULL; ++i) {
        if (strcmp(arglist[i], symbol) == 0) {
            return i;
        }
    }
    return -1;
}

/**
 * Remove the last argument if it is "&"
 * @param arglist the argument list
 * @param count the number of arguments (excluding NULL)
 * @return 1 if & was removed, 0 otherwise
 */
int remove_background_ampersand(char** arglist, int* count) {
    if (*count > 0 && strcmp(arglist[*count - 1], "&") == 0) {
        arglist[*count - 1] = NULL;
        (*count)--;
        return 1;
    }
    return 0;
}

/**
 * Execute a single command (no pipes or redirection)
 * @param arglist the argument list
 * @param background 1 if should be run in background
 * @return 1 on success, 0 on error
 */
int execute_command(char** arglist, int background) {
    pid_t pid = fork();
    if (pid == -1) {
        perror("fork");
        return 0;
    } else if (pid == 0) {
        // child process
        if (!background) signal(SIGINT, SIG_DFL);
        else signal(SIGINT, SIG_IGN);
        execvp(arglist[0], arglist);
        perror("execvp");
        exit(1);
    }

    if (!background) {
        int status;
        if (waitpid(pid, &status, 0) == -1 && errno != EINTR && errno != ECHILD) {
            perror("waitpid");
        }
    }

    return 1;
}

/**
 * Execute a command with output redirection
 * @param arglist the argument list
 * @param symbol_index index of ">"
 * @return 1 on success, 0 on error
 */
int execute_output_redirect(char** arglist, int symbol_index) {
    arglist[symbol_index] = NULL;
    const char* filename = arglist[symbol_index + 1];

    pid_t pid = fork();
    if (pid == -1) {
        perror("fork");
        return 0;
    } else if (pid == 0) {
        signal(SIGINT, SIG_DFL);
        int fd = open(filename, O_CREAT | O_WRONLY | O_TRUNC, PERMISSIONS);
        if (fd < 0) {
            perror("open");
            exit(1);
        }
        dup2(fd, STDOUT_FILENO);
        close(fd);
        execvp(arglist[0], arglist);
        perror("execvp");
        exit(1);
    }

    int status;
    if (waitpid(pid, &status, 0) == -1 && errno != EINTR && errno != ECHILD) {
        perror("waitpid");
    }
    return 1;
}

/**
 * Execute a command with input redirection
 * @param arglist the argument list
 * @param symbol_index index of "<"
 * @return 1 on success, 0 on error
 */
int execute_input_redirect(char** arglist, int symbol_index) {
    arglist[symbol_index] = NULL;
    const char* filename = arglist[symbol_index + 1];

    pid_t pid = fork();
    if (pid == -1) {
        perror("fork");
        return 0;
    } else if (pid == 0) {
        signal(SIGINT, SIG_DFL);
        int fd = open(filename, O_RDONLY);
        if (fd < 0) {
            perror("open");
            exit(1);
        }
        dup2(fd, STDIN_FILENO);
        close(fd);
        execvp(arglist[0], arglist);
        perror("execvp");
        exit(1);
    }

    int status;
    if (waitpid(pid, &status, 0) == -1 && errno != EINTR && errno != ECHILD) {
        perror("waitpid");
    }
    return 1;
}

/**
 * Execute a pipeline of commands
 * @param arglist the full argument list
 * @param count number of arguments
 * @return 1 on success, 0 on error
 */
int execute_pipeline(char** arglist, int count) {
    char* commands[MAX_CMDS][count + 1];
    int cmd_count = 0;
    int idx = 0;

    // Split commands by '|'
    for (int i = 0; i <= count; i++) {
        if (arglist[i] == NULL || strcmp(arglist[i], "|") == 0) {
            commands[cmd_count][idx] = NULL;
            cmd_count++;
            idx = 0;
            continue;
        }
        commands[cmd_count][idx++] = arglist[i];
    }

    int pipefds[2 * (cmd_count - 1)];
    for (int i = 0; i < cmd_count - 1; i++) {
        if (pipe(pipefds + i * 2) < 0) {
            perror("pipe");
            return 0;
        }
    }

    for (int i = 0; i < cmd_count; i++) {
        pid_t pid = fork();
        if (pid == -1) {
            perror("fork");
            return 0;
        } else if (pid == 0) {
            signal(SIGINT, SIG_DFL);

            if (i != 0) dup2(pipefds[(i - 1) * 2], STDIN_FILENO);
            if (i != cmd_count - 1) dup2(pipefds[i * 2 + 1], STDOUT_FILENO);

            for (int j = 0; j < 2 * (cmd_count - 1); j++) close(pipefds[j]);

            execvp(commands[i][0], commands[i]);
            perror("execvp");
            exit(1);
        }
    }

    for (int j = 0; j < 2 * (cmd_count - 1); j++) close(pipefds[j]);
    for (int i = 0; i < cmd_count; i++) wait(NULL);

    return 1;
}

/**
 * Main command handler, dispatches to appropriate execution method
 * @param count number of arguments
 * @param arglist array of argument strings
 * @return 1 to continue shell, 0 to exit
 */
int process_arglist(int count, char** arglist) {
    int background = remove_background_ampersand(arglist, &count);

    int pipe_index = find_symbol(arglist, "|");
    int in_index = find_symbol(arglist, "<");
    int out_index = find_symbol(arglist, ">");

    if (pipe_index != -1) return execute_pipeline(arglist, count);
    else if (in_index != -1) return execute_input_redirect(arglist, in_index);
    else if (out_index != -1) return execute_output_redirect(arglist, out_index);
    else return execute_command(arglist, background);
}

/**
 * Signal handler for SIGCHLD to reap background processes
 * @param sig signal number
 */
static void sigchld_handler(int sig) {
    int saved_errno = errno;
    while (waitpid(-1, NULL, WNOHANG) > 0);
    errno = saved_errno;
}
