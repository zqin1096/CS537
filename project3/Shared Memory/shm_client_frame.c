#include <time.h>
#include <sys/time.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <fcntl.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <string.h>
#define SHM_NAME "zhaoyin_xuezhan"

// Mutex variables
pthread_mutex_t* mutex;

int fd_shm;
char* shm_base;

typedef struct {
	int inuse;
	int pid;
	char birth[25];
	char clientString[10];
	int elapsed_sec;
	double elapsed_msec;
} stats_t;

stats_t* p;

void exit_handler(int sig) {
	// critical section begins.
	pthread_mutex_lock(mutex);
	// Mark its segment as valid so that the segment can be used by another
	// client later.
	p->inuse = 0;
	pthread_mutex_unlock(mutex);
	// critical section ends.

	if (munmap(shm_base, getpagesize()) == -1) {
		fprintf(stderr, "munmap() failed.\n");
		exit(1);
	}

	// Close the shared memory segment as if it was a file
	if (close(fd_shm) == -1) {
		fprintf(stderr, "close() failed.\n");
		exit(1);
	}
	exit(0);
}

int main(int argc, char* argv[]) {
	struct timeval t1, t2;
	// Signal handling
	// Use sigaction() here and call exit_handler
	struct sigaction sa;
	memset(&sa, 0, sizeof(sa));
	// Install signal handler
	sa.sa_handler = exit_handler;
	if (sigaction(SIGINT, &sa, NULL) == -1) {
		fprintf(stderr, "SIGINT error.\n");
		exit(1);
	}
	if (sigaction(SIGTERM, &sa, NULL) == -1) {
		fprintf(stderr, "SIGTERM error.\n");
		exit(1);
	}

	// Open the preexisting shared memory segment created by shm_server
	fd_shm = shm_open(SHM_NAME, O_RDWR, 0660);
	if (fd_shm == -1) {
		fprintf(stderr, "shm_open() failed.\n");
		exit(1);
	}

	shm_base = mmap(NULL, getpagesize(), PROT_READ | PROT_WRITE, MAP_SHARED,
		fd_shm, 0);
	if (shm_base == MAP_FAILED) {
		fprintf(stderr, "mmap() failed.\n");
		exit(1);
	}
	// Point the mutex to the particular segment of the shared memory page
	mutex = (pthread_mutex_t*)shm_base;
	stats_t* stats = (stats_t*)((void*)shm_base + sizeof(pthread_mutex_t));

	// critical section begins
	pthread_mutex_lock(mutex);
	// client searching through the segments of the page to find a valid
	// (or available) segment and then mark this segment as invalid.
	// The value of maxClients is 63.
	for (int i = 0; i < 63; i++) {
		if (stats[i].inuse == 0) {
			stats[i].inuse = 1;
			stats[i].pid = getpid();
			time_t curtime;
			// check return value
			if (time(&curtime) == ((time_t)-1)) {
				fprintf(stderr, "Failure to obtain the current time.\n");
				exit(1);
			}
			char* c_time_string;
			c_time_string = ctime(&curtime);
			gettimeofday(&t1, NULL);
			if (c_time_string == NULL) {
				fprintf(stderr, "Failure to convert the current time.\n");
				exit(1);
			}
			// Exclude the newline character.
			strncpy(stats[i].birth, c_time_string, strlen(c_time_string) - 1);
			strcpy(stats[i].clientString, argv[1]);
			p = stats + i;
			pthread_mutex_unlock(mutex);
			goto LOOP;
		}
	}
	pthread_mutex_unlock(mutex);
	fprintf(stderr, "Error.\n");
	exit(1);
	// critical section ends

LOOP: while (1) {
	// Fill in fields in stats_t
	gettimeofday(&t2, NULL);
	p->elapsed_sec = t2.tv_sec - t1.tv_sec;
	p->elapsed_msec = (t2.tv_usec - t1.tv_usec) / 1000.0f;
	sleep(1);
	// display the PIDs of all the active clients
	fprintf(stdout, "Active clients :");
	for (int i = 0; i < 63; i++) {
		if (stats[i].inuse == 1) {
			fprintf(stdout, " %d ", stats[i].pid);
		}
	}
	fprintf(stdout, "\n");
}
return 0;
}