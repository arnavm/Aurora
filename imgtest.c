// imgtest.c
// Code for testing image processing

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>
#include <math.h>
#include <signal.h>

#ifndef N_LEDS
#define N_LEDS 50
#endif

#ifndef WIDTH
#define WIDTH 720
#endif

#ifndef HEIGHT
#define HEIGHT 480
#endif

int i, j, k, r, b, g, dev_fd, img_fd, frame = 0,
	led_xdim, led_ydim, led_dim, numPixels;
const char *dev = "/dev/ttyUSB0";
const char *img = "frame.ppm";
unsigned char gamma_table[256][3], 
	led_buffer[6 + (N_LEDS * 3)], 
		img_buffer[WIDTH * HEIGHT * 3], 
			rgb[3] = {0, 0, 0};
time_t t, start, prev;

// 16 LEDs across, 9 down
static int display[2] = {16, 9};

// If first index is 0, pixel is along a row;
// if first index is 1, pixel is along a column;
static int leds[N_LEDS][3] = {
	// {8, 4}
	// {0, 0, 0}, {0, 1, 0}, {0, 2, 0}, {0, 3, 0}, {0, 4, 0},
	// 	{0, 5, 0}, {0, 6, 0}, {0, 7, 0}, {1, 7, 0}, {1, 7, 1},
	// 		{1, 7, 2}, {1, 7, 3}, {0, 7, 3}, {0, 6, 3}, {0, 5, 3},
	// 			{0, 4, 3}, {0, 3, 3}, {0, 2, 3}, {0, 1, 3}, {0, 0, 3},
	// 				{1, 0, 3}, {1, 0, 2}, {1, 0, 1}, {1, 0, 0}, {0, 0, 0}
	// 
	// {4, 8}
	// {0, 0, 0}, {0, 1, 0}, {0, 2, 0}, {0, 3, 0}, {1, 3, 0},
	// 	{1, 3, 1}, {1, 3, 2}, {1, 3, 3}, {1, 3, 4}, {1, 3, 5},
	// 		{1, 3, 6}, {1, 3, 7}, {0, 3, 7}, {0, 2, 7}, {0, 1, 7},
	// 			{0, 0, 7}, {1, 0, 7}, {1, 0, 6}, {1, 0, 5}, {1, 0, 4},
	// 				{1, 0, 3}, {1, 0, 2}, {1, 0, 1}, {1, 0, 0}, {0, 0, 0}
	// 
	// {16, 9}
	{0, 0, 0}, {0, 1, 0}, {0, 2, 0}, {0, 3, 0}, {0, 4, 0}, 
		{0, 5, 0}, {0, 6, 0}, {0, 7, 0}, {0, 8, 0}, {0, 9, 0}, 
			{0, 10, 0}, {0, 11, 0}, {0, 12, 0}, {0, 13, 0}, {0, 14, 0}, 
				{0, 15, 0}, {1, 15, 0}, {1, 15, 1}, {1, 15, 2}, {1, 15, 3}, 
					{1, 15, 4}, {1, 15, 5}, {1, 15, 6}, {1, 15, 7}, {1, 15, 8}, 
						{0, 15, 8}, {0, 14, 8}, {0, 13, 8}, {0, 12, 8}, {0, 11, 8}, 
							{0, 10, 8}, {0, 9, 8}, {0, 8, 8}, {0, 7, 8}, {0, 6, 8}, 
								{0, 5, 8}, {0, 4, 8}, {0, 3, 8}, {0, 2, 8}, {0, 1, 8}, 
									{0, 0, 8}, {1, 0, 8}, {1, 0, 7}, {1, 0, 6}, {1, 0, 5}, 
										{1, 0, 4}, {1, 0, 3}, {1, 0, 2}, {1, 0, 1}, {1, 0, 0}, 
						
};

