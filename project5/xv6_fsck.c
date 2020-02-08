#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <string.h>
#include "fs.h"
#include "stat.h"
#include "types.h"

void* img_ptr = NULL;              /* Starting address of the first block of file system image. */
struct superblock* sb = NULL;      /* Starting address of the superblock. */
struct dinode* inode_table = NULL; /* Starting address of inode table. */
uchar* bitmap = NULL;              /* Starting address of bitmap. */
uint data_blocks;                  /* Starting address of data blocks in blocks. */
uint size;                         /* Size of file system image in blocks. */
uint nblocks;                      /* Number of data blocks. */
uint ninodes;                      /* Number of inodes. */
int* blocks_used;                  /* Number of times each block is used */

/**
 * Check the type of each inode. Each inode is either unallocated or one of
 * the valid types. Return 0 if all inodes have valid type. Return -1 if there
 * is an error.
 */
int inode_type() {
	for (uint i = 0; i < ninodes; i++) {
		short type = inode_table[i].type;
		if ((type != 0) && (type != T_FILE) && (type != T_DIR) &&
			(type != T_DEV)) {
			return -1;
		}
	}
	return 0;
}

/**
 * For in-use inodes, check if each address in the direct block is valid.
 * Return 0 if all direct addresses are valid. Return -1 if there is an error.
 */
int direct_addr() {
	for (uint i = 0; i < ninodes; i++) {
		for (uint j = 0; j < NDIRECT; j++) {
			uint addr = inode_table[i].addrs[j];
			if (addr != 0 && ((addr < data_blocks) || (addr >= size))) {
				return -1;
			}
		}
	}
	return 0;
}

/**
 * For in-use inodes, check if each indirect address is valid. Return 0 if all
 * indirect blocks are valid. Return -1 if there is an error.
 */
int indirect_addr() {
	for (uint i = 0; i < ninodes; i++) {
		// The block address that the indirect pointer points at.
		uint indirect_addr = inode_table[i].addrs[NDIRECT];
		if (indirect_addr == 0) {
			continue;
		}
		if (indirect_addr != 0 && ((indirect_addr < data_blocks) ||
			(indirect_addr >= size))) {
			return -1;
		}
		// Starting address of the data block that stores the addresses to the
		// actual user data blocks.
		uint* addr = (uint*)(img_ptr + BSIZE * indirect_addr);
		for (uint j = 0; j < NINDIRECT; j++) {
			if (addr[j] != 0 && ((addr[j] < data_blocks) || (addr[j] >= size))) {
				return -1;
			}
		}
	}
	return 0;
}

/**
 * Check if root directory exists. Returns 0 if the root inode is consistent.
 * Return -1 if there is an error.
 */
int root_check() {
	// The inode number of root inode is 1.
	if (inode_table[1].type != T_DIR) {
		return -1;
	}
	// Get the first block address of root directory entries.
	uint addr = inode_table[1].addrs[0];
	struct dirent* directory_entry = (struct dirent*)(img_ptr + BSIZE * addr);
	// The parent of the root directory is itself.
	if (!((directory_entry[0].inum == 1) && (directory_entry[1].inum == 1))) {
		return -1;
	}
	return 0;
}

/**
 * Perform integrity checks on the contents of each directory, making sure that
 * "." and ".." are the first entries. The "." entry points to itself. Return 0
 * if the format of each directory is correct. Return -1 if there is an error.
 */
int directory_format() {
	for (uint i = 0; i < ninodes; i++) {
		if (inode_table[i].type == T_DIR) {
			// Block address of directory entries.
			uint addr = inode_table[i].addrs[0];
			// Address of the first direcotry entry.
			struct dirent* directory_entry = (struct dirent*)(img_ptr + BSIZE * addr);
			if (!((strcmp(directory_entry[0].name, ".") == 0) &&
				(directory_entry[0].inum == i) &&
				(strcmp(directory_entry[1].name, "..") == 0))) {
				return -1;
			}
		}
	}
	return 0;
}

/**
 * For in-use inodes, check if each address in use is also marked in use in the
 * bitmap. Return 0 if they are consistent. Return -1 if there is an error.
 */
