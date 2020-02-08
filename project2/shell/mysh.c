#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <linux/limits.h>
#include <signal.h>
#define MAX_SIZE 129

int numOfArgs;        /* Number of arguments */
int numOfCommand = 1; /* Number of commands entered */
int in = 0;           /* Check if input redirection is needed */
int out = 0;          /* Check if output redirection is needed */
int pl = 0;           /* Check if pipeline is needed */
int b = 0;            /* Background process indicator */
char* input = NULL;   /* The name of input file */
char* output = NULL;  /* The name of output file */
int processes[20];    /* Background processes */

char error_message[30] = "An error has occurred\n";

/**
 * Read the command from the shell. Exit the shell if
 * end-of-file condition occurs.
 */
char* mysh_read_line(void) {
	char* line = NULL;
	size_t length = 0;
	// End-of-file condition.
	if (getline(&line, &length, stdin) == -1) {
		free(line);
		exit(0);
	}
	return line;
}

/**
 * Split the input from the shell into arguments.
 */
char** mysh_parse_line(char* line) {
	int num = 0;
	char** arguments = NULL;
	// strtok() returns a pointer to a null-terminated string
	// containing the next token.
	char* token = strtok(line, " \n\t");
	while (token != NULL) {
		num++;
		arguments = realloc(arguments, sizeof(char*) * num);
		arguments[num - 1] = token;
		token = strtok(NULL, " \n\t");
	}
	numOfArgs = num;
	// The list of arguments is terminated by a null pointer.
	arguments = realloc(arguments, sizeof(char*) * (num + 1));
	arguments[num] = NULL;
	return arguments;
}

/**
 * When the built-in exit command is called, use kill() to send
 * signals to terminate all still-running processes.
 */
void mysh_kill(void) {
	for (int i = 0; i < 20; i++) {
		if (processes[i] != 0) {
			kill(processes[i], SIGQUIT);
		}
	}
}

/**
 * Check if redirection or pipeline is needed. Return the index
 * of either the rediction sign or the pipeline sign. Return zero
 * if no rediction or pipeline is needed. Return -1 if the command
 * does not have the right format. Return -2 if there is no command
 * before the redirection sign.
 */
int mysh_redirection(char** args) {
	for (int i = 0; i < numOfArgs; i++) {
		if (strcmp(args[i], ">") == 0) {
			// There is no command before the ">" operator.
			if (i == 0) {
				return -2;
			}
			// There is no argument after the ">" operator.
			if (i == numOfArgs - 1) {
				return -1;
			}
			else if (numOfArgs - i - 1 == 1) {
				// There is only one argument after the ">" operator.
				in = 0;
				output = args[i + 1];
				out = 1;
				return i;
			}
			else if (numOfArgs - i - 1 == 2) {
				if (strcmp(args[numOfArgs - 1], "&") == 0) {
					b = 1;
					in = 0;
					output = args[i + 1];
					out = 1;
					return i;
				}
				else {
					return -1;
				}
			}
			else if (numOfArgs - i - 1 == 3) {
				if (strcmp(args[i + 2], "<") == 0) {
					// Both input redirection and output redirection
					// is needed.
					in = 1;
					input = args[i + 3];
					out = 1;
					output = args[i + 1];
					return i;
				}
				else {
					return -1;
				}
			}
			else if (numOfArgs - i - 1 == 4) {
				if (strcmp(args[i + 2], "<") == 0) {
					if (strcmp(args[numOfArgs - 1], "&") == 0) {
						b = 1;
						in = 1;
						input = args[i + 3];
						out = 1;
						output = args[i + 1];
						return i;
					}
					else {
						return -1;
					}
				}
				else {
					return -1;
				}
			}
			else {
				return -1;
			}
		}
		else if (strcmp(args[i], "<") == 0) {
			if (i == 0) {
				return -2;
			}
			if (i == numOfArgs - 1) {
				return -1;
			}
			else if (numOfArgs - i - 1 == 1) {
				// There is only one argument after the "<" operator.
				in = 1;
				input = args[i + 1];
				out = 0;
				return i;
			}
			else if (numOfArgs - i - 1 == 2) {
				if (strcmp(args[numOfArgs - 1], "&") == 0) {
					b = 1;
					in = 1;
					input = args[i + 1];
					out = 0;
					return i;
				}
				else {
					return -1;
				}
			}
			else if (numOfArgs - i - 1 == 3) {
				if (strcmp(args[i + 2], ">") == 0) {
					// Both input redirection and output redirection
					// is needed.
					in = 1;
					input = args[i + 1];
					out = 1;
					output = args[i + 3];
					return i;
				}
				else {
					return -1;
				}
			}
			else if (numOfArgs - i - 1 == 4) {
				if (strcmp(args[i + 2], ">") == 0) {
					if (strcmp(args[numOfArgs - 1], "&") == 0) {
						b = 1;
						in = 1;
						input = args[i + 1];
						out = 1;
						output = args[i + 3];
						return i;
					}
					else {
						return -1;
					}
				}
				else {
					return -1;
				}
			}
			else {
				return -1;
			}
		}
		else if (strcmp(args[i], "|") == 0) {
			if (i == 0 || i == numOfArgs - 1) {
				return -1;
			}
			else {
				// Pipeline is needed.
				pl = 1;
				return i;
			}
		}
	}
	if (strcmp(args[numOfArgs - 1], "&") == 0) {
		b = 1;
		return (numOfArgs - 1);
	}
	return 0;
}