void INThandler(int sig) {
	close(dev_fd);
	fprintf(stdout, "Device closed, shutting down...\n");
	exit(0);
}

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
				pixels[i][j * localHeight + k] = 15 + 3 * (start + j * WIDTH + k);
			}
		}
	}
}

static void getMeanPixelValues(int led_idx, int pixels[][led_dim * led_dim], unsigned char *rgb, unsigned char *buffer) {
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

int main(int argc, char const *argv[])
{	
	struct termios tty;
	
	// Open the device
	if ((dev_fd = open(dev, O_RDWR | O_NOCTTY | O_NONBLOCK)) < 0) {
		fprintf(stdout, "Can't open device '%s'\n", dev);
		return 1;
	}
	
	// Serial port configuration
	tcgetattr(dev_fd, &tty);
	tty.c_iflag = INPCK;
	tty.c_lflag = 0;
	tty.c_oflag = 0;
	tty.c_cflag = CREAD | CS8 | CLOCAL;
	tty.c_cc[VMIN] = 0;
	tty.c_cc[VTIME] = 0;
	cfsetispeed(&tty, B115200);
	cfsetospeed(&tty, B115200);
	tcsetattr(dev_fd, TCSANOW, &tty);
	
	// Clear buffers
	bzero(led_buffer, sizeof(led_buffer));
	bzero(img_buffer, sizeof(img_buffer));
	
	// Header initialization
	led_buffer[0] = 'A';
	led_buffer[1] = 'd';
	led_buffer[2] = 'a';
	led_buffer[3] = (N_LEDS - 1) >> 8;
	led_buffer[4] = (N_LEDS - 1) & 0xff;
	led_buffer[5] = led_buffer[3] ^ led_buffer[4] ^ 0x55;
	
	// Pre-compute gamma correction table for LED brightness levels:
	for (i = 0; i < 256; i++) {
		float f = pow((float) i / 255.0, 1.5);
		gamma_table[i][0] = (char) (f * 255.0);
		gamma_table[i][1] = (char) (f * 255.0);
		gamma_table[i][2] = (char) (f * 255.0);
	}
	
	// Pre-compute pixel positions
	led_xdim = WIDTH / display[0];
	led_ydim = HEIGHT / display[1];
	led_dim = led_xdim < led_ydim ? led_xdim : led_ydim;
	numPixels = led_dim * led_dim;
	fprintf(stdout, "Total pixels per LED: %d\n", numPixels);
	int pixels[N_LEDS][numPixels];
	bzero(pixels, sizeof(pixels));
	setPixels(pixels);
	
	
	// Process the image
	img_fd = open(img, O_RDONLY);
	read(img_fd, img_buffer, 15);
	fprintf(stdout, "%s", img_buffer);
	
	// Zero the buffer and store rest of image
	bzero(img_buffer, sizeof(img_buffer));
	read(img_fd, img_buffer, sizeof(img_buffer));
	
	// Close the image file
	close(img_fd);
	
	// Initialize time for statistics
	prev = start = time(NULL);
	
	for (;;) {
		signal(SIGINT, INThandler);
		for (k = 0; k < N_LEDS; k++) {
			getMeanPixelValues(k, pixels, rgb, img_buffer);
			led_buffer[6 + 3 * k] = gamma_table[rgb[0]][0];
			led_buffer[7 + 3 * k] = gamma_table[rgb[1]][1];
			led_buffer[8 + 3 * k] = gamma_table[rgb[2]][2];
		}
		
		// Issue color data to LEDs
		tcdrain(dev_fd);
		write(dev_fd, &led_buffer[0], sizeof(led_buffer));
		
		// Keep track of frame count for statistics
		// frame++;
		// Update stats once per second
		// if ((t = time(NULL)) != prev) {
		// 	// fprintf(stdout, "Average frames/sec: %d\n", (int)((float) frame / (float) (t - start)));
		// 	fprintf(stdout, "Frames: %d\n", frame);
		// 	frame = 0;
		// 	prev = t;
		// }
	}
	
	close(dev_fd);
	return 0;
}
