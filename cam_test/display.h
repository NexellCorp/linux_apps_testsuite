#ifndef _DISPLAY_H_
#define _DISPLAY_H_

#ifdef __cplusplus
extern "C" {
#endif

struct dp_device *dp_device_init(int drmFd);
struct dp_plane* dp_plane_init(struct dp_device *device, uint32_t format);
struct dp_framebuffer *dp_buffer_add(struct dp_device *device,
		uint32_t format, uint32_t width, uint32_t height,
		bool seperated, int gem_fd, uint32_t size, bool interlaced);

int dp_plane_update(struct dp_device *device, struct dp_framebuffer *fb, struct dp_plane *plane,
		uint32_t crop_x, uint32_t crop_y, uint32_t crop_w, uint32_t crop_h,
		uint32_t dw, uint32_t dh);


#ifdef __cplusplus
}
#endif

#endif
