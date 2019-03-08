/*
 * Copyright (c) 2016 Nexell Co., Ltd.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <getopt.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>

#include <nx-v4l2.h>

static void print_usage(const char *appName)
{
	printf(
		"  common options :\n"
		"     -e : enumerate all video devices\n"
		"     -p : print all connected v4l2 devices\n"
		"     -h : help\n"
		" =========================================================================================================\n\n",
		appName);
}

int32_t main(int32_t argc, char *argv[])
{
	int32_t opt, ret = 0;

	printf("======camera test application=====\n");
	while (-1 != (opt = getopt(argc, argv, "eph")))
	{
		switch (opt)
		{
			case 'e':
				nx_v4l2_enumerate();
				return 0;
			case 'p':
				print_all_nx_v4l2_entry();
				return 0;
			case 'h':
				print_usage(argv[0]);
				return 0;
			default:
				break;
		}
	}
	return ret;
}
