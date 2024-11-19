#!/bin/sh

# critical
./runprocs \
	--restart 'for i in $(seq 3); do echo aaa; sleep 0.5; done' \
	\
	--restart 'for i in $(seq 2); do echo bbb; sleep 0.7; done' \
	\
	--oneshot 'sleep 10; echo ayy' \
	\
	--critical 'sleep 4; echo lmao'

# all oneshot
./runprocs \
	--oneshot 'sleep 2' \
	\
	--oneshot 'sleep 1' \
	\
	--oneshot 'sleep 0.5'
