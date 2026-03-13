all: mschedule

mschedule: mschedule.c
	gcc -Wall -g -pthread -o mschedule mschedule.c -lm

clean:
	rm -f *~ mschedule
