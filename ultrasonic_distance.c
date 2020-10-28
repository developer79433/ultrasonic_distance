#include <stdio.h>
#include <pigpio.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>

#define GPIO_TRIGGER 18
#define GPIO_ECHO 24
#define PULSE_DURATION_US 10
#define NANOS_PER_SEC 1000000000
#ifdef CLOCK_MONOTONIC_RAW
#define CLOCK CLOCK_MONOTONIC
#endif

static int setup(int in_pin, int out_pin)
{
	int ret = 0;

	// set GPIO direction (IN / OUT)
	ret = gpioSetMode(in_pin, PI_INPUT);
	if (ret != 0) {
		return ret;
	}
	ret = gpioSetMode(out_pin, PI_OUTPUT);
	if (ret != 0) {
		return ret;
	}

	return ret;
}

static void normalise_tv(struct timespec *ts)
{
	if (ts->tv_nsec >= NANOS_PER_SEC) {
		ts->tv_sec += ts->tv_nsec / NANOS_PER_SEC;
		ts->tv_nsec = ts->tv_nsec % NANOS_PER_SEC;
	}
}

static int sleep_for(const struct timespec *ts)
{
	struct timespec until;
	int ret = clock_gettime(CLOCK, &until);
	if (ret != 0) {
		return ret;
	}

	until.tv_sec += ts->tv_sec;
	until.tv_nsec += ts->tv_nsec;
	normalise_tv(&until);

	for (;;) {
		ret = clock_nanosleep(CLOCK, TIMER_ABSTIME, &until, 0);
		if (0 == ret) {
			break; // Successfully slept
		} else if (EINTR == ret) {
			continue; // Interrupted - resume sleep
		} else {
			ret = errno;
			perror("nanosleep");
			break;
		}
	}

	return ret;
}

static int pulse(int duration_us) {
	// TODO: Use gpioTrigger
	int ret;
	struct timespec pulse_duration = { .tv_sec = 0, .tv_nsec = duration_us * 1000 };

	// set Trigger to HIGH
	ret = gpioWrite(GPIO_TRIGGER, 1);
	if (ret != 0) {
		return ret;
	}

	// set Trigger after 0.01ms to LOW
	ret = sleep_for(&pulse_duration);
	if (ret != 0) {
		return ret;
	}

	ret = gpioWrite(GPIO_TRIGGER, 0);
	if (ret != 0) {
		return ret;
	}

	return ret;
}

int distance(float *dist)
{
	int ret = pulse(PULSE_DURATION_US);
	if (ret != 0) {
		return ret;
	}

	// TODO: Use gpioSetAlertFunc or gpioSetISRFunc
	// The below is a polling loop, pegging a core. This is naive.
	// An interrupt may be more power-efficient while still yielding
	// adequate performance.

	struct timespec start_time, stop_time;
	// save StartTime
	for(;;) {
		int ret = gpioRead(GPIO_ECHO);
		if (PI_BAD_GPIO == ret) {
			return ret;
		}
		if (0 != ret) {
			ret = clock_gettime(CLOCK, &start_time);
			if (ret != 0) {
				return ret;
			}
			break;
		}
	}

	// save time of arrival
	for(;;) {
		int ret = gpioRead(GPIO_ECHO);
		if (PI_BAD_GPIO == ret) {
			return ret;
		}
		if (1 != ret) {
			ret = clock_gettime(CLOCK, &stop_time);
			if (ret != 0) {
				return ret;
			}
			break;
		}
	}

	// time difference between start and arrival, in nanoseconds
	double elapsed = (stop_time.tv_sec - start_time.tv_sec) * NANOS_PER_SEC + (stop_time.tv_nsec - start_time.tv_nsec);
	// multiply by the speed of sound (0.000343 metres/nanosecond)
	// and divide by 2, because there and back
	*dist = elapsed * 0.0001715;

	return 0;
}

int main(int argc, char **argv)
{
	int ret = gpioInitialise();
	if (PI_INIT_FAILED == ret) {
		fputs("Cannot initialise pigpio library", stderr);
		return EXIT_FAILURE;
	}

	ret = atexit(gpioTerminate);
	if (ret != 0) {
		fputs("Cannot install atexit function", stderr);
		return EXIT_FAILURE;
	}

	ret = setup(GPIO_ECHO, GPIO_TRIGGER);
	if (ret != 0) {
		fputs("Cannot configure GPIO pins", stderr);
		return EXIT_FAILURE;
	}

	for(;;) {
		float dist;
		ret = distance(&dist);
		if (ret != 0) {
			fputs("Cannot obtain distance\n", stderr);
			return EXIT_FAILURE;
		}
		fprintf(stderr, "Measured Distance = %.1f cm\n", dist);
		sleep(1);
	}

	return EXIT_SUCCESS;
}
