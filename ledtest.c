// ledtest.c
// Test code for RGB LED strip

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>
#include <math.h>

#ifndef N_LEDS
#define N_LEDS 50
#endif

int main (int argc, char const *argv[])
{
	int fd, i, bright_idx = 0, brightness, bytesToGo, bytesSent, r, g, b;
	unsigned char buffer[6 + (N_LEDS * 3)];
	time_t t, start, prev;
	struct termios tty;
	
	char *dev = "/dev/ttyUSB0";
	if ((fd = open(dev, O_RDWR | O_NOCTTY | O_NONBLOCK)) < 0) {
		fprintf(stdout, "Can't open device '%s'.\n", dev);
		return 1;
	}
	
	// Serial port configuration
	tcgetattr(fd, &tty);
	tty.c_iflag = INPCK;
	tty.c_lflag = 0;
	tty.c_oflag = 0;
	tty.c_cflag = CREAD | CS8 | CLOCAL;
	tty.c_cc[VMIN] = 0;
	tty.c_cc[VTIME] = 0;
	cfsetispeed(&tty, B115200);
	cfsetospeed(&tty, B115200);
	tcsetattr(fd, TCSANOW, &tty);
	
	// Clear LED buffer
	bzero(buffer, sizeof(buffer));
	
	// Header initialization
	buffer[0] = 'A';
	buffer[1] = 'd';
	buffer[2] = 'a';
	buffer[3] = (N_LEDS - 1) >> 8;
	buffer[4] = (N_LEDS - 1) & 0xff;
	buffer[5] = buffer[3] ^ buffer[4] ^ 0x55;
	
	r = 0;
	g = 0;
	b = 0;
	
	for (;;) {
		brightness = bright_idx % 0x100;
		
		// This generates random colors
		// r = floor(rand()/(double)(RAND_MAX) * 0xff);
		// g = floor(rand()/(double)(RAND_MAX) * 0xff);
		// b = floor(rand()/(double)(RAND_MAX) * 0xff);
		
		// Start at position 6, after LED header/magic word
		
		for (i = 6; i < sizeof(buffer);) {
			
			buffer[i++] = r++ % 0xff;
			buffer[i++] = g++ % 0xff;
			buffer[i++] = b++ % 0xff;
			
			// Issue color data to LEDs
			tcdrain(fd);
			write(fd, &buffer[0], sizeof(buffer));
		
			// Wait before changing colors
			usleep(50000);
			
		}
		
		++bright_idx;
	}
	
	close(fd);
	return 0;
}
