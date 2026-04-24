/* Set a V4L2 subdev's pad 0 output format directly via ioctl.
 *
 * Workaround for sun4i-csi not propagating VIDIOC_S_FMT from the video
 * node down to the connected source subdev (OV2640). Without matching
 * formats on both ends of the media link, VIDIOC_STREAMON fails with
 * -EPIPE because v4l2_subdev_link_validate rejects the pipeline.
 *
 * Usage:
 *   ./set-subdev-fmt [DEV] [WIDTH] [HEIGHT] [MBUS_CODE]
 *   Defaults: /dev/v4l-subdev0 640 480 0x2006 (YUYV8_2X8)
 *
 * Common codes (from linux/media-bus-format.h):
 *   0x2006 = MEDIA_BUS_FMT_YUYV8_2X8
 *   0x2008 = MEDIA_BUS_FMT_UYVY8_2X8
 */

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <sys/ioctl.h>
#include <linux/v4l2-subdev.h>
#include <linux/media-bus-format.h>

int main(int argc, char **argv) {
	const char *dev   = argc > 1 ? argv[1] : "/dev/v4l-subdev0";
	unsigned    w     = argc > 2 ? (unsigned)atoi(argv[2]) : 640;
	unsigned    h     = argc > 3 ? (unsigned)atoi(argv[3]) : 480;
	unsigned    code  = argc > 4 ? (unsigned)strtoul(argv[4], NULL, 0)
	                              : MEDIA_BUS_FMT_YUYV8_2X8;

	int fd = open(dev, O_RDWR);
	if (fd < 0) { perror(dev); return 1; }

	struct v4l2_subdev_format fmt = {
		.pad   = 0,
		.which = V4L2_SUBDEV_FORMAT_ACTIVE,
		.format = {
			.width      = w,
			.height     = h,
			.code       = code,
			.field      = V4L2_FIELD_NONE,
			.colorspace = V4L2_COLORSPACE_SRGB,
		},
	};
	if (ioctl(fd, VIDIOC_SUBDEV_S_FMT, &fmt) < 0) {
		perror("VIDIOC_SUBDEV_S_FMT");
		return 1;
	}

	memset(&fmt, 0, sizeof fmt);
	fmt.pad   = 0;
	fmt.which = V4L2_SUBDEV_FORMAT_ACTIVE;
	if (ioctl(fd, VIDIOC_SUBDEV_G_FMT, &fmt) < 0) {
		perror("VIDIOC_SUBDEV_G_FMT");
		return 1;
	}

	printf("%s pad 0: code=0x%04x %ux%u field=%u\n",
	       dev, fmt.format.code, fmt.format.width, fmt.format.height,
	       fmt.format.field);
	return 0;
}
