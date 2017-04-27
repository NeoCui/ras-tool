/*
 * Copyright (C) 2015 Intel Corporation
 * Author: Tony Luck
 *
 * This software may be redistributed and/or modified under the terms of
 * the GNU General Public License ("GPL") version 2 only as published by the
 * Free Software Foundation.
 */

/*
 * Allocate memory - use EINJ to inject a bunch of soft errors,
 * then consume them all as fast a possible.
 */
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>

#define EINJ_ETYPE "/sys/kernel/debug/apei/einj/error_type"
#define EINJ_ADDR "/sys/kernel/debug/apei/einj/param1"
#define EINJ_MASK "/sys/kernel/debug/apei/einj/param2"
#define EINJ_NOTRIGGER "/sys/kernel/debug/apei/einj/notrigger"
#define EINJ_DOIT "/sys/kernel/debug/apei/einj/error_inject"

volatile int trigger;

static void wfile(char *file, unsigned long val)
{
	FILE *fp;
	static int total_errors;

tryagain:
	fp = fopen(file, "w");
	if (fp == NULL) {
		perror(file);
		exit(1);
	}
	fprintf(fp, "0x%lx\n", val);
	if (fclose(fp) == EOF) {
		perror(file);
		if (++total_errors == 10)
			exit(1);
		sleep(3);
		goto tryagain;
	}
}

static void check_configuration(void)
{
	char	model[512];

	pagesize = getpagesize();

	if (getuid() != 0) {
		fprintf(stderr, "%s: must be root to run error injection tests\n", progname);
		exit(1);
	}
	if (access("/sys/firmware/acpi/tables/EINJ", R_OK) == -1) {
		fprintf(stderr, "%s: Error injection not supported, check your BIOS settings\n", progname);
		exit(1);
	}
	if (access(EINJ_NOTRIGGER, R_OK|W_OK) == -1) {
		fprintf(stderr, "%s: Is the einj.ko module loaded?\n", progname);
		exit(1);
	}
	model[0] = '\0';
	proc_cpuinfo(&nsockets, &ncpus, model);
	if (nsockets == 0 || ncpus == 0) {
		fprintf(stderr, "%s: could not find number of sockets/cpus\n", progname);
		exit(1);
	}
	if (ncpus % nsockets) {
		fprintf(stderr, "%s: strange topology. Are all cpus online?\n", progname);
		exit(1);
	}
	lcpus_persocket = ncpus / nsockets;
	if (!force_flag && strstr(model, "Gold") == NULL) {
		fprintf(stderr, "%s: warning: cpu may not support recovery\n", progname);
		exit(1);
	}
}

static void inject(int nerrors, double interval)
{
	char	*buf;
	long long paddr;
	extern long long vtop(char *);
	int	i;
	unsigned long s, e;
	int	bufsz = 4096;

	buf = malloc(bufsz);
	if (buf == NULL) {
		perror("malloc");
		exit(1);
	}
	memset(buf, '*', bufsz);

	paddr = vtop(buf);
	printf("vaddr = %p paddr = %llx\n", buf, paddr);
	wfile(EINJ_ADDR, paddr);

	for (i = 0; i < nerrors; i++) {
		printf("Inject times: %d\n", i);
		wfile(EINJ_DOIT, 1);

		/* wait a bit to make sure SMI is all done on all cpus */
		usleep((int)(interval * 1.0e6));
	}


	/* Trigger error by reading from target location */
	for (i = 0; i < bufsz; i++)
		trigger += *(buf + i);

	/* wait a bit to allow CMCI handlers to complete */
	usleep((int)(interval * 1.0e6));
}

int main(int argc, char **argv)
{
	check_configuration();
	int nerrors = (argc > 1) ? atoi(argv[1]) : 20;
	double interval = (argc > 2) ? atof(argv[2]) : 1.0;

	wfile(EINJ_ETYPE, 0x8);
	wfile(EINJ_MASK, ~0x0ul);
	wfile(EINJ_NOTRIGGER, 1);

	inject(nerrors, interval);

	return 0;
}
