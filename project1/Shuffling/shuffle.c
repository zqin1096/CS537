#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

void usage(void) {
	fprintf(stderr, "Usage: shuffle -i inputfile -o outputfile\n");
	exit(1);
}

int main(int argc, char* argv[]) {
	// arguments
	char* inFile = NULL;
	char* outFile = NULL;
	int c;
	opterr = 0;
	while ((c = getopt(argc, argv, "i:o:")) != -1) {
		switch (c) {
		case 'i':
			inFile = strdup(optarg);
			break;
		case 'o':
			outFile = strdup(optarg);
			break;
		default:
			usage();
		}
	}

	if (inFile == NULL || outFile == NULL) {
		usage();
	}

	// Open the file read-only and return a FILE pointer.
	FILE* fp = fopen(inFile, "r");
	// fopen return NULL if an error occured.
	if (fp == NULL) {
		fprintf(stderr, "Error: Cannot open file %s\n", inFile);
		exit(1);
	}

	// Find the size of the file.
	struct stat st;
	// Total size, in bytes.
	off_t fsize;
	if (stat(inFile, &st) == -1) {
		fprintf(stderr, "stat() failed\n");
		exit(1);
	}
	fsize = st.st_size;

	// Number of lines.
	int nums = 0;
	char ch = '\0';
	// Find the number of lines in the file.
	for (int i = 0; i < fsize; i++) {
		if (fread(&ch, sizeof(char), 1, fp) != 1) {
			fprintf(stderr, "fread() failed\n");
			exit(1);
		}
		if (ch == '\n') {
			nums++;
		}
	}
	// Store the length of each line.
	int* lengths = malloc(sizeof(int) * nums);
	// Reset counter for the number of lines.
	nums = 0;
	// Sets the file position indicator to the beginning of the file.
	rewind(fp);

	int length = 0;
	// Find the length of each line.
	for (int i = 0; i < fsize; i++) {
		length++;
		if (fread(&ch, sizeof(char), 1, fp) != 1) {
			fprintf(stderr, "fread() failed\n");
			exit(1);
		}
		if (ch == '\n') {
			// Null-terminated string.
			length++;
			lengths[nums++] = length;
			length = 0;
		}
	}
	// Store each line into a buffer.
	char** buffer = malloc(sizeof(char*) * nums);
	rewind(fp);

	// Read each line into the buffer.
	for (int i = 0; i < nums; i++) {
		int len = lengths[i];
		char* token = malloc(len);
		if (fread(token, len - 1, 1, fp) != 1) {
			fprintf(stderr, "fread() failed\n");
			exit(1);
		}
		token[len - 1] = '\0';
		buffer[i] = token;
	}

	// Open and create output file.
	FILE* fp1 = fopen(outFile, "w");
	if (fp1 == NULL) {
		fprintf(stderr, "Error: Cannot open file %s\n", inFile);
		exit(1);
	}
	int left = 0;
	int right = nums - 1;
	// Alternate printing lines starting at the beginning of the file moving
	// forward and lines at the end of the file moving backwards.
	while (left <= right) {
		if (left != right) {
			int length1 = strlen(buffer[left]);
			int length2 = strlen(buffer[right]);
			if (fwrite(buffer[left], sizeof(char), length1, fp1) != length1) {
				fprintf(stderr, "fwrite() failed\n");
				exit(1);
			}
			if (fwrite(buffer[right], sizeof(char), length2, fp1) != length2) {
				fprintf(stderr, "fwrite() failed\n");
				exit(1);
			}
		}
		else {
			int length = strlen(buffer[left]);
			if (fwrite(buffer[left], sizeof(char), length, fp1) != length) {
				fprintf(stderr, "fwrite() failed\n");
				exit(1);
			}
		}
		left++;
		right--;
	}
	free(inFile);
	free(outFile);
	fclose(fp);
	fclose(fp1);
	free(lengths);
	for (int i = 0; i < nums; i++) {
		free(buffer[i]);
	}
	free(buffer);
}
