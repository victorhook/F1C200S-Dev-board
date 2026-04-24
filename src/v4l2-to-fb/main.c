#include <errno.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <linux/videodev2.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#define CAM_DEV "/dev/video0"
#define FB_DEV  "/dev/fb0"
#define WIDTH   160
#define HEIGHT  120
#define NBUF    4

static volatile sig_atomic_t stop;
static void on_sigint(int sig) { (void)sig; stop = 1; }

static void die(const char *msg) { perror(msg); exit(1); }

static int xioctl(int fd, unsigned long req, void *arg) {
	int r;
	do { r = ioctl(fd, req, arg); } while (r < 0 && errno == EINTR);
	return r;
}

int main(void) {
	signal(SIGINT, on_sigint);

	int cam = open(CAM_DEV, O_RDWR);
	if (cam < 0) die("open " CAM_DEV);

	struct v4l2_format fmt = {
		.type = V4L2_BUF_TYPE_VIDEO_CAPTURE,
		.fmt.pix = {
			.width       = WIDTH,
			.height      = HEIGHT,
			.pixelformat = V4L2_PIX_FMT_RGB565,
			.field       = V4L2_FIELD_NONE,
		},
	};
	if (xioctl(cam, VIDIOC_S_FMT, &fmt) < 0) die("VIDIOC_S_FMT");

	struct v4l2_requestbuffers req = {
		.count  = NBUF,
		.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE,
		.memory = V4L2_MEMORY_MMAP,
	};
	if (xioctl(cam, VIDIOC_REQBUFS, &req) < 0) die("VIDIOC_REQBUFS");

	struct { void *start; size_t length; } bufs[NBUF];

	for (unsigned i = 0; i < req.count; i++) {
		struct v4l2_buffer buf = {
			.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE,
			.memory = V4L2_MEMORY_MMAP,
			.index  = i,
		};
		if (xioctl(cam, VIDIOC_QUERYBUF, &buf) < 0) die("VIDIOC_QUERYBUF");
		bufs[i].length = buf.length;
		bufs[i].start = mmap(NULL, buf.length, PROT_READ | PROT_WRITE,
				     MAP_SHARED, cam, buf.m.offset);
		if (bufs[i].start == MAP_FAILED) die("mmap cam");
		if (xioctl(cam, VIDIOC_QBUF, &buf) < 0) die("VIDIOC_QBUF");
	}

	int fb = open(FB_DEV, O_RDWR);
	if (fb < 0) die("open " FB_DEV);

	struct fb_var_screeninfo vinfo;
	struct fb_fix_screeninfo finfo;
	if (xioctl(fb, FBIOGET_VSCREENINFO, &vinfo) < 0) die("FBIOGET_VSCREENINFO");
	if (xioctl(fb, FBIOGET_FSCREENINFO, &finfo) < 0) die("FBIOGET_FSCREENINFO");

	size_t fb_len = finfo.smem_len;
	uint8_t *fb_mem = mmap(NULL, fb_len, PROT_READ | PROT_WRITE, MAP_SHARED, fb, 0);
	if (fb_mem == MAP_FAILED) die("mmap fb");

	printf("Streaming %dx%d %s → %s (fb %ux%u @ %ubpp, stride %u)\n",
	       WIDTH, HEIGHT, CAM_DEV, FB_DEV,
	       vinfo.xres, vinfo.yres, vinfo.bits_per_pixel, finfo.line_length);

	enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	if (xioctl(cam, VIDIOC_STREAMON, &type) < 0) die("VIDIOC_STREAMON");

	size_t cam_stride = WIDTH * 2;
	size_t rows  = (HEIGHT < vinfo.yres) ? HEIGHT : vinfo.yres;
	size_t bytes = (cam_stride < finfo.line_length) ? cam_stride : finfo.line_length;

	while (!stop) {
		struct v4l2_buffer buf = {
			.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE,
			.memory = V4L2_MEMORY_MMAP,
		};
		/* Direct ioctl here (not xioctl) so SIGINT can break out —
		 * xioctl's EINTR retry would swallow the signal. */
		if (ioctl(cam, VIDIOC_DQBUF, &buf) < 0) {
			if (errno == EINTR) break;
			die("VIDIOC_DQBUF");
		}

		uint8_t *src = bufs[buf.index].start;
		uint8_t *dst = fb_mem;
		for (size_t y = 0; y < rows; y++) {
			memcpy(dst, src, bytes);
			src += cam_stride;
			dst += finfo.line_length;
		}

		if (xioctl(cam, VIDIOC_QBUF, &buf) < 0) die("VIDIOC_QBUF");
	}

	xioctl(cam, VIDIOC_STREAMOFF, &type);
	for (unsigned i = 0; i < req.count; i++)
		munmap(bufs[i].start, bufs[i].length);
	munmap(fb_mem, fb_len);
	close(fb);
	close(cam);
	return 0;
}
