CFLAGS=-ansi -pedantic -Wextra -Werror -g -Wfatal-errors

default:
	gcc ${CFLAGS} ./runprocs.c -o runprocs

static:
	gcc -static ${CFLAGS} ./runprocs.c -o runprocs

workflow:
	musl-gcc -static ${CFLAGS} ./runprocs.c -o runprocs