int address_bitmap() {
	for (uint i = 0; i < ninodes; i++) {
		if (inode_table[i].type != 0) {
			for (uint j = 0; j < NDIRECT; j++) {
				// Block address.
				uint addr = inode_table[i].addrs[j];
				if (addr != 0) {
					if (!((bitmap[addr / 8] >> (addr % 8)) & 1)) {
						return -1;
					}
				}
			}
			// Indirect pointer is in-use.
			if (inode_table[i].addrs[NDIRECT] != 0) {
				uint* addr = (uint*)(img_ptr + BSIZE * inode_table[i].addrs[NDIRECT]);
				for (uint j = 0; j < NINDIRECT; j++) {
					if (addr[j] != 0) {
						if (!((bitmap[addr[j] / 8] >> (addr[j] % 8)) & 1)) {
							return -1;
						}
					}
				}
			}
		}
	}
	return 0;
}

/**
 * Count the number of times that each address is used.
 */
void address_count() {
	for (uint i = 0; i < ninodes; i++) {
		if (inode_table[i].type != 0) {
			for (uint j = 0; j < NDIRECT; j++) {
				// Block address.
				uint addr = inode_table[i].addrs[j];
				if (addr != 0) {
					blocks_used[addr]++;
				}
			}
			// Indirect pointer is in-use.
			if (inode_table[i].addrs[NDIRECT] != 0) {
				blocks_used[inode_table[i].addrs[NDIRECT]]++;
				uint* addr = (uint*)(img_ptr + BSIZE * inode_table[i].addrs[NDIRECT]);
				for (uint j = 0; j < NINDIRECT; j++) {
					if (addr[j] != 0) {
						blocks_used[addr[j]]++;
					}
				}
			}
		}
	}
}

/**
 * For in-use inodes, check if direct address is only used once.
 */
int direct_once() {
	for (uint i = 0; i < ninodes; i++) {
		if (inode_table[i].type != 0) {
			for (uint j = 0; j < NDIRECT; j++) {
				uint addr = inode_table[i].addrs[j];
				if (addr != 0 && blocks_used[addr] > 1) {
					return -1;
				}
			}
		}
	}
	return 0;
}

/**
 * For in-use inodes, check if indirect address is only used once.
 */
int indirect_once() {
	for (uint i = 0; i < ninodes; i++) {
		if (inode_table[i].type != 0) {
			if (inode_table[i].addrs[NDIRECT] != 0) {
				if (inode_table[i].addrs[NDIRECT] > 1) {
					return -1;
				}
				uint* addr = (uint*)(img_ptr + BSIZE * inode_table[i].addrs[NDIRECT]);
				for (uint j = 0; j < NINDIRECT; j++) {
					if (addr[j] != 0 && blocks_used[addr[j] > 1]) {
						return -1;
					}
				}
			}
		}
	}
	return 0;
}

/**
 * For each block marked in-use in bitmap, check if it is actually in-use in
 * an inode or indirect block somewhere.
 */
int marked_used() {
	// Start from data blocks.
	for (uint addr = data_blocks; addr < size; addr++) {
		if (((bitmap[addr / 8] >> (addr % 8)) & 1) && blocks_used[addr] == 0) {
			return -1;
		}
	}
	return 0;
}

/**
 *
 */

