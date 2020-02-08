#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <pthread.h>
#include <string.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#define SHM_NAME "zhaoyin_xuezhan"

// Mutex variables
pthread_mutex_t* mutex;
pthread_mutexattr_t mutexAttribute;

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

void exit_handler(int sig) {
	// Remove the mapped shared memory segment from the address
	// space of the process.
	if (munmap(shm_base, getpagesize()) == -1) {
		fprintf(stderr, "munmap() failed.\n");
		exit(1);
	}

	// Close the shared memory segment as if it was a file
	if (close(fd_shm) == -1) {
		fprintf(stderr, "close() failed.\n");
		exit(1);
	}

	// Remove the shared memory segment from the file system
	if (shm_unlink(SHM_NAME) == -1) {
		fprintf(stderr, "shm_unlink() failed.\n");
		exit(1);
	}
	exit(0);
}

int main(int argc, char* argv[]) {
	struct sigaction sa;
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = exit_handler;
	if (sigaction(SIGINT, &sa, NULL) == -1) {
		fprintf(stderr, "SIGINT error.\n");
		exit(1);
	}
	// Creating a new shared memory segment.
	fd_shm = shm_open(SHM_NAME, O_RDWR | O_CREAT, 0660);
	if (fd_shm == -1) {
		fprintf(stderr, "shm_open() failed.\n");
		exit(1);
	}
	int pageSize = getpagesize();
	// Configure the size of the shared memory segment.
	if (ftruncate(fd_shm, pageSize) == -1) {
		fprintf(stderr, "fruncate() failed.\n");
		exit(1);
	}
	// Map the shared memory segment to the address space of the process.
	shm_base = mmap(NULL, pageSize, PROT_READ | PROT_WRITE, MAP_SHARED,
		fd_shm, 0);
	if (shm_base == MAP_FAILED) {
		fprintf(stderr, "mmap() failed.\n");
		exit(1);
	}
	// Point the mutex to the segment of the shared memory page.
	mutex = (pthread_mutex_t*)shm_base;
	// Initializing mutex.
	if (pthread_mutexattr_init(&mutexAttribute) != 0) {
		fprintf(stderr, "pthread_mutexattr_init() failed.\n");
		exit(1);
	}
	pthread_mutexattr_setpshared(&mutexAttribute, PTHREAD_PROCESS_SHARED);
	pthread_mutex_init(mutex, &mutexAttribute);
	// Initialization of the rest of the segments in the shared memory page.
	stats_t* stats = (stats_t*)((void*)shm_base + sizeof(pthread_mutex_t));
	int iteration = 1;
	while (1)
	{
		// Display the statistics of active clients.
		for (int i = 0; i < 63; i++) {
			if (stats[i].inuse == 1) {
				fprintf(stdout, "%d, ", iteration);
				fprintf(stdout, "pid : %d, ", stats[i].pid);
				fprintf(stdout, "birth : %s, ", stats[i].birth);
				fprintf(stdout, "elapsed : %d s %.4f ms, ",
					stats[i].elapsed_sec,
					stats[i].elapsed_msec);
				fprintf(stdout, "%s\n", stats[i].clientString);
			}
		}
		sleep(1);
		iteration++;
	}

	return 0;
}