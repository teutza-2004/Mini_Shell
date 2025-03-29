// SPDX-License-Identifier: BSD-3-Clause

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>

#include <fcntl.h>
#include <unistd.h>
#include <stdio.h> //
#include <stdlib.h> //
#include <string.h> //

#include "cmd.h"
#include "utils.h"

#define READ		0
#define WRITE		1

/**
 * Internal change-directory command.
 */
static bool shell_cd(word_t *dir)
{
	/* TODO: Execute cd. */
	if (dir == NULL || dir->string == NULL)
		return false;
	int cd = chdir(dir->string);

	if (cd < 0)
		return false;
	return true;
}

/**
 * Internal exit/quit command.
 */
static int shell_exit(void)
{
	/* TODO: Execute exit/quit. */
	exit(0);
}

/**
 * Parse a simple command (internal, environment variable assignment,
 * external command).
 */
static int parse_simple(simple_command_t *s, int level, command_t *father)
{
	/* TODO: Sanity checks. */
	if (s == NULL || father == NULL || father != s->up)
		return 1;
	/* TODO: If builtin command, execute the command. */
	char *string = get_word(s->verb); // extrag comanda

	if (strchr(string, '=') == NULL) { // cazul in care nu este atribuire de variabila
		if (strcmp(string, "cd") == 0) {
			free(string);

			// if-uri pentru redirectari ale rezultatului comezii cd
			// doar creaza fisierul si nu redirecteaza nimic in el
			if (s->out != NULL && s->out->string != NULL) {
				int fd = open(s->out->string, O_WRONLY | O_CREAT | ((s->io_flags & IO_OUT_APPEND) ? O_APPEND : O_TRUNC), 0644);

				if (fd < 0)
					return -1;
				close(fd);
			}
			// creaza fisierul si redirecteaza mesajul erorii
			if (s->err != NULL && s->err->string != NULL) {
				int fd = open(s->err->string, O_WRONLY | O_CREAT | ((s->io_flags & IO_ERR_APPEND) ? O_APPEND : O_TRUNC), 0644);

				if (fd < 0)
					return -1;
				close(fd);
			}
			//apelul efectiv al comezii
			bool cd = shell_cd(s->params);

			if (cd == true)
				return 0;
			else
				return 1;
		} else if (strcmp(string, "exit") == 0 || strcmp(string, "quit") == 0) {
			free(string);
			return shell_exit();
		}
	} else if (strcmp(s->verb->next_part->string, "=") == 0) {
		/* TODO: If variable assignment, execute the assignment and return
		 * the exit status.
		 */
		char *val = get_word(s->verb->next_part->next_part); // extrag valoarea data variabilei
		int set = setenv(s->verb->string, val, 1); // setez valoarea variabilei respective

		free(val);
		free(string);
		return set;
	}

	/* TODO: If external command:
	 *   1. Fork new process
	 *     2c. Perform redirections in child
	 *     3c. Load executable in child
	 *   2. Wait for child
	 *   3. Return exit status
	 */

	pid_t pid = fork();

	if (pid == 0) { // sunt in copil
		char *out_string = get_word(s->out); // fisierul de iesire
		char *in_string = get_word(s->in); // fisierul de intrare
		char *err_string = get_word(s->err); // fisierul de erroare

		if (s->in != NULL && in_string != NULL) {
			// pentru redirectare <
			// deschid fisierul de intrare si schimb fd-ul stdin cu cel al fisierului
			int in_fd = open(in_string, O_RDONLY);

			if (in_fd < 0) {
				close(in_fd);
				exit(1);
			}
			dup2(in_fd, STDIN_FILENO);
			close(in_fd);
		}
		if (s->out != NULL && s->err != NULL && strcmp(out_string, err_string) == 0) {
			// pentru redirectare &>
			// deschid fisierul de iesire si schimb fd-urile stdin si stdout cu cel al fisierului
			int out_fd = open(out_string, O_WRONLY | O_CREAT | ((s->io_flags & IO_OUT_APPEND) ? O_APPEND : O_TRUNC), 0644);

			if (out_fd < 0) {
				close(out_fd);
				exit(1);
			}
			dup2(out_fd, STDOUT_FILENO);
			dup2(out_fd, STDERR_FILENO);
			close(out_fd);
		} else {
			if (s->out != NULL && out_string != NULL) {
				// pentru redirectare >
				// deschid fisierul de iesire si schimb fd-ul stdout cu cel al fisierului
				int out_fd = open(out_string, O_WRONLY | O_CREAT | ((s->io_flags & IO_OUT_APPEND) ? O_APPEND : O_TRUNC), 0644);

				if (out_fd < 0) {
					close(out_fd);
					exit(1);
				}
				dup2(out_fd, STDOUT_FILENO);
				close(out_fd);
			}
			if (s->err != NULL && err_string != NULL) {
				// pentru redirectare 2>
				// deschid fisierul de eroare si schimb fd-ul stderr cu cel al fisierului
				int err_fd = open(err_string, O_WRONLY | O_CREAT | ((s->io_flags & IO_ERR_APPEND) ? O_APPEND : O_TRUNC), 0644);

				if (err_fd < 0) {
					close(err_fd);
					exit(1);
				}
				dup2(err_fd, STDERR_FILENO);
				close(err_fd);
			}
		}
		free(out_string);
		free(in_string);
		free(err_string);

		int size = 0;
		char **argv = get_argv(s, &size); // scot argumentele comenzii
		char *err_buf = calloc(1000, sizeof(char)); //

		execvp(argv[0], argv); // execut comanda cu argumentele sale
		// scriu la fd-ul stderr mesaj de esuare a executiei comenzii (pt comenzile care nu exista)
		sprintf(err_buf, "Execution failed for '%s'\n", argv[0]);
		write(STDERR_FILENO, err_buf, strlen(err_buf));
		free(err_buf);

		for (int i = 0; *(argv + i) != NULL; i++)
			free(*(argv + i));
		free(argv);
		exit(1);
	}
	// sunt in parinte
	// astept executia copilului si afisez statusul de exit
	int stat = 0;

	waitpid(pid, &stat, 0);
	free(string);
	return WEXITSTATUS(stat);
	/* TODO: Replace with actual exit status. */
}

