// Aurora - version 1.0

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <termios.h>
#include <time.h>
#include <errno.h>
#include <unistd.h>
#include <math.h>
#include <signal.h>
#include <assert.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <linux/videodev2.h>

#ifndef CLEAR
#define CLEAR(x) memset (&(x), 0, sizeof(x))
#endif

#ifndef N_LEDS
#define N_LEDS 50
#endif

#ifndef WIDTH
#define WIDTH 720 // 720 - 20
#endif

#ifndef HEIGHT
#define HEIGHT 480 // 480 - 10
#endif

static const char *input_dev = "/dev/video1", *output_dev = "/dev/ttyUSB0", 
	*value_curve_file = "curve_in_progress_4_value", *red_curve_file = "curve_red",
	*green_curve_file = "curve_green", *blue_curve_file = "curve_blue";
static int input_fd = -1, output_fd = -1, frame = 0, h_offset = 10,
	led_xdim, led_ydim, led_dim, numPixels, fade = 75,
		i, j, k, r, g, b;
static unsigned int n_buffers = 0;
static unsigned char gamma_table[256][3],
	led_buffer[6 + (N_LEDS * 3)],
		led_prev[N_LEDS * 3],
		// img_buffer[WIDTH * HEIGHT * 3],
			rgb[3] = {0, 0, 0};
static float value[256], red[256], green[256], blue[256], curved;
static time_t t, start, prev;
static struct v4l2_format fmt;
static struct termios tty;
struct buffer {
	void *start;
	size_t length;
};
static struct buffer *buffers = NULL;

// 16 LEDs across, 9 down
static int display[2] = {18, 11};

// If first index is 0, pixel is along a row;
// if first index is 1, pixel is along a column;
static int leds[N_LEDS][3] = {
	// {16, 9}
	{0, 4, 10}, {0, 3, 10}, {0, 2, 10}, {0, 1, 10}, {0, 0, 10}, 
		{1, 0, 10}, {1, 0, 9}, {1, 0, 8}, {1, 0, 7}, {1, 0, 6}, 
			{1, 0, 5}, {1, 0, 4}, {1, 0, 3}, {1, 0, 2}, {1, 0, 1}, 
				{1, 0, 0}, {0, 0, 0}, {0, 1, 0}, {0, 2, 0}, {0, 3, 0}, 
					{0, 4, 0}, {0, 5, 0}, {0, 6, 0}, {0, 7, 0}, {0, 8, 0}, 
						{0, 9, 0}, {0, 10, 0}, {0, 11, 0}, {0, 12, 0}, {0, 13, 0}, 
							{0, 14, 0}, {0, 15, 0}, {0, 16, 0}, {0, 17, 0}, {1, 17, 0}, 
								{1, 17, 1}, {1, 17, 2}, {1, 17, 3}, {1, 17, 4}, {1, 17, 5}, 
									{1, 17, 6}, {1, 17, 7}, {1, 17, 8}, {1, 17, 9}, {1, 17, 10}, 
										{0, 17, 10}, {0, 16, 10}, {0, 15, 10}, {0, 14, 10}, {0, 13, 10}, 
};

