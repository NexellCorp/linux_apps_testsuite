#ifndef _MEMORY_H_
#define _MEMORY_H_

#ifdef __cplusplus
extern "C" {
#endif

#define MAX_PLANES	3

typedef struct
{
	uint32_t	width;			/*	Video Image's Width	*/
	uint32_t	height;			/*	Video Image's Height	*/
	uint32_t	planes;			/*	Number of valid planes	*/
	uint32_t	format;			/*	Pixel Format(N/A)	*/

	int		dmaFd[MAX_PLANES];	/*	DMA memory Handle	*/
	int		gemFd[MAX_PLANES];	/*	GEM Handle		*/
	uint32_t	flink[MAX_PLANES];	/*	GEM Handle		*/
	void		*vaddr[MAX_PLANES];	/*	Virtual Address Pointer	*/

	uint32_t	size[MAX_PLANES];	/*	Each plane's size.	*/
	uint32_t	stride[MAX_PLANES];	/*	Each plane's stride.	*/
} memory_info, *memory_handle;

memory_info *alloc_memory(int drmFd, uint32_t width, uint32_t height,
		uint32_t planes, uint32_t format, bool interlaced);
void free_memory(int drmFd, memory_info * pMem);
uint32_t IsContinuousPlane(uint32_t fourcc);

#ifdef __cplusplus
}
#endif

#endif
