#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#include <string.h>
#include <pthread.h>

#define MAX_PROCESSES 10

typedef struct {
    pid_t pid;
    char command[256];
} Process;

Process processes[MAX_PROCESSES];
int process_count = 0;

void start_process(const char *command) {
    if (process_count >= MAX_PROCESSES) {
        printf("Maximum number of processes reached\n");
        return;
    }

    pid_t pid = fork();
    if (pid == 0) {
        // Child process
        execl("/bin/sh", "sh", "-c", command, (char *)NULL);
        exit(1);
    } else if (pid > 0) {
        // Parent process
        processes[process_count].pid = pid;
        snprintf(processes[process_count].command, sizeof(processes[process_count].command), "%s", command);
        process_count++;
        printf("Started process %d: %s\n", pid, command);
    } else {
        perror("fork");
    }
}

void restart_process(int index) {
    if (index < 0 || index >= process_count) {
        printf("Invalid process index\n");
        return;
    }

    kill(processes[index].pid, SIGTERM);
    waitpid(processes[index].pid, NULL, 0);
    start_process(processes[index].command);
}

void monitor_processes() {
    while (1) {
        for (int i = 0; i < process_count; i++) {
            int status;
            pid_t result = waitpid(processes[i].pid, &status, WNOHANG);
            if (result == 0) {
                // Process is still running
            } else if (result == processes[i].pid) {
                // Process has terminated
                printf("Process %d terminated. Restarting...\n", processes[i].pid);
                restart_process(i);
            } else {
                perror("waitpid");
            }
        }
        sleep(5);  // Check every 5 seconds
    }
}

typedef enum {
	rp_exit_pol_oneshot,
	rp_exit_pol_restart,
	rp_exit_pol_critical
} rp_exit_pol_t;

typedef struct {
	const char *command;
	rp_exit_pol_t exit_pol;
} rp_proc_opts_t;

typedef struct {
	rp_proc_opts_t *proc_optss;
	int num_procs;
} rp_procs_opts_t;

const char* RP_EXIT_POL_ONESHOT = "oneshot";
const char* RP_EXIT_POL_RESTART = "restart";
const char* RP_EXIT_POL_CRITICAL = "critical";

void rp_proc_opts_init(
		rp_proc_opts_t *proc_opts
		) {
	proc_opts -> exit_pol = rp_exit_pol_critical;
}

int rp_procs_opts_init(
		rp_procs_opts_t *procs_opts,
		int argc,
		const char **argv
		) {
	int i;

	procs_opts -> num_procs = 0;

	for (i = 0; i < argc; i++) {
		if (strncmp("--", argv[i], 2) != 0) {
			procs_opts -> num_procs++;
		}
	}

	procs_opts -> proc_optss = (rp_proc_opts_t*) malloc(
		sizeof(rp_proc_opts_t) * procs_opts -> num_procs
		);

	for (i = 0; i < procs_opts -> num_procs; i++) {
		rp_proc_opts_init(procs_opts -> proc_optss + i);
	}

	return 0;
}

int rp_procs_opts_parse_argv(rp_procs_opts_t *procs_opts, int argc, const char **argv) {
	int i;
	int i_proc;

	i_proc = 0;

	for (i = 0; i < argc; i++) {
		if (strncmp("--", argv[i], 2) == 0) {
			if (i_proc >= procs_opts -> num_procs) {
				puts("Err.");
			}

			if (strcmp(RP_EXIT_POL_ONESHOT, argv[i] + 2) == 0) {
				procs_opts -> proc_optss[i_proc].exit_pol =
					rp_exit_pol_oneshot;
			}
			else if (strcmp(RP_EXIT_POL_RESTART, argv[i] + 2) == 0) {
				procs_opts -> proc_optss[i_proc].exit_pol =
					rp_exit_pol_restart;
			}
			else if (strcmp(RP_EXIT_POL_CRITICAL, argv[i] + 2) == 0) {
				procs_opts -> proc_optss[i_proc].exit_pol =
					rp_exit_pol_critical;
			}
			else {
				puts("Unknown!\n");
			}
		}
		else {
			procs_opts -> proc_optss[i_proc].command =
				argv[i];
			i_proc++;
		}
	}

	return 0;
}

typedef struct {
	rp_procs_opts_t procs_opts;
	pid_t *pids;
	int exited_proc;
	int proc_to_wait;
	pthread_cond_t proc_exited_cond;
	pthread_mutex_t proc_exited_mutex;
} rp_ctx_t;