static void setPixels(int pixels[][led_dim * led_dim]) {
	// Initialize bounds of the region
	int xmin, ymin, xmax, ymax, led_short;
	
	// Scale the shortest dimension appropriately 
	if (led_dim == led_xdim) {
		led_short = HEIGHT / (display[1] + 1);
	}
	else {
		led_short = WIDTH / (display[0] + 1);
	}
	
	// Iterate for all LEDs
	///// THERE HAS TO BE A BETTER WAY OF DOING THIS!
	for (i = 0; i < N_LEDS; i++) {
		// If we are in a row and column-spacing is shorter...
		if (leds[i][0] == 0 && led_dim == led_ydim) {
			// fprintf(stdout, "\nLED %d: special case 1\n", i);
			xmin = (leds[i][1] + 1) * led_short - led_dim / 2;
			ymin = leds[i][2] * led_dim;
			xmax = (leds[i][1] + 1) * led_short + led_dim / 2;
			ymax = (leds[i][2] + 1) * led_dim;
		}
		// If we are in a column and row-spacing is shorter...
		else if (leds[i][0] == 1 && led_dim == led_xdim) {
			// fprintf(stdout, "\nLED %d: special case 2 (led_short = %d)\n", i, led_short);
			xmin = leds[i][1] * led_dim;
			ymin = (leds[i][2] + 1) * led_short - led_dim / 2;
			xmax = (leds[i][1] + 1) * led_dim;
			ymax = (leds[i][2] + 1) * led_short + led_dim / 2;
		}
		// Base case
		else {
			// fprintf(stdout, "\nLED %d: base case\n", i);
			// If we are in the right-most column...
			if (leds[i][0] == 1 && leds[i][1] == display[0] - 1) {
				// fprintf(stdout, "In right-most column\n");
				xmin = WIDTH - led_dim;
				xmax = WIDTH;
				ymin = leds[i][2] * led_dim;
				ymax = (leds[i][2] + 1) * led_dim;
			} 
			// If we are in the bottom-most row...
			else if (leds[i][0] == 0 && leds[i][2] == display[1] - 1) {
				// fprintf(stdout, "In bottom-most row\n");
				xmin = leds[i][1] * led_dim;
				xmax = (leds[i][1] + 1) * led_dim;
				ymin = HEIGHT - led_dim;
				ymax = HEIGHT;

			} 
			// Base case of the base case
			else {
				xmin = leds[i][1] * led_dim;
				xmax = (leds[i][1] + 1) * led_dim;
				ymin = leds[i][2] * led_dim;
				ymax = (leds[i][2] + 1) * led_dim;
			}
		}
		
		// Print LED bounds information (for error-checking)
		fprintf(stdout, "LED %d: (%d, %d) to (%d, %d)\n", i, xmin, ymin, xmax, ymax);
		int start = ymin * WIDTH + xmin;
		int localWidth = xmax - xmin;
		int localHeight = ymax - ymin;
		
		// Calculate pixel positions
		for (j = 0; j < localHeight; j++) {
			for (k = 0; k < localWidth; k++) {
				// fprintf(stdout, "i = %d, j = %d, k = %d\n", i, j, k);
				// fprintf(stdout, "\tlocation = %d\n", j * localHeight + k);
				pixels[i][j * localHeight + k] = 3 * (start + j * WIDTH + k);
			}
		}
	}
}

static void getMeanPixelValues(int led_idx, int pixels[][led_dim * led_dim], unsigned char *rgb, const unsigned char *buffer) {
	// Initialize RGB values
	r = 0;
	g = 0;
	b = 0;
	
	// Iterate over all pixels
	for (i = 0; i < numPixels; i++) {
		r += buffer[pixels[led_idx][i] + 0];
		g += buffer[pixels[led_idx][i] + 1];
		b += buffer[pixels[led_idx][i] + 2];
	}
	
	// Store mean values
	rgb[0] = r / numPixels;
	rgb[1] = g / numPixels;
	rgb[2] = b / numPixels;
}

static void open_devices(void) {
	struct stat st;
    
	if (stat(input_dev, &st) == -1) {
            fprintf(stderr, "Cannot identify '%s'\n", input_dev);
            exit(EXIT_FAILURE);
    }
	
    if (!S_ISCHR(st.st_mode)) {
            fprintf(stderr, "%s is no input_dev\n", input_dev);
            exit(EXIT_FAILURE);
    }
	
	if ((input_fd = open(input_dev, O_RDWR  | O_NONBLOCK, 0)) < 0) {
		fprintf(stderr, "Cannot open '%s'\n", input_dev);
		exit(EXIT_FAILURE);
	}
	else {
		fprintf(stdout, "Device '%s' successfully opened!\n", input_dev);
	}
	
	if ((output_fd = open(output_dev, O_RDWR | O_NOCTTY | O_NONBLOCK)) < 0) {
		fprintf(stdout, "Can't open device '%s'\n", output_dev);
		exit(EXIT_FAILURE);
	}
	else {
		fprintf(stdout, "Device '%s' successfully opened!\n", output_dev);
	}
}

