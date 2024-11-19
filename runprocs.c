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

const char* RP_EXIT_POL_ONESHOT = "oneshot";
const char* RP_EXIT_POL_RESTART = "restart";
const char* RP_EXIT_POL_CRITICAL = "critical";

typedef enum {
	rp_exit_pol_oneshot,
	rp_exit_pol_restart,
	rp_exit_pol_critical
} rp_exit_pol_t;

typedef struct {
	const char *command;
	rp_exit_pol_t exit_pol;
} rp_opts_t;

typedef struct {
	pid_t pid;
	pthread_t thread;
} rp_proc_t;

typedef struct {
	rp_opts_t *opts;
	rp_proc_t *procs;
	int num_procs;

	int exited_proc;
	int proc_to_wait;
	int is_dying;
	int procs_left;
	pthread_cond_t proc_exited_cond;
	pthread_mutex_t proc_exited_mutex;
} rp_t;

int rp_init(
		rp_t *rp,
		int argc,
		const char **argv
		) {
	int i;

	rp -> num_procs = 0;

	for (i = 0; i < argc; i++) {
		if (strncmp("--", argv[i], 2) != 0) {
			rp -> num_procs++;
		}
	}

	rp -> opts = (rp_opts_t*) malloc(
		sizeof(rp_opts_t) * rp -> num_procs
		);

	rp -> procs = (rp_proc_t*) malloc(
		sizeof(rp_proc_t) * rp -> num_procs
		);

	int i_proc = 0;

	for (i = 0; i < argc; i++) {
		if (strncmp("--", argv[i], 2) == 0) {
			if (i_proc >= rp -> num_procs) {
				puts("Err.");
			}

			if (strcmp(RP_EXIT_POL_ONESHOT, argv[i] + 2) == 0) {
				rp -> opts[i_proc].exit_pol = rp_exit_pol_oneshot;
			}
			else if (strcmp(RP_EXIT_POL_RESTART, argv[i] + 2) == 0) {
				rp -> opts[i_proc].exit_pol = rp_exit_pol_restart;
			}
			else if (strcmp(RP_EXIT_POL_CRITICAL, argv[i] + 2) == 0) {
				rp -> opts[i_proc].exit_pol = rp_exit_pol_critical;
			}
			else {
				puts("Unknown!\n");
			}
		}
		else {
			rp -> opts[i_proc].command = argv[i];
			i_proc++;
		}
	}

	pthread_cond_init(&(rp -> proc_exited_cond), NULL);
	pthread_mutex_init(&(rp -> proc_exited_mutex), NULL);
	rp -> exited_proc = -1;
	rp -> is_dying = 0;
	rp -> procs_left = rp -> num_procs;

	return 0;
}

int rp_start_proc(rp_t *rp, int proc_num) {
    pid_t pid = fork();
    if (pid == 0) {
        // Child process
        execl("/bin/sh", "sh", "-c", rp -> opts[proc_num].command, (char *)NULL);
        exit(1);
    }
	else if (pid > 0) {
        // Parent process
        rp -> procs[proc_num].pid = pid;
        /*snprintf(processes[process_count].command, sizeof(processes[process_count].command), "%s", command);*/
        /*process_count++;*/
        /*printf("Started process: %s\n", command);*/
    } else {
        perror("fork");
    }
}

typedef struct {
	rp_t *rp;
	int proc_num;
} rp_wait_proc_args_t;

void* rp_wait_proc_thread(void *args_p) {
	rp_wait_proc_args_t *args = args_p;
	rp_t *rp = args -> rp;
	int proc_num = args -> proc_num;

	free(args);

	pid_t pid = rp -> procs[proc_num].pid;
	int status;

	// Hang until it exits
	pid_t result = waitpid(pid, &status, 0);
	// Okay, it exited
	
	pthread_mutex_lock(&(rp -> proc_exited_mutex));
	pthread_cond_signal(&(rp -> proc_exited_cond));
	rp -> exited_proc = proc_num;
	pthread_mutex_unlock(&(rp -> proc_exited_mutex));

	return NULL;
}

void* rp_wait_proc(rp_t *rp, int proc_num) {
	/* Dynamically allocate it because otherwise
	 * it will go out of scope once this function exits,
	 * and the thread will be left with garbage data
	 */
	rp_wait_proc_args_t *args = malloc(sizeof(rp_wait_proc_args_t));
	args -> rp = rp;
	args -> proc_num = proc_num;

	pthread_t thread_id;
	pthread_create(&thread_id, NULL, rp_wait_proc_thread, args);
}

int rp_wait_exit(rp_t *rp) {
	pthread_mutex_lock(&(rp -> proc_exited_mutex));
	pthread_cond_wait(&(rp -> proc_exited_cond), &(rp -> proc_exited_mutex));
	pthread_mutex_unlock(&(rp -> proc_exited_mutex));
}

int rp_die(rp_t *rp) {
	rp -> is_dying = 1;
	for (int i = 0; i < rp -> num_procs; i++) {
		kill(rp -> procs[i].pid, SIGTERM);
		/*waitpid(rp -> procs[i].pid, NULL, 0);*/
		fprintf(stderr, "Kill %i\n", rp -> procs[i].pid);
	}
}

int rp_handle_exit(rp_t *rp) {
	/* assert ctx -> exited_proc >= 0 */
	pthread_mutex_lock(&(rp -> proc_exited_mutex));

	fprintf(stderr, "Proc number %i exited.\n", rp -> exited_proc);

	if (rp -> is_dying) {
		fprintf(stderr, "Am dying, no restart.\n");
		pthread_mutex_unlock(&(rp -> proc_exited_mutex));
		return 0;
	}

	switch (rp -> opts[rp -> exited_proc].exit_pol) {
		case rp_exit_pol_oneshot:

			if (--(rp -> procs_left) <= 0) {
				fprintf(
					stderr,
					"All procs were oneshot and all have exited.\n"
					);
				rp_die(rp);
			}

			break;
		case rp_exit_pol_restart:

			rp_start_proc(rp, rp -> exited_proc);
			rp_wait_proc(rp, rp -> exited_proc);

			break;
		case rp_exit_pol_critical:

			rp_die(rp);

			break;
		default:
			/* panic */
			break;
	}

	rp -> exited_proc = -1;

	pthread_mutex_unlock(&(rp -> proc_exited_mutex));
}

int rp_supervise(rp_t *rp) {
	for (int i = 0; i < rp -> num_procs; i++) {
		rp_start_proc(rp, i);
		rp_wait_proc(rp, i);
	}

	while (!(rp -> is_dying)) {
		rp_wait_exit(rp);
		rp_handle_exit(rp);
	}

	fprintf(stderr, "Bye!\n");
}

int main(int argc, const char **argv) {
	/*
    start_process("for i in $(seq 2); do echo 'Process 1 $i'; sleep 2; done");
    start_process("while true; do echo 'Process 2'; sleep 3; done");

    monitor_processes();
	*/
	rp_t rp;
	rp_init(&rp, argc - 1, argv + 1);

	/*rp_ctx_watch_procs(&ctx);*/

	rp_supervise(&rp);

	/*pthread_join(thread_id, NULL);*/

    return 0;
}

