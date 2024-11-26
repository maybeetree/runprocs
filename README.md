# RUNPROCS

Runprocs is a process supervisor,
similar to
[supervisord](https://supervisord.org/)
and
[superd](https://sr.ht/~craftyguy/superd/)

## Roadmap

- [ ] Process supervision
    - [x] oneshot processes
    - [x] continuously restarted processes
    - [x] critical processes
    - [x] track process by PID
    - [ ] track process by session ID
    - [ ] track process by process group
    - [ ] track process by PID namespace
- [ ] Remote control
    - [ ] query process status
    - [ ] stop processes
    - [ ] restart processes
    - [ ] delete processes
    - [ ] add new processes
- [ ] Misc
    - [ ] Error handling
    - [ ] Unit tests
    - [ ] Valgrind


## Installation

- compile with `make` or download a static binary from the
    [releases page]().

## Examples

Supervise one process and restart it if it exits:

```sh
runprocs --restart "myservice --myflag"
```

Supervise two processes, and restart each one if it exits

```sh
runprocs \
    --restart myservice \
    --restart myotherservice
```

Run two processes, and, if either one exits, terminal the other one
and exit.

```sh
runprocs \
    --critical myservice \
    --critical myotherservice
```

Run two "worker" processes and a "controller" process.
If either worker exits, it will get restarted.
If the controller exits,
then all workers get terminated,
and `runprocs` exits.

```sh
runprocs \
    --restart "myworker --id 1" \
    --restart "myworker --id 2" \
    --critical mycontroller
```

Run a process that won't get restarted if it exits.

```sh
runprocs --oneshot myservice
```

## Doc

### Exit policies

- `--restart`: if the process exits, restart it.
- `--oneshot`: if the process exits, do nothing.
- `--critical`: if the process exits, terminate all other processes.

