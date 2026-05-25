#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <unistd.h>

#define NV_SCHEDULER_INIT 455

static int parse_int_arg(const char *value, int *out)
{
	char *end = NULL;
	long parsed;

	errno = 0;
	parsed = strtol(value, &end, 10);
	if (errno != 0 || end == value || *end != '\0' || parsed < INT_MIN || parsed > INT_MAX)
		return -1;

	*out = (int)parsed;
	return 0;
}

static void print_usage(const char *prog)
{
	fprintf(stderr,
			"usage: %s <slack_weight> <streak_limit>\n",
			prog);
}

int main(int argc, char **argv)
{
	int slack_weight = 1;
	int streak_limit = 15;

	if (argc != 1 && argc != 3) {
		print_usage(argv[0]);
		return 1;
	}

	if (argc == 3) {
		if (parse_int_arg(argv[1], &slack_weight) != 0 ||
			parse_int_arg(argv[2], &streak_limit) != 0) {
			fprintf(stderr, "[ERROR] invalid scheduler arguments\n");
			print_usage(argv[0]);
			return 1;
		}
	}

	if (syscall(NV_SCHEDULER_INIT, slack_weight, 0, streak_limit) < 0) {
		fprintf(stderr, "[ERROR] nv_scheduler_init failed: %s\n", strerror(errno));
		return 1;
	}

	fprintf(stderr,
			"[INFO] nv_scheduler_init succeeded: slack=%d streak=%d\n",
			slack_weight,
			streak_limit);
	return 0;
}