int main(int argc, char* argv[]) {
	if (argc != 2) {
		fprintf(stderr, "Usage: xv6_fsck <file_system_image>.\n");
		exit(1);
	}
	// Open the file read-only and return a file descriptor.
	int fd = open(argv[1], O_RDONLY);
	// open() returns -1 if an error occured.
	if (fd < 0) {
		fprintf(stderr, "image not found.\n");
		exit(1);
	}
	struct stat buf;
	// Retrieve information about the file.
	if (fstat(fd, &buf) == -1) {
		fprintf(stderr, "fstat() failed.\n");
		exit(1);
	}
	// Map the file system image into memory.
	img_ptr = mmap(NULL, buf.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
	sb = (struct superblock*)(img_ptr + BSIZE);
	inode_table = (struct dinode*)(img_ptr + BSIZE * 2);
	// 1 byte (8 bits).
	bitmap = (uchar*)(img_ptr + BSIZE * ((sb->ninodes / 8) + 3));
	// The first data block is the 29th (zero-based numbering) block of the
	// file system image.
	data_blocks = (sb->ninodes / 8) + 4;
	size = sb->size;
	nblocks = sb->nblocks;
	ninodes = sb->ninodes;
	blocks_used = malloc(sizeof(int) * size);

	if (inode_type() == -1) {
		fprintf(stderr, "ERROR: bad inode.\n");
		exit(1);
	}

	// Check the directory format first, then perform the root check.
	if (directory_format() == -1) {
		fprintf(stderr, "ERROR: directory not properly formatted.\n");
		exit(1);
	}

	if (root_check() == -1) {
		fprintf(stderr, "ERROR: root directory does not exist.\n");
		exit(1);
	}

	if (direct_addr() == -1) {
		fprintf(stderr, "ERROR: bad direct address in inode.\n");
		exit(1);
	}

	if (indirect_addr() == -1) {
		fprintf(stderr, "ERROR: bad indirect address in inode.\n");
		exit(1);
	}

	// Count the number of times that each address is used.
	address_count();

	if (direct_once() == -1) {
		fprintf(stderr, "ERROR: direct address used more than once.\n");
		exit(1);
	}
	/*
	   if (indirect_once() == -1) {
	   fprintf(stderr, "ERROR: indirect address used more than once.\n");
	   exit(1);
	   }
	 */
	if (address_bitmap() == -1) {
		fprintf(stderr, "ERROR: address used by inode but marked free in bitmap.\n");
		exit(1);
	}

	if (marked_used() == -1) {
		fprintf(stderr, "ERROR: bitmap marks block in use but it is not in use.\n");
		exit(1);
	}

	uint* count = malloc(sizeof(uint) * ninodes);
	// Number of directory entries can be contained in a data block.
	uint num = BSIZE / (sizeof(struct dirent));
	// Get the number of times that each inode is referred to.
	// The number of references to the root inode should be 1.
	count[1] = 1;
	for (uint i = 0; i < ninodes; i++) {
		if (inode_table[i].type == 1) {
			for (uint j = 0; j < NDIRECT; j++) {
				if (inode_table[i].addrs[j] != 0) {
					uint addr = inode_table[i].addrs[j];
					struct dirent* directory_entry = (struct dirent*)(img_ptr + BSIZE * addr);
					// Skip the first two entries "." and "..".
					int start = 0;
					if (j == 0) {
						start = 2;
					}
					for (int k = start; k < num; k++) {
						if (directory_entry[k].inum != 0) {
							count[directory_entry[k].inum]++;
						}
					}
				}
			}
			if (inode_table[i].addrs[NDIRECT] != 0) {
				uint* addr = (uint*)(img_ptr + BSIZE * inode_table[i].addrs[NDIRECT]);
				for (uint j = 0; j < NINDIRECT; j++) {
					if (addr[j] != 0) {
						struct dirent* directory_entry = (struct dirent*)(img_ptr + BSIZE * addr[j]);
						for (uint k = 0; k < num; k++) {
							if (directory_entry[k].inum != 0) {
								count[directory_entry[k].inum]++;
							}
						}
					}
				}
			}
		}
	}

	// Check for condition 9 ~ 12.
	for (uint i = 0; i < ninodes; i++) {
		// Every inode in use must be referenced at least once.
		if (inode_table[i].type != 0 && count[i] == 0) {
			fprintf(stderr, "ERROR: inode marked use but not found in a directory.\n");
			exit(1);
		}
		// Inode is referred to some other directories, but it is not in use.
		if (inode_table[i].type == 0 && count[i] != 0) {
			fprintf(stderr, "ERROR: inode referred to in directory but marked free.\n");
			exit(1);
		}
		// Check if a directory has more than one link.
		if (inode_table[i].type == 1 && count[i] > 1) {
			fprintf(stderr, "ERROR: directory appears more than once in file system.\n");
			exit(1);
		}
		// Check if nlinks is consistent for regular file.
		if (inode_table[i].type == 2 && count[i] != inode_table[i].nlink) {
			fprintf(stderr, "ERROR: bad reference count for file.\n");
			exit(1);
		}
	}

	free(blocks_used);
	free(count);

	// Remove the mapped system file image from the address
	// space of the process.
	if (munmap(img_ptr, buf.st_size) == -1) {
		fprintf(stderr, "munmap() failed.\n");
		exit(1);
	}

	// Close the file descriptor, so that it no longer refers to any file
	// and may be reused.
	if (close(fd) == -1) {
		fprintf(stderr, "close() failed.\n");
		exit(1);
	}
	return 0;
}