/**
 * Process two commands in parallel, by creating two children.
 */
static bool run_in_parallel(command_t *cmd1, command_t *cmd2, int level,
		command_t *father)
{
	/* TODO: Execute cmd1 and cmd2 simultaneously. */
	pid_t pid1 = fork();

	if (pid1 == 0) {
		// sunt in copilul 1 (cmd1)
		int pc = parse_command(cmd1, level + 1, father); // apelez functia de parsare si executie pt cmd1

		exit(pc);
	}

	pid_t pid2 = fork();

	if (pid2 == 0) {
		// sunt in copilul 2 (cmd2)
		int pc = parse_command(cmd2, level + 1, father); // apelez functia de parsare si executie pt cmd2

		exit(pc);
	}
	// sunt in parinte
	// astept sa isi termine executia cele doua comenzi si returnez exit status-ul "comun"
	int stat1 = 0, stat2 = 0;

	waitpid(pid1, &stat1, 0);
	waitpid(pid2, &stat2, 0);
	return WEXITSTATUS(stat1) && WEXITSTATUS(stat2);
	/* TODO: Replace with actual exit status. */
}

/**
 * Run commands by creating an anonymous pipe (cmd1 | cmd2).
 */
static bool run_on_pipe(command_t *cmd1, command_t *cmd2, int level,
		command_t *father)
{
	/* TODO: Redirect the output of cmd1 to the input of cmd2. */
	int pipefd[2];

	pipe(pipefd); // creez legatura dintre cele doua comenzi
	pid_t pid1 = fork();

	if (pid1 == 0) {
		// sunt in copil 1 (cmd1)
		// cmd1 trbuie numai sa scrie in pipe
		close(pipefd[READ]); // inchid fd-ul de citire al pipe-ului
		dup2(pipefd[WRITE], STDOUT_FILENO); // dau la stdout fd-ul de scriere al pipe-ului
		close(pipefd[WRITE]); // inchid fd-ul de scriere al pipe-ului
		int pc = parse_command(cmd1, level + 1, father); // apelez functia de parsare si executie pt cmd1

		exit(pc);
	}

	pid_t pid2 = fork();

	if (pid2 == 0) {
		// sunt in copil 2 (cmd2)
		// cmd2 trbuie doar sa citeasca din pipe
		close(pipefd[WRITE]); // inchid fd-ul de scriere al pipe-ului
		dup2(pipefd[READ], STDIN_FILENO); // dau la stdin fd-ul de citire al pipe-ului
		close(pipefd[READ]); // inchid fd-ul de citire al pipe-ului
		int pc = parse_command(cmd2, level + 1, father); // apelez functia de parsare si executie pt cmd2

		exit(pc);
	}
	// sunt in parinte
	// inchid pipe-ul, astept sa terimne executia cele doua comenzi si returnez exit status-ul cmd2
	close(pipefd[READ]);
	close(pipefd[WRITE]);
	int stat1 = 0, stat2 = 0;

	waitpid(pid1, &stat1, 0);
	waitpid(pid2, &stat2, 0);

	return WEXITSTATUS(stat2);
	/* TODO: Replace with actual exit status. */
}

/**
 * Parse and execute a command.
 */
int parse_command(command_t *c, int level, command_t *father)
{
	/* TODO: sanity checks */
	if (c == NULL)
		return -1;

	if (c->op == OP_NONE) {
		/* TODO: Execute a simple command. */
		return parse_simple(c->scmd, level + 1, c); /* TODO: Replace with actual exit code of command. */
	}

	switch (c->op) {
	case OP_SEQUENTIAL: {
		/* TODO: Execute the commands one after the other. */
		parse_command(c->cmd1, level + 1, c);
		return parse_command(c->cmd2, level + 1, c);
	}

	case OP_PARALLEL: {
		/* TODO: Execute the commands simultaneously. */
		return run_in_parallel(c->cmd1, c->cmd2, level + 1, c);
	}

	case OP_CONDITIONAL_NZERO: {
		/* TODO: Execute the second command only if the first one
		 * returns non zero. `||`
		 */
		int ret_nz = parse_command(c->cmd1, level + 1, c);

		if (ret_nz != 0)
			return parse_command(c->cmd2, level + 1, c);
		return ret_nz;
	}

	case OP_CONDITIONAL_ZERO: {
		/* TODO: Execute the second command only if the first one
		 * returns zero. `&&`
		 */
		int ret_z = parse_command(c->cmd1, level + 1, c);

		if (ret_z == 0)
			return parse_command(c->cmd2, level + 1, c);
		return ret_z;
	}

	case OP_PIPE: {
		/* TODO: Redirect the output of the first command to the
		 * input of the second.
		 */
		return run_on_pipe(c->cmd1, c->cmd2, level + 1, c);
	}

	default:
		return SHELL_EXIT;
	}

	return 0; /* TODO: Replace with actual exit code of command. */
}
