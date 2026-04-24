/* Enable/disable the OV2640 sensor's built-in color-bar test pattern.
 *
 * Usage:  ./set-test-pattern [DEV] [VALUE]
 *         DEV    default /dev/v4l-subdev0
 *         VALUE  0 = off (normal image), 1 = color bars
 *
 * Bypasses the imaging pipeline so you can verify the capture/conversion
 * path works independently of sensor exposure/gain/lighting.
 */

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <sys/ioctl.h>
#include <linux/videodev2.h>

int main(int argc, char **argv) {
	const char *dev = argc > 1 ? argv[1] : "/dev/v4l-subdev0";
	int value       = argc > 2 ? atoi(argv[2]) : 1;

	int fd = open(dev, O_RDWR);
	if (fd < 0) { perror(dev); return 1; }

	struct v4l2_control ctrl = {
		.id    = V4L2_CID_TEST_PATTERN,
		.value = value,
	};
	if (ioctl(fd, VIDIOC_S_CTRL, &ctrl) < 0) {
		perror("VIDIOC_S_CTRL");
		return 1;
	}

	memset(&ctrl, 0, sizeof ctrl);
	ctrl.id = V4L2_CID_TEST_PATTERN;
	if (ioctl(fd, VIDIOC_G_CTRL, &ctrl) < 0) {
		perror("VIDIOC_G_CTRL");
		return 1;
	}

	printf("%s test_pattern = %d (%s)\n",
	       dev, ctrl.value, ctrl.value ? "ON — color bars" : "OFF — normal image");
	return 0;
}
