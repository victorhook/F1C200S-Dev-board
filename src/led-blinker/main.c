#include <fcntl.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>

#define LED "/sys/class/leds/devboard:led2/brightness"

int main(void) {
	printf("Led blinker started!\n");
	int fd = open(LED, O_WRONLY);

	if (fd < 0) {
		perror("Failed to open LED " LED);
		return 1;
	}

	struct timespec next;
	clock_gettime(CLOCK_MONOTONIC, &next);

	int on = 1;
	while (1) {
		if (write(fd, on ? "1" : "0", 1) != 1) {
			perror("Error when writing LED" LED);
			return 1;
		}

		on = !on;

		next.tv_sec += 1;
		clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next, NULL);
	}

	return 0;
}
