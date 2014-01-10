/*
 * Copyright 2012 (c) Barnes & Noble Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 * MA 02111-1307 USA
 */

/*
 * Various B&N boot support functions shared between the different HW platforms
 */

#include <common.h>
#include <bn_boot.h>

struct img_info bootimg_info = {
	.width = 136,
	.height = 136,
	.bg_color = 0x0,
};

struct cpr_info display_cpr_info = {
	.cpr_values_valid = 0,
	.cpr_values = {0, 0, 0, 0, 0, 0, 0, 0, 0},
};

#define char2u16(char_ptr) \
    ((char_ptr)[0] | ((char_ptr)[1] << 8))

static char * next_line(char *line);
void display_rle(uint16_t const *start, uint16_t *fb, int pwidth, int pheight)
{
	uint16_t i, j;
	register uint16_t count;
	register uint16_t color;

	char * line = (char *) start;

	int width = simple_strtoul(line, &line, 10);
	line = next_line((char *) line);
	int height = simple_strtoul(line, &line, 10);
	line = next_line((char *) line);

	line++; // skip the extra carriage return

	count = char2u16(line);
	color = char2u16(&(line[2]));
	//bootimg_info.bg_color = color;

	if (pheight > pwidth)
		fb += (pheight - 450) * pwidth + (pwidth - width)/2;
	else
		fb += (pheight - height)/2 * pwidth + 400;

	for (i = 0; i < height; ++i) {
		for (j = 0; j < width; ++j) {
			*fb++ = color;
			--count;
			if (count == 0) {
				line += 4;
				count = char2u16(line);
				color = char2u16(&(line[2]));
			}
		}
		fb += pwidth - width;
	}
}

#define SPLASH_MAX_SIZE (0x1000000)
int gunzip(void *, int, unsigned char *, unsigned long *);

static char * next_line(char *line)
{
	char *l = line;

	while (*l != '\n')
		++l;

	return ++l;
}

int display_ppm(uint8_t *ppm, uint32_t *fb, int width, int height)
{
	uint16_t i, j;
	char *line;
	char *n;
	unsigned long logo_width, logo_height = 0;

	line = next_line((char *) ppm); // skip type
	line = next_line(line); // skip creator/comment

	// line should now be width height

	if (line == NULL) {
		return -1;
	}

	n = strchr(line, '\n');

	if (n) *n = '\0';

	logo_width = simple_strtoul(line, &line, 10);
	++line;
	logo_height = simple_strtoul(line, &line, 10);

	if ((logo_width == 0) || (logo_height == 0)) {
		return -1;
	}

	// skip pixel size
	line = next_line(line);

	// set the image height, width, and bg_color 
	bootimg_info.width = logo_width;
	bootimg_info.height = logo_height;
	bootimg_info.bg_color = (line[0] << 16) | (line[1] << 8) | line[2];

	for (i = 0; i < logo_height; ++i) {
		for (j = 0; j < logo_width; ++j) {
			*fb++ = (line[0] << 16) | (line[1] << 8) | line[2];
			line += 3;
		}
	}

	return 0;
}


int display_mmc_gzip_ppm(int mmc, int part, const char *filename, uint32_t *fb, int width, int height)
{
	char buf[64];
	uint8_t *ppm = (uint8_t *) 0x82000000;
	uchar *gzip_ppm = (uchar *) 0x81000000;
	unsigned long len = SPLASH_MAX_SIZE;

	sprintf(buf, "mmcinit %d; fatload mmc %d:%d 0x81000000 %s 4194304", mmc, mmc, part, filename);

	if (run_command(buf, 0)) {
		return -1;
	}

	if (gunzip(ppm, len, gzip_ppm, &len) != 0) {
		return -1;
	}

	return display_ppm(ppm, fb, width, height);
}

int load_display_cpr(void)
{
	char command_buf[256];
	char cpr_buf[DISPLAYCPR_TOKEN_MAXSIZE];
	char *cpr_ptr, *end_ptr;
	int index = 0;
	int retval = -1;
	int sum_of_coefs = 0;

	memset(cpr_buf, 0, sizeof(cpr_buf));
	sprintf(command_buf, "mmcinit 1; fatload mmc 1:5 0x%08x DisplayCPR %d", cpr_buf, DISPLAYCPR_TOKEN_MAXSIZE);
	if (run_command(command_buf, 0)) {
		sprintf(command_buf, "mmcinit 1; fatload mmc 1:4 0x%08x devconf/DisplayCPR %d", cpr_buf, DISPLAYCPR_TOKEN_MAXSIZE);
		if (run_command(command_buf, 0)) {
			printf("No DisplayCPR found in /bootdata nor /rom/devconf.\n");
			return retval;
		}
	}

	cpr_ptr = strtok(cpr_buf, " ");
	while (cpr_ptr != NULL  && index < DISPLAYCPR_NUM_VALUES) {
		display_cpr_info.cpr_values[index] = simple_strtol(cpr_ptr, &end_ptr, 10);
		if (cpr_ptr == end_ptr) {
			printf("invalid CPR value [%s]\n", cpr_ptr);
			return retval;
		}
		sum_of_coefs += display_cpr_info.cpr_values[index];
		index++;
		cpr_ptr = strtok(NULL, " ");
	}

	if ((index != 9) || (sum_of_coefs == 0)) {
		printf("Invalid CPR token: [%s]\n", cpr_buf);
	} else {
		display_cpr_info.cpr_values_valid = 1;
		retval = 0;
	}

	return retval;
}