/**
 * cd builtin function.
 */
int mysh_cd(char** args) {
	if (numOfArgs > 2) {
		write(STDERR_FILENO, error_message, strlen(error_message));
		return EXIT_FAILURE;
	}
	if (numOfArgs == 1) {
		char* path = getenv("HOME");
		if (chdir(path) != 0) {
			write(STDERR_FILENO, error_message, strlen(error_message));
			return EXIT_FAILURE;
		}
	}
	else {
		if (chdir(args[1]) != 0) {
			write(STDERR_FILENO, error_message, strlen(error_message));
			return EXIT_FAILURE;
		}
	}
	return EXIT_SUCCESS;
}
/**
 * pwd builtin command.
 */
int mysh_pwd(void) {
	if (numOfArgs > 1) {
		write(STDERR_FILENO, error_message, strlen(error_message));
		return EXIT_FAILURE;
	}
	char buf[PATH_MAX];
	if (getcwd(buf, sizeof(buf))) {
		fprintf(stdout, "%s\n", buf);
		return EXIT_SUCCESS;
	}
	else {
		write(STDERR_FILENO, error_message, strlen(error_message));
		return EXIT_FAILURE;
	}
}

int mysh_child_process(char** args) {
	pid_t pid1;
	pid_t pid2;
	int p[2];
	// Check if redirection or pipeline is needed.
	int rv = mysh_redirection(args);
	if (rv == -2) {
		numOfCommand--;
		return EXIT_SUCCESS;
	}
	// Create a pipe for interprocess communication.
	if (pipe(p) == -1) {
		write(STDERR_FILENO, error_message, strlen(error_message));
		return EXIT_FAILURE;
	}
	pid1 = fork();
	if (pid1 < 0) {
		write(STDERR_FILENO, error_message, strlen(error_message));
		return EXIT_FAILURE;
	}
	else if (pid1 == 0) {
		// The command does not have the right format.
		if (rv == -1) {
			write(STDERR_FILENO, error_message, strlen(error_message));
			exit(1);
		}
		else if (rv == 0) {
			// Redirection or pipeline is not needed.
			// Do nothing.
		}
		else {
			if (out) {
				int newfd_out = open(output, O_WRONLY | O_CREAT | O_TRUNC, S_IRWXU);
				if (newfd_out == -1) {
					write(STDERR_FILENO, error_message, strlen(error_message));
					exit(1);
				}
				// The standard output of the program is rerouted to a specified
				// file.
				if (dup2(newfd_out, STDOUT_FILENO) == -1) {
					write(STDERR_FILENO, error_message, strlen(error_message));
					exit(1);
				}
				// The arguments of the program is terminated with a NULL pointer.
				args[rv] = NULL;
			}
			if (in) {
				int newfd_in = open(input, O_RDONLY);
				if (newfd_in == -1) {
					write(STDERR_FILENO, error_message, strlen(error_message));
					exit(1);
				}
				if (dup2(newfd_in, STDIN_FILENO) == -1) {
					write(STDERR_FILENO, error_message, strlen(error_message));
					exit(1);
				}
				args[rv] = NULL;
			}
			if (b) {
				args[rv] = NULL;
			}
			if (pl) {
				// Close unused read end.
				close(p[0]);
				if (dup2(p[1], STDOUT_FILENO) == -1) {
					write(STDERR_FILENO, error_message, strlen(error_message));
					exit(1);
				}
				close(p[1]);
				args[rv] = NULL;
			}
			else {
				// Close both ends of the pipe if no pipeline is needed.
				close(p[0]);
				close(p[1]);
			}
		}
		if (execvp(args[0], args) == -1) {
			write(STDERR_FILENO, error_message, strlen(error_message));
		}
		exit(1);
	}
	if (b) {
		// Close both ends of the pipe.
		close(p[0]);
		close(p[1]);
		for (int i = 0; i < 20; i++) {
			if (processes[i] == 0) {
				processes[i] = pid1;
				break;
			}
		}
	}
	else {
		if (pl) {
			pid2 = fork();
			if (pid2 < 0) {
				write(STDERR_FILENO, error_message, strlen(error_message));
				return EXIT_FAILURE;
			}
			else if (pid2 == 0) {
				close(p[1]);
				if (dup2(p[0], STDIN_FILENO) == -1) {
					write(STDERR_FILENO, error_message, strlen(error_message));
					exit(1);
				}
				close(p[0]);
				// The number of arguments of the second program.
				int num = numOfArgs - rv;
				char* second_args[num];
				for (int i = 0; i < num - 1; i++) {
					second_args[i] = args[rv + 1 + i];
				}
				second_args[num - 1] = NULL;
				if (execvp(second_args[0], second_args) == -1) {
					write(STDERR_FILENO, error_message, strlen(error_message));
				}
				exit(1);
			}
		}
		close(p[0]);
		close(p[1]);
		waitpid(pid1, NULL, 0);
		if (pl) {
			waitpid(pid2, NULL, 0);
		}
	}
	return EXIT_SUCCESS;
}