static void initialize_devices(struct v4l2_format *fmt) {
	// Get information about the input_dev
	struct v4l2_input input;
	v4l2_std_id std_id;
	int index;
	
	if (ioctl(input_fd, VIDIOC_G_INPUT, &index) == -1) {
		perror("VIDOC_G_INPUT");
		exit(EXIT_FAILURE);
	}
	
	// memset(&input, 0, sizeof(input));
	CLEAR(input);
	input.index = index;
	
	if (ioctl(input_fd, VIDIOC_ENUMINPUT, &input) == -1) {
		perror("VIDIOC_ENUMINPUT");
		exit(EXIT_FAILURE);
	}
	
	if ((input.std & V4L2_STD_NTSC) == 0) {
		fprintf(stderr, "Oops. NTSC is not supported.\n");
		exit(EXIT_FAILURE);
	}
	
	std_id = V4L2_STD_NTSC;
	
	if (ioctl (input_fd, VIDIOC_S_STD, &std_id) == -1) {
		perror("VIDIOC_S_STD");
		exit(EXIT_FAILURE);
	}
	
	// fprintf(stdout, "Current input: %s\nIndex: %d\nType: %d\nAudioset: %d\nTuner: %d\nStatus: %x\nCapabilities: %d\n", input.name, input.index, input.type, input.audioset, input.tuner, input.status, input.capabilities);
	
	// Get information about the current video standard
	struct v4l2_standard standard;
	
	if (ioctl(input_fd, VIDIOC_G_STD, &std_id)) {
		perror("VIDOC_G_STD");
		exit(EXIT_FAILURE);
	}
	
	// memset(&standard, 0, sizeof(standard));
	CLEAR(standard);
	standard.index = 0;
	
	while (ioctl(input_fd, VIDIOC_ENUMSTD, &standard) == 0) {
		if (standard.id & std_id) {
			// fprintf(stdout, "Current video standard: %s\n", standard.name);
			// exit(EXIT_SUCCESS);
			break;
		}
		standard.index++;
	} 
	
	// // Print information about the video standards supported by this input_dev
	// fprintf (stdout, "Current input %s supports:\n", input.name);
	// 
	// memset(&standard, 0, sizeof(standard));
	// standard.index = 0;
	// 
	// while (0 == ioctl (input_fd, VIDIOC_ENUMSTD, &standard)) {
	// 	if (standard.id & input.std)
	// 		printf ("\t%s\n", standard.name);
	// 
	// 	standard.index++;
	// }
	// 
	// if (errno == EINVAL || standard.index == 0) {
	// 	perror("VIDIOC_ENUMSTD");
	// 	exit(EXIT_FAILURE);
	// }
	
	// initialize the input_dev
	struct v4l2_capability cap;
	// fprintf(stdout, "Device '%s' ioctl value: %d\n", input_dev, ioctl(input_fd, VIDIOC_QUERYCAP, &cap));
	// fprintf(stdout, "Driver: %s\nCard: %s\nBus info: %s\nVersion: %d\nCapabilities: %x\n", (char *) cap.driver, (char *) cap.card, (char *) cap.bus_info, cap.version, cap.capabilities);
	
	
	struct v4l2_cropcap cropcap;
	struct v4l2_crop crop;
	
	CLEAR(cropcap);
	cropcap.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	if (ioctl(input_fd, VIDIOC_CROPCAP, &cropcap) == 0) {
		crop.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		crop.c = cropcap.defrect;
	}

	// Cropping is not allowed by this driver
		
	
	// Set information about formats
	fmt->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	fmt->fmt.pix.width = 720;
	fmt->fmt.pix.height = 480;
	fmt->fmt.pix.pixelformat = V4L2_PIX_FMT_RGB24;
	fmt->fmt.pix.field = V4L2_FIELD_INTERLACED;
	
	ioctl(input_fd, VIDIOC_S_FMT, fmt);
	
	struct v4l2_requestbuffers req;
	
	CLEAR(req);
	req.count = 6;
	req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	req.memory = V4L2_MEMORY_MMAP;
	
	ioctl(input_fd, VIDIOC_REQBUFS, &req);
	
	// fprintf(stdout, "Number of buffers: %d\n", req.count);
	
	buffers = calloc(req.count, sizeof(*buffers));
	for (n_buffers = 0; n_buffers < req.count; ++n_buffers) {
		struct v4l2_buffer buf;
		
		CLEAR(buf);
		buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		buf.memory = V4L2_MEMORY_MMAP;
		buf.index = n_buffers;
		
		ioctl(input_fd, VIDIOC_QUERYBUF, &buf);
		
		buffers[n_buffers].length = buf.length;
		buffers[n_buffers].start = mmap(NULL, buf.length, PROT_READ | PROT_WRITE, MAP_SHARED, input_fd, buf.m.offset);
	}
	
	// Serial port configuration
	tcgetattr(output_fd, &tty);
	tty.c_iflag = INPCK;
	tty.c_lflag = 0;
	tty.c_oflag = 0;
	tty.c_cflag = CREAD | CS8 | CLOCAL;
	tty.c_cc[VMIN] = 0;
	tty.c_cc[VTIME] = 0;
	cfsetispeed(&tty, B115200);
	cfsetospeed(&tty, B115200);
	tcsetattr(output_fd, TCSANOW, &tty);
	
	CLEAR(led_buffer);
	CLEAR(led_prev);
	// CLEAR(img_buffer);
	
	// Header initialization
	led_buffer[0] = 'A';
	led_buffer[1] = 'd';
	led_buffer[2] = 'a';
	led_buffer[3] = (N_LEDS - 1) >> 8;
	led_buffer[4] = (N_LEDS - 1) & 0xff;
	led_buffer[5] = led_buffer[3] ^ led_buffer[4] ^ 0x55;
	
	// Pre-compute gamma correction table for LED brightness levels
	float g = 1.25;
	for (i = 0; i < 256; i++) {
		float f = pow((value[i] / 255.0), g);
		gamma_table[i][0] = (char) red[(int) round(f * 255.0)];//(pow(red[i] / 255.0, g) * 255.0);//value[(int) (pow((red[i] / 255.0), g) * 255)];//red[(int) round(f * 255.0)];//value[(int) round(red[i])];//(pow(value[(int) round(red[i])] / 255.0, g) * 255.0);//pow((value[i] * red[i] / i), g);//pow((red[i] / i * value[i] / i), g) * 255.0;//(red[i] * f * 255.0 / i);//value[(int) round(pow((red[i] / 255.0), g) * 255.0)];//
		gamma_table[i][1] = (char) green[(int) round(f * 255.0)];//(pow(green[i] / 255.0, g) * 255.0);//value[(int) (pow((green[i] / 255.0), g) * 255)];//green[(int) round(f * 255.0)];//value[(int) round(green[i])];//g(pow(value[(int) round(green[i])] / 255.0, g) * 255.0);//pow((value[i] * green[i] / i), g);//pow((green[i] / i * value[i] / i), g) * 255.0;//(green[i] * f * 255.0 / i);//value[(int) round(pow((green[i] / 255.0), g) * 255.0)];//
		gamma_table[i][2] = (char) blue[(int) round(f * 255.0)];//(pow(blue[i] / 255.0, g) * 255.0);//value[(int) (pow((blue[i] / 255.0), g) * 255)];//value[(int) round(blue[i])];//(pow(value[(int) round(blue[i])] / 255.0, g) * 255.0);//pow((value[i] * blue[i] / i), g);//pow((blue[i] / i * value[i] / i), g) * 255.0;//(blue[i] * f * 255.0 / i);//value[(int) round(pow((blue[i] / 255.0), g) * 255.0)];//
	}
}

