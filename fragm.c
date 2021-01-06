#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/sysinfo.h>
#include <sys/types.h>
#include <unistd.h>

#define MAX_ORDER	11
#define PAGE_SIZE	4096
#define SZ_GB		(1UL << 30)

#define max(a,b)             \
({                           \
    __typeof__ (a) _a = (a); \
    __typeof__ (b) _b = (b); \
    _a > _b ? _a : _b;       \
})

#define min(a,b)             \
({                           \
    __typeof__ (a) _a = (a); \
    __typeof__ (b) _b = (b); \
    _a < _b ? _a : _b;       \
})

static int print_buddyinfo(void)
{
	char buf[4 * PAGE_SIZE] = {0};
	int ret, off, fd, i;
	ssize_t len;
	unsigned long nr[MAX_ORDER] = {0};
	unsigned long total = 0, cumulative = 0;

	fd = open("/proc/buddyinfo", O_RDONLY);
	if (fd < 0) {
		perror("open");
		return -1;
	}

	len = read(fd, buf, sizeof(buf));
	if (len <= 0) {
		perror("read");
		close(fd);
		return -1;
	}

	off = 0;
	while (off < len) {
		int node;
		char __node[64], __zone[64], zone[64];
		unsigned long n[MAX_ORDER];
		int parsed;

		ret = sscanf(buf + off, "%s %d, %s %s %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu\n%n",
			     __node, &node, __zone, zone, &n[0], &n[1], &n[2], &n[3], &n[4], &n[5], &n[6],
			     &n[7], &n[8], &n[9], &n[10], &parsed);
		//printf("%d %s %lu %lu %lu\n", node, zone, n[0], n[1], n[10]);
		if (ret < 15)
			break;

		off += parsed;

		for (i = 0; i < MAX_ORDER; i++)
			nr[i] += n[i];
	}

	for (i = 0; i < MAX_ORDER; i++)
		total += (PAGE_SIZE << i) * nr[i];

	printf("%-4s%10s%10s%10s%10s\n", "Order", "Pages", "Total", "%Free", "%Higher");
	for (i = 0; i < MAX_ORDER; i++) {
		unsigned long bytes = (PAGE_SIZE << i) * nr[i];

		cumulative += bytes;

		printf("%-4d %10lu %7.2lfGB %8.1lf%% %8.1lf%%\n", i, nr[i],
		       (double) bytes / SZ_GB,
		       (double) bytes / total * 100,
		       (double) (total - cumulative) / total * 100);
	}

	close(fd);
	return 0;
}

static int fragment_memory(int order, int dentries)
{
	struct sysinfo info;
	size_t off = 0, size, mmap_size, i;
	char *area;
	int ret, iter = 0;

	ret = sysinfo(&info);
	if (ret) {
		perror("sysinfo");
		return -1;
	}

	print_buddyinfo();

	printf("total %.1lf GB, free %.1lf GB\n", (double)info.totalram / SZ_GB,
	       (double)info.freeram / SZ_GB);

	mmap_size = info.totalram;

	area = mmap(NULL, mmap_size, PROT_WRITE | PROT_READ,
		    MAP_ANONYMOUS | MAP_PRIVATE, 0, 0);
	if (area == MAP_FAILED) {
		perror("mmap");
		return -1;
	}

	off = 0;
	while (1) {
		ret = sysinfo(&info);
		if (ret) {
			perror("sysinfo");
			break;
		}

		size = info.freeram;
		printf("%.1lf GB..\n", (double)size / SZ_GB);

		/* Populate region */
		for (i = off; i < min(off + size, mmap_size); i += PAGE_SIZE)
			area[i] = 'f';

		/* Make holes */
		for (i = off;
		     i < min(off + size, mmap_size - (PAGE_SIZE << order) - PAGE_SIZE);
		     i += (PAGE_SIZE << order)) {
			ret = madvise(area + i + PAGE_SIZE,
				      (PAGE_SIZE << order) - PAGE_SIZE,
				      MADV_DONTNEED);
			if (ret) {
				perror("madvise");
				return -1;
			}
		}

		if (dentries) {
			unsigned long count;

			ret = sysinfo(&info);
			if (ret) {
				perror("sysinfo");
				break;
			}

			count = info.freeram *
				(PAGE_SIZE << (order - PAGE_SIZE)) /
				(PAGE_SIZE << order);

			/* struct dentry is ~192 bytes large*/
			count /= 192;

			printf("creating %lu dentries (%.1lfGB)\n", count,
			       (double)count * 192 / SZ_GB);
			for (i = 0; i < count; i++) {
				struct stat st;
				char buf[32];

				snprintf(buf, sizeof(buf), "/%ld%d%lu",
					 info.uptime, iter, i);
				stat(buf, &st);
			}
		}

		off += size;
		if (off >= mmap_size)
			break;

		iter++;
	}

	printf("done\n");
	print_buddyinfo();

	if (!dentries) {
		while(1) {
			sleep(10);
			printf("\n");
			print_buddyinfo();
		}
	}

	munmap(area, mmap_size);

	return 0;
}

int main(int argc, char **argv)
{
	int dentries = 0, order, i;

	if (argc < 2)
		goto bad_args;

	if (strcmp(argv[1], "stat") == 0) {
		print_buddyinfo();
		return EXIT_SUCCESS;
	} else if (strcmp(argv[1], "fragment") == 0) {
		for (i = 2; i < argc - 1; i++) {
			if (strcmp(argv[i], "--dentries") == 0)
				dentries = 1;
		}

		order = atoi(argv[argc - 1]);
		if (order < 1 || order > 18) {
			fprintf(stderr, "Order must be in [1; 18] range\n");
			return EXIT_FAILURE;
		}

		fragment_memory(order, dentries);
	} else {
		goto bad_args;
	}


	return EXIT_SUCCESS;

bad_args:
	fprintf(stderr,
		"Usage:\n"
		"    Fragment memory: fragment [--dentries] <order>\n"
		"    Show stats:      stat\n");
	return EXIT_FAILURE;
}