/**
 * Decide which function to run.
 */
int mysh_execute(char* line, char** args) {
	int r = 0;
	if (numOfArgs == 0) {
		return r;
	}
	if (strcmp(args[0], "exit") == 0) {
		mysh_kill();
		free(line);
		free(args);
		exit(0);
	}
	else if (strcmp(args[0], "cd") == 0) {
		r = mysh_cd(args);
	}
	else if (strcmp(args[0], "pwd") == 0) {
		r = mysh_pwd();
	}
	else {
		r = mysh_child_process(args);
	}
	numOfCommand++;
	return r;
}

/**
 * Wait for the terminated child.
 */
void mysh_wait(void) {
	for (int i = 0; i < 20; i++) {
		if (processes[i] != 0) {
			if (waitpid(processes[i], NULL, WNOHANG) != 0) {
				processes[i] = 0;
			}
		}
	}
}

/**
 * Loop and interpret command.
 */
void mysh_loop(void) {
	char* line;
	char** arguments;
	while (1) {
		mysh_wait();
		fprintf(stdout, "mysh (%d)> ", numOfCommand);
		fflush(stdout);
		line = mysh_read_line();
		if (strlen(line) > MAX_SIZE) {
			numOfCommand++;
			write(STDERR_FILENO, error_message, strlen(error_message));
			free(line);
			continue;
		}
		arguments = mysh_parse_line(line);
		mysh_execute(line, arguments);
		free(line);
		free(arguments);
		in = 0;
		out = 0;
		pl = 0;
		b = 0;
	}
}

/**
 * Run the shell. There should be no additional arguments to run the shell.
 */
int main(int argc, char* argv[]) {
	if (argc != 1) {
		write(STDERR_FILENO, error_message, strlen(error_message));
		exit(1);
	}
	mysh_loop();
	exit(0);
}
