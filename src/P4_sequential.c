#include <pthread.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "parsePGM.h"

#define BUFF_SIZE 1024
#define THREADS_MAX 124888 // we've limited the maximum number of threads to our os's maximum


typedef struct ThreadInfo
{
	int id;
	char* filename;
	unsigned int* histogram;

	int nBytesHeader;
	int bytesToRead;
	int offset;

	pthread_t thread;
	pthread_mutex_t *lock;

} ThreadInfo;

void* thread_read(void* data)
{
	ThreadInfo* tinfo = (ThreadInfo*)data;
	int nBytesHeader = tinfo->nBytesHeader;
	int nBytesRead = 0;
	int totalBytesRead = 0;
	int offset = tinfo->offset;

	// Compute histogram. This is what you will need to do in threads.
	unsigned char buffer[BUFF_SIZE];
	int fd = open(tinfo->filename, O_RDONLY); // NOTE: CHECK THESE FOR FAILURE N SHIII
	lseek(fd, nBytesHeader + offset, SEEK_SET); // Move the cursor to the start of the data segment
	for (int i = 0; i < tinfo->bytesToRead; i += BUFF_SIZE)
	{
		if (totalBytesRead + BUFF_SIZE > tinfo->bytesToRead)
		{
			nBytesRead = read(fd, buffer, tinfo->bytesToRead - totalBytesRead);
		}
		else
			nBytesRead = read(fd, buffer, BUFF_SIZE);
		if (nBytesRead <= 0)
		{
			printf("Unexpected error: the file ended before all pixels where read.\n");
			_exit(1);
		}
		for (int j = 0; j < nBytesRead; j++)
		{
			pthread_mutex_lock(tinfo->lock);
			tinfo->histogram[buffer[j]]++;
			pthread_mutex_unlock(tinfo->lock);
		}
		totalBytesRead += nBytesRead;
	}
	close(fd);

	return NULL;
}

int main(int argc, char* argv[])
{
	if (argc != 4)
	{
		printf("Incorrect usage. Args needed: ./histogram <pathToImage> <pathToHistogramOut> <nThreadsCreate>\n");
		_exit(1);
	}


	for (int i = 0; argv[3][i]; i++)
	{
		if (!isdigit(argv[3][i]))
		{
			printf("Number of threads must be in range of (1, %d", THREADS_MAX);
			_exit(1);
		}
	}
	int nthreads = atoi(argv[3]);
	if (nthreads < 1 || nthreads > THREADS_MAX)
	{
		printf("Number of threads must be in range of (1, %d", THREADS_MAX);
		_exit(1);
	}

	// Read the header
	int width, height;
	int maxval;

	int nBytesHeader = parse_pgm_header(argv[1], &width, &height, &maxval);
	if (maxval > 255)
	{
		perror("Expecting 1 byte ints\n");
		_exit(1);
	}
	int nPixels = width * height;

	unsigned int* histogram = malloc((maxval + 1) * sizeof(unsigned int));
	if (histogram == NULL)
		_exit(1); // kys
	for (int i = 0; i <= maxval; i++)
		histogram[i] = 0;

	if (nthreads > nPixels) // Set it so at least each thread reads one byte
		nthreads = nPixels;

	pthread_mutex_t lock;
	pthread_mutex_init(&lock, NULL);
	ThreadInfo* t_info = malloc(nthreads * sizeof(ThreadInfo));
	if (t_info == NULL)
		_exit(1); // kys
	for (int i = 0; i < nthreads; i++)
	{
		t_info[i].filename = argv[1];
		t_info[i].id = i;
		t_info[i].nBytesHeader = nBytesHeader;
		t_info[i].bytesToRead = nPixels / nthreads;
		t_info[i].offset = i * t_info[i].bytesToRead;
		if (i == nthreads - 1)
			t_info[i].bytesToRead += nPixels % nthreads;
		t_info[i].histogram = histogram;
		t_info[i].lock = &lock;
		if (pthread_create(&t_info[i].thread, NULL, thread_read, (void *)&t_info[i]) != 0)
			_exit(1); // kys
	}

	for (int i = 0; i < nthreads; i++)
		if (pthread_join(t_info[i].thread, NULL) != 0)
			_exit(1);


	// Write histogram, so it can be loaded in python with np.loadtxt
	int fd_out = open(argv[2], O_WRONLY | O_CREAT, 0644);
	for (int i = 0; i <= maxval; i++)
	{
		char s[80];
		sprintf(s, "%d,%d\n", i, histogram[i]);
		write(fd_out, s, strlen(s));
	}
	close(fd_out);
	pthread_mutex_destroy(&lock);
	free(histogram);
	free(t_info);
}
