#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <linux/input.h>

int main(void) {
	int fd = open("/dev/input/event0", O_RDONLY);
	if (fd < 0) { perror("open /dev/input/event0"); return 1; }

	struct input_event ev;
	printf("Watching buttons. Ctrl+C to quit.\n");
	while (read(fd, &ev, sizeof(ev)) == sizeof(ev)) {
		if (ev.type != EV_KEY) continue;
		const char *name =
			ev.code == KEY_ENTER ? "ENTER (BTN_1)" :
			ev.code == KEY_ESC   ? "ESC (BTN_2)"   : "?";
		printf("%-15s %s\n", name, ev.value ? "pressed" : "released");
		fflush(stdout);
	}
	return 0;
}