static void start_capture(void) {
	// start capturing with the input_dev
	unsigned int i;
	enum v4l2_buf_type type;
	
	for (i = 0; i < n_buffers; ++i) {
		struct v4l2_buffer buf;
		
		CLEAR(buf);
		buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		buf.memory = V4L2_MEMORY_MMAP;
		buf.index = i;
		
		ioctl(input_fd, VIDIOC_QBUF, &buf);
	}
	
	type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	ioctl(input_fd, VIDIOC_STREAMON, &type);
	fprintf(stdout, "Device '%s' started capturing!\n", input_dev);	
} 

static void stop_capture(void) {
	// stop capturing with the input_dev
	enum v4l2_buf_type type;
	type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	ioctl(input_fd, VIDIOC_STREAMOFF, &type);
	fprintf(stdout, "Device '%s' stopped capturing!\n", input_dev);
}

static void uninitialize_devices(void) {
	// un-initialize the input_dev
	unsigned int i;
	for (i = 0; i < n_buffers; ++i) {
		munmap(buffers[i].start, buffers[i].length);
	}
	
	free(buffers);
	fprintf(stdout, "Device '%s' memory successfully freed!\n", input_dev);	
}

static void close_devices(void) {
	// close the devices
	if (close(input_fd) == -1) {
		fprintf(stderr, "Failed to close '%s'\n", input_dev);
		exit(EXIT_FAILURE);
	}
	else {
		fprintf(stdout, "Device '%s' successfully closed!\n", input_dev);
		input_fd = -1;
	}
	
	if (close(output_fd) == -1) {
		fprintf(stderr, "Failed to close '%s'\n", output_dev);
		exit(EXIT_FAILURE);
	}
	else {
		fprintf(stdout, "Device '%s' successfully closed!\n", output_dev);
		output_fd = -1;
	}
}

