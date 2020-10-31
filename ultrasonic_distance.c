#include <stdio.h>
#include <pigpio.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>

#define GPIO_TRIGGER 18
#define GPIO_ECHO 24
#define PULSE_DURATION_US 10
#define NANOS_PER_SEC 1000000000
#define POLL_INTERVAL_US 1000
#define ECHO_TIMEOUT_US 500000
#define SPEED_SOUND_MM_NS_HALF 0.0001715
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

static void normalise_timespec(struct timespec *ts)
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
	normalise_timespec(&until);

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

#define USE_GPIOTRIGGER

#ifdef USE_GPIOTRIGGER

static int pulse(int pin, int duration_us) {
	return gpioTrigger(pin, duration_us, 1);
}

#else /* ndef USE_GPIOTRIGGER */

static int pulse(int pin, int duration_us) {
	struct timespec pulse_duration = { .tv_sec = 0, .tv_nsec = duration_us * 1000 };

	// set pin high
	int ret = gpioWrite(pin, 1);
	if (ret != 0) {
		return ret;
	}

	ret = sleep_for(&pulse_duration);
	if (ret != 0) {
		return ret;
	}

	// set pin low again
	ret = gpioWrite(pin, 0);
	if (ret != 0) {
		return ret;
	}

	return ret;
}
#endif /* ndef USE_GPIOTRIGGER */

struct isr_args {
	int fired;
	int level;
	uint32_t ticks;
};

static void on_edge(int pin, int level, uint32_t tick, void *userdata)
{
	struct isr_args *args = (struct isr_args *) userdata;
	args->fired = 1;
	args->level = level;
	args->ticks = tick;
}

static int wait_for_edge(int pin, unsigned int edge, int timeout_us, struct timespec *elapsed)
{
	struct isr_args args = { .fired = 0 };
	uint32_t start_ticks = gpioTick();
	struct timespec poll_interval = { .tv_sec = 0, .tv_nsec = POLL_INTERVAL_US * 1000 };

	normalise_timespec(&poll_interval);

	int ret = gpioSetISRFuncEx(pin, edge, timeout_us / 1000, on_edge, &args);
	if (ret != 0) {
		return ret;
	}

	for (;;) {

		ret = sleep_for(&poll_interval);
		if (ret < 0) {
			return ret;
		}

		if (args.fired) {
			break;
		}

	}

	if (PI_TIMEOUT != args.level) {
		elapsed->tv_sec = 0;
		elapsed->tv_nsec = (args.ticks - start_ticks) * 1000;
		normalise_timespec(elapsed);
	}

	ret = gpioSetISRFuncEx(pin, edge, 0, NULL, NULL);
	if (ret != 0) {
		return ret;
	}

	return 0;
}

int distance_mm(float *dist)
{
	int ret = pulse(GPIO_TRIGGER, PULSE_DURATION_US);
	if (ret != 0) {
		return ret;
	}

	struct timespec elapsed;
	ret = wait_for_edge(GPIO_ECHO, FALLING_EDGE, ECHO_TIMEOUT_US, &elapsed);
	if (ret != 0) {
		return ret;
	}

	// time difference between start and arrival, in nanoseconds
	int elapsed_ns = elapsed.tv_sec * NANOS_PER_SEC + elapsed.tv_nsec;
	// multiply by the speed of sound (0.000343 metres/nanosecond)
	// and divide by 2, because there and back
	*dist = elapsed_ns * SPEED_SOUND_MM_NS_HALF;

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
		ret = distance_mm(&dist);
		if (ret != 0) {
			fputs("Cannot obtain distance\n", stderr);
			return EXIT_FAILURE;
		}
		fprintf(stderr, "Measured Distance = %.1f mm\n", dist);
		sleep(1);
	}

	return EXIT_SUCCESS;
}
