#ifndef _DEBUG_H_
#define _DEBUG_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

extern bool cam_dbg_on;

#define CAM_ERR(args...) \
		printf(args)

#define CAM_DBG(args...) \
	do {		\
		if (cam_dbg_on)	\
			printf(args);	\
	} while (0)

#ifdef __cplusplus
}
#endif

#endif
