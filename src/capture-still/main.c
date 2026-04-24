/* Capture one frame from sun4i-csi in YUV420M (multi-planar Y+Cb+Cr)
 * and write it as concatenated I420 raw data to disk.
 *
 * Convert on host side with:
 *   ffmpeg -f rawvideo -pixel_format yuv420p -video_size 640x480 \
 *          -i frame.yuv out.png
 */

#include <errno.h>
#include <fcntl.h>
#include <linux/videodev2.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#define DEVICE  "/dev/video0"
#define WIDTH   640
#define HEIGHT  480
#define NBUF    4
#define NPLANES 3

static void die(const char *msg) { perror(msg); exit(1); }

static int xioctl(int fd, unsigned long req, void *arg) {
	int r;
	do { r = ioctl(fd, req, arg); } while (r < 0 && errno == EINTR);
	return r;
}

int main(int argc, char **argv) {
	const char *out_path = (argc > 1) ? argv[1] : "frame.yuv";

	int fd = open(DEVICE, O_RDWR);
	if (fd < 0) die("open " DEVICE);

	/* Set multi-planar YUV420M format */
	struct v4l2_format fmt = {
		.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE,
		.fmt.pix_mp = {
			.width       = WIDTH,
			.height      = HEIGHT,
			.pixelformat = V4L2_PIX_FMT_YUV420M,
			.field       = V4L2_FIELD_NONE,
			.num_planes  = NPLANES,
		},
	};
	if (xioctl(fd, VIDIOC_S_FMT, &fmt) < 0) die("VIDIOC_S_FMT");
	fprintf(stderr, "Format set: %ux%u YUV420M %u planes\n",
		fmt.fmt.pix_mp.width, fmt.fmt.pix_mp.height,
		fmt.fmt.pix_mp.num_planes);

	/* Request buffers */
	struct v4l2_requestbuffers req = {
		.count  = NBUF,
		.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE,
		.memory = V4L2_MEMORY_MMAP,
	};
	if (xioctl(fd, VIDIOC_REQBUFS, &req) < 0) die("VIDIOC_REQBUFS");
	fprintf(stderr, "Got %u buffers\n", req.count);

	/* Query + mmap each plane of each buffer, then queue */
	struct { void *start[NPLANES]; size_t length[NPLANES]; } bufs[NBUF];

	for (unsigned i = 0; i < req.count; i++) {
		struct v4l2_plane planes[NPLANES] = { 0 };
		struct v4l2_buffer buf = {
			.type     = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE,
			.memory   = V4L2_MEMORY_MMAP,
			.index    = i,
			.length   = NPLANES,
			.m.planes = planes,
		};
		if (xioctl(fd, VIDIOC_QUERYBUF, &buf) < 0) die("VIDIOC_QUERYBUF");
		for (unsigned p = 0; p < NPLANES; p++) {
			bufs[i].length[p] = planes[p].length;
			bufs[i].start[p]  = mmap(NULL, planes[p].length,
						 PROT_READ | PROT_WRITE, MAP_SHARED,
						 fd, planes[p].m.mem_offset);
			if (bufs[i].start[p] == MAP_FAILED) die("mmap");
		}
		if (xioctl(fd, VIDIOC_QBUF, &buf) < 0) die("VIDIOC_QBUF");
	}

	/* Stream on */
	enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	if (xioctl(fd, VIDIOC_STREAMON, &type) < 0) die("VIDIOC_STREAMON");
	fprintf(stderr, "Streaming...\n");

	/* Dequeue one frame */
	struct v4l2_plane planes[NPLANES] = { 0 };
	struct v4l2_buffer buf = {
		.type     = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE,
		.memory   = V4L2_MEMORY_MMAP,
		.length   = NPLANES,
		.m.planes = planes,
	};
	if (xioctl(fd, VIDIOC_DQBUF, &buf) < 0) die("VIDIOC_DQBUF");
	fprintf(stderr, "Got buffer %u: Y=%u Cb=%u Cr=%u bytes\n",
		buf.index, planes[0].bytesused,
		planes[1].bytesused, planes[2].bytesused);

	/* Write planes as concatenated I420 */
	int out = open(out_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (out < 0) die("open output");
	for (unsigned p = 0; p < NPLANES; p++) {
		size_t n = planes[p].bytesused;
		if (write(out, bufs[buf.index].start[p], n) != (ssize_t)n)
			die("write");
	}
	close(out);

	printf("Wrote %ux%u YUV420 frame to %s (%u+%u+%u bytes)\n",
	       WIDTH, HEIGHT, out_path,
	       planes[0].bytesused, planes[1].bytesused, planes[2].bytesused);
	printf("Convert: ffmpeg -f rawvideo -pixel_format yuv420p "
	       "-video_size %ux%u -i %s out.png\n",
	       WIDTH, HEIGHT, out_path);

	xioctl(fd, VIDIOC_STREAMOFF, &type);
	for (unsigned i = 0; i < req.count; i++)
		for (unsigned p = 0; p < NPLANES; p++)
			munmap(bufs[i].start[p], bufs[i].length[p]);
	close(fd);
	return 0;
}
