#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <linux/input.h>

int main(void) {

	printf("Led blinker started!\n");
	int fd = open("/sys/class/leds/devboard:led2/brightness", O_WRONLY);

	while (1) {
		write(fd, "1", 1);
	}

	close(fd);

	return 0;
}