static void INThandler(int sig) {
	fprintf(stdout, "\nDevices closing...\n");
	stop_capture();
	uninitialize_devices();
	close_devices();
	fprintf(stdout, "Thank you for using Aurora.\n");
	exit(EXIT_SUCCESS);
}

static void process_image(const void *img_buffer, int pixels[][led_dim * led_dim], struct v4l2_format *fmt) {
	// FILE *fout;
	// fout = fopen("frame_p.ppm", "w");
	// fprintf(fout, "P6\n%d %d 255\n", fmt->fmt.pix.width, fmt->fmt.pix.height);
	// int curvedPixels[WIDTH * HEIGHT];
	// Average color values per LED
	int weight = 257 - fade;
	for (k = 0; k < N_LEDS; k++) {
		getMeanPixelValues(k, pixels, rgb, img_buffer);
		led_buffer[6 + 3 * k] = gamma_table[rgb[0]][0];// * 3/4 + gamma_table[led_prev[3 * k + 0]][0] * 1/4;
		led_buffer[7 + 3 * k] = gamma_table[rgb[1]][1];// * 3/4 + gamma_table[led_prev[3 * k + 1]][1] * 1/4;
		led_buffer[8 + 3 * k] = gamma_table[rgb[2]][2];// * 3/4 + gamma_table[led_prev[3 * k + 2]][2] * 1/4;
		// led_prev[3 * k + 0] = rgb[0];
		// led_prev[3 * k + 1] = rgb[1];
		// led_prev[3 * k + 2] = rgb[2];
		// fputc(rgb[0], fout);
		// fputc(rgb[1], fout);
		// fputc(rgb[2], fout);
	}
	// fclose(fout);
	// Issue color data to LEDs
	tcdrain(output_fd);
	write(output_fd, &led_buffer[0], sizeof(led_buffer));
		
	// fputc('.', stdout);
	// fflush(stdout);
}

static void save_image(struct v4l2_buffer *buf, struct v4l2_format *fmt, char *out_name) {
	FILE *fout;
	fout = fopen(out_name, "w");
	if (!fout) {
		perror("Cannot open image");
		exit(EXIT_FAILURE);
	}
	// fputc('.', stdout);
	// fflush(stdout);
	fprintf(fout, "P6\n%d %d 255\n", fmt->fmt.pix.width, fmt->fmt.pix.height);
	fwrite(buffers[buf->index].start, buf->bytesused, 1, fout);
	fclose(fout);
	// fputc('.', stdout);
	// fflush(stdout);
	// fprintf(stdout, "saved %s!\n", out_name);
	// fprintf(stdout, "Frame number: %d\n", buf->sequence);
}