void rp_ctx_init(
		rp_ctx_t *ctx,
		int argc,
		const char **argv
		) {
	rp_procs_opts_init(&(ctx -> procs_opts), argc, argv);
	/*
	ctx -> proc_exited_cond = PTHREAD_COND_INITIALIZER;
	ctx -> proc_exited_mutex = PTHREAD_MUTEX_INITIALIZER;
	*/
	pthread_cond_init(&(ctx -> proc_exited_cond), NULL);
	pthread_mutex_init(&(ctx -> proc_exited_mutex), NULL);

	ctx -> pids = (pid_t*)
		malloc(sizeof(pid_t) * ctx -> procs_opts.num_procs);

	ctx -> exited_proc = -1;
}

int rp_ctx_parse_argv(rp_ctx_t *ctx, int argc, const char **argv) {
	rp_procs_opts_parse_argv(&(ctx -> procs_opts), argc, argv);
}

int rp_ctx_start_proc(rp_ctx_t *ctx, int proc_num) {
    pid_t pid = fork();
    if (pid == 0) {
        // Child process
        execl("/bin/sh", "sh", "-c", ctx -> procs_opts.proc_optss[proc_num].command, (char *)NULL);
        exit(1);
    } else if (pid > 0) {
        // Parent process
        ctx -> pids[proc_num] = pid;
        /*snprintf(processes[process_count].command, sizeof(processes[process_count].command), "%s", command);*/
        /*process_count++;*/
        /*printf("Started process: %s\n", command);*/
    } else {
        perror("fork");
    }
}

void* rp_ctx_wait_proc_thread(void *ctx_as_void) {
	rp_ctx_t *ctx = (rp_ctx_t*)(ctx_as_void);
	int proc_num = ctx -> proc_to_wait;

	pid_t pid = ctx -> pids[proc_num];
	int status;

	// Hang until it exits
	pid_t result = waitpid(pid, &status, 0);
	// Okay, it exited
	
	pthread_mutex_lock(&(ctx -> proc_exited_mutex));
	pthread_cond_signal(&(ctx -> proc_exited_cond));
	ctx -> exited_proc = proc_num;
	pthread_mutex_unlock(&(ctx -> proc_exited_mutex));

	return NULL;
}

void* rp_ctx_wait_proc(rp_ctx_t *ctx, int proc_num) {
	ctx -> proc_to_wait = proc_num;
	pthread_t thread_id;
	pthread_create(&thread_id, NULL, rp_ctx_wait_proc_thread, ctx);
}

int rp_ctx_wait_till_exit(rp_ctx_t *ctx) {
	pthread_mutex_lock(&(ctx -> proc_exited_mutex));
	pthread_cond_wait(&(ctx -> proc_exited_cond), &(ctx -> proc_exited_mutex));
	pthread_mutex_unlock(&(ctx -> proc_exited_mutex));
}

int rp_ctx_handle_exit(rp_ctx_t *ctx) {
	/* assert ctx -> exited_proc >= 0 */
	pthread_mutex_lock(&(ctx -> proc_exited_mutex));

	printf("Proc number %i exited.\n", ctx -> exited_proc);

	switch (ctx -> procs_opts.proc_optss[ctx -> exited_proc].exit_pol) {
		case rp_exit_pol_oneshot:
			break;
		case rp_exit_pol_restart:

			rp_ctx_start_proc(ctx, ctx -> exited_proc);
			rp_ctx_wait_proc(ctx, ctx -> exited_proc);

			break;
		case rp_exit_pol_critical:
			break;
		default:
			/* panic */
			break;
	}

	ctx -> exited_proc = -1;

	pthread_mutex_unlock(&(ctx -> proc_exited_mutex));
}

int rp_ctx_supervise(rp_ctx_t *ctx) {
	for (int i = 0; i < ctx -> procs_opts.num_procs; i++) {
		rp_ctx_start_proc(ctx, i);
		rp_ctx_wait_proc(ctx, i);
	}

	for (;;) {
		rp_ctx_wait_till_exit(ctx);
		rp_ctx_handle_exit(ctx);
	}
}

int main(int argc, const char **argv) {
	/*
    start_process("for i in $(seq 2); do echo 'Process 1 $i'; sleep 2; done");
    start_process("while true; do echo 'Process 2'; sleep 3; done");

    monitor_processes();
	*/
	rp_ctx_t ctx;
	rp_ctx_init(&ctx, argc - 1, argv + 1);
	rp_ctx_parse_argv(&ctx, argc - 1, argv + 1);

	/*rp_ctx_watch_procs(&ctx);*/

	rp_ctx_supervise(&ctx);

	/*pthread_join(thread_id, NULL);*/

    return 0;
}

