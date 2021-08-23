/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 * This code is a complete clean re-write of the stress tool by
 * Colin Ian King <colin.king@canonical.com> and attempts to be
 * backwardly compatible with the stress tool by Amos Waterland
 * <apw@rossby.metr.ou.edu> but has more stress tests and more
 * functionality.
 *
 */
#include "stress-ng.h"

static const stress_help_t help[] = {
	{ NULL,	"exit-group N",		"start N workers that exercise exit_group" },
	{ NULL,	"exit-group-ops N",	"stop exit_group workers after N bogo exit_group loops" },
	{ NULL,	NULL,			NULL }
};

#if defined(HAVE_LIB_PTHREAD) && 	\
    defined(__NR_exit_group)

#define STRESS_PTHREAD_EXIT_GROUP_MAX	(16)

/* per pthread data */
typedef struct {
	pthread_t pthread;	/* The pthread */
	int	  ret;		/* pthread create return */
} stress_exit_group_info_t;

static pthread_mutex_t mutex;
static volatile bool keep_running_flag;
static uint64_t pthread_count;
static stress_exit_group_info_t pthreads[MAX_PTHREAD];

static inline void stop_running(void)
{
	keep_running_flag = false;
}

/*
 *  keep_running()
 *  	Check if SIGALRM is pending, set flags
 * 	to tell pthreads and main pthread stressor
 *	to stop. Returns false if we need to stop.
 */
static bool keep_running(void)
{
	if (stress_sigalrm_pending())
		stop_running();
	return keep_running_flag;
}

/*
 *  stress_exit_group_sleep()
 *	tiny delay
 */
static void stress_exit_group_sleep(void)
{
	shim_nanosleep_uint64(10000);
}

/*
 *  stress_exit_group_func()
 *	pthread specific system call stressor
 */
static void *stress_exit_group_func(void *arg)
{
	int ret;
	static void *nowt = NULL;

	(void)arg;

	ret = pthread_mutex_lock(&mutex);
	if (ret == 0) {
		pthread_count++;
		ret = pthread_mutex_unlock(&mutex);
		(void)ret;
	}

	while (keep_running_flag &&
	       (pthread_count < STRESS_PTHREAD_EXIT_GROUP_MAX)) {
		stress_exit_group_sleep();
	}
	shim_exit_group(0);

	/* should never get here */
	return &nowt;
}

/*
 *  stress_exit_group()
 *	stress by creating pthreads
 */
static void NORETURN stress_exit_group_child(const stress_args_t *args)
{
	int ret;
	sigset_t set;
	size_t i, j;

	keep_running_flag = true;

	/*
	 *  Block SIGALRM, instead use sigpending
	 *  in pthread or this process to check if
	 *  SIGALRM has been sent.
	 */
	sigemptyset(&set);
	sigaddset(&set, SIGALRM);
	sigprocmask(SIG_BLOCK, &set, NULL);

	pthread_count = 0;

	(void)memset(&pthreads, 0, sizeof(pthreads));
	ret = pthread_mutex_lock(&mutex);
	if (ret) {
		stop_running();
		shim_exit_group(0);
	}
	for (i = 0; i < STRESS_PTHREAD_EXIT_GROUP_MAX; i++)
		pthreads[i].ret = -1;

	for (i = 0; i < STRESS_PTHREAD_EXIT_GROUP_MAX; i++) {
		pthreads[i].ret = pthread_create(&pthreads[i].pthread, NULL,
			stress_exit_group_func, NULL);
		if (pthreads[i].ret) {
			/* Out of resources, don't try any more */
			if (pthreads[i].ret == EAGAIN)
				break;
			/* Something really unexpected */
			pr_fail("%s: pthread_create failed, errno=%d (%s)\n",
				args->name, pthreads[i].ret, strerror(pthreads[i].ret));
			stop_running();
			shim_exit_group(0);
		}
		if (!(keep_running() && keep_stressing(args)))
			break;
	}
	ret = pthread_mutex_unlock(&mutex);
	if (ret) {
		stop_running();
		shim_exit_group(0);
	}
	/*
	 *  Wait until they are all started or
	 *  we get bored waiting..
	 */
	for (j = 0; j < 1000; j++) {
		bool all_running = false;

		if (!keep_stressing(args)) {
			stop_running();
			shim_exit_group(0);
			break;
		}

		all_running = (pthread_count == i);
		if (all_running)
			break;

		stress_exit_group_sleep();
	}
	shim_exit_group(0);
	/* Should never get here */
	_exit(0);
}

/*
 *  stress_exit_group()
 *	stress by creating pthreads
 */
static int stress_exit_group(const stress_args_t *args)
{
        stress_set_proc_state(args->name, STRESS_STATE_RUN);

	while (keep_stressing(args)) {
		pid_t pid;
		int ret;

		ret = pthread_mutex_init(&mutex, NULL);
		if (ret) {
			pr_fail("%s: pthread_mutex_init failed, errno=%d (%s)\n",
				args->name, ret, strerror(ret));
			return EXIT_FAILURE;
		}

		pid = fork();
		if (pid < 0) {
			continue;
		} else if (pid == 0) {
			stress_exit_group_child(args);
		} else {
			int status, ret;

			ret = waitpid(pid, &status, 0);
			if (ret < 0)
				break;
		}

		(void)pthread_mutex_destroy(&mutex);
		inc_counter(args);
	}

        stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	return EXIT_SUCCESS;
}


stressor_info_t stress_exit_group_info = {
	.stressor = stress_exit_group,
	.class = CLASS_SCHEDULER | CLASS_OS,
	.help = help
};
#else
stressor_info_t stress_exit_group_info = {
	.stressor = stress_not_implemented,
	.class = CLASS_SCHEDULER | CLASS_OS,
	.help = help
};
#endif