static int read_frame (struct v4l2_format *fmt, int frame_id, int pixels[][led_dim * led_dim]) {
	struct v4l2_buffer buf;
	unsigned int i;
	char out_name[16];
	
	CLEAR(buf);
	
	buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	buf.memory = V4L2_MEMORY_MMAP;
	// fprintf(stdout, "Dequeuing...");
	ioctl(input_fd, VIDIOC_DQBUF, &buf);
	// fprintf(stdout, "...dequeued!\n");
	assert(buf.index < n_buffers);
	
	process_image(buffers[buf.index].start, pixels, fmt);
	
	sprintf(out_name, "frame.ppm", frame_id);
	// fprintf(stdout, "Saving image...");
	save_image(&buf, fmt, out_name);
	// fprintf(stdout, "Queuing...");
	ioctl(input_fd, VIDIOC_QBUF, &buf);
	// fprintf(stdout, "...queued!\n\n");
	return 1;
}

static void mainloop(struct v4l2_format *fmt, int pixels[][led_dim * led_dim]) {
	// "main loop"
	unsigned int count = 0;
	fd_set input_fds;
	struct timeval tv;
	int r;
	
	// fprintf(stdout, "Changing tv_sec...\n");
	tv.tv_sec = 0;
	// fprintf(stdout, "...changed!\n");
	// fprintf(stdout, "Changing tv_usec...");
	tv.tv_usec = 0;
	// fprintf(stdout, "...changed!\n");
	
	// prev = start = time(NULL);
	
	for (;;) {
		signal(SIGINT, INThandler);	
		// fprintf(stdout, "Count = %d\n", count);
		// fprintf(stdout, "Zeroing input_fds...\n");
		FD_ZERO(&input_fds);
		// fprintf(stdout, "...zeroed!\n");
		// fprintf(stdout, "Setting input_fds...\n");
		FD_SET(input_fd, &input_fds);
		// fprintf(stdout, "...set!\n");
		// fprintf(stdout, "Selecting...\n");
		r = select(input_fd + 1, &input_fds, NULL, NULL, &tv);
		// read(input_fd, buffers, sizeof(buffers));
		if (r == -1) {
			perror("select");
			exit(EXIT_FAILURE);
		}
		// fprintf(stdout, "...selected!\n");
		if (read_frame(fmt, count, pixels)) {
		 	count++;
		//  	frame++;
		//  	if ((t = time(NULL)) != prev) {
		//  		fprintf(stdout, "Average frames/sec: %f\n", ((float) frame / (float) (t - start)));
				// fprintf(stdout, "Frames: %d\n", frame);
				// frame = 0;
		//  		prev = t;
		//  	}
		}
		else {
			break;
		}
	}
}

int main (int argc, char const *argv[]) {
	// Initialize tone curve tables
	// Value curve
	FILE *value_curve = fopen(value_curve_file, "r");
	for (i = 0; i < 256; i++) {
		fscanf(value_curve, "%f", &curved);
		value[i] = 0xff * curved;
	}
	fclose(value_curve);

	// Red curve
	FILE *red_curve = fopen(red_curve_file, "r");
	for (i = 0; i < 256; i++) {
		fscanf(red_curve, "%f", &curved);
		red[i] = 0xff * curved;
	}
	fclose(red_curve);

	// Green curve
	FILE *green_curve = fopen(green_curve_file, "r");
	for (i = 0; i < 256; i++) {
		fscanf(green_curve, "%f", &curved);
		green[i] = 0xff * curved;
	}
	fclose(green_curve);

	// Blue curve
	FILE *blue_curve = fopen(blue_curve_file, "r");
	for (i = 0; i < 256; i++) {
		fscanf(blue_curve, "%f", &curved);
		blue[i] = 0xff * curved;
	}
	fclose(blue_curve);

	struct v4l2_format fmt;
	CLEAR(fmt);
	
	open_devices();
	
	initialize_devices(&fmt);
	
	// Pre-compute pixel positions
	led_xdim = WIDTH / display[0];
	led_ydim = HEIGHT / display[1];
	led_dim = led_xdim < led_ydim ? led_xdim : led_ydim;
	numPixels = led_dim * led_dim;
	// fprintf(stdout, "Total pixels per LED: %d\n", numPixels);
	int pixels[N_LEDS][numPixels];
	CLEAR(pixels);
	setPixels(pixels);
	
	start_capture();
	
	mainloop(&fmt, pixels);
	
	stop_capture();
	
	uninitialize_devices();
	
	close_devices();
	
	return 0;
}
