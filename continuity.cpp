#include "VapourSynth.h"
#include "VSHelper.h"

#define MAX(a, b) (((a) > (b)) ? (a) : (b))
#define MIN(a, b) (((a) < (b)) ? (a) : (b))

typedef struct least_squares_data {
	int32_t integral_x;
	int32_t integral_y;
	int32_t integral_xy;
	int32_t integral_xsqr;
} least_squares_data;

typedef struct least_squares_data64 {
	int64_t integral_x;
	int64_t integral_y;
	int64_t integral_xy;
	int64_t integral_xsqr;
} least_squares_data64;

typedef struct
{
    VSNodeRef *node;
    const VSVideoInfo *vi;

    int left[3];
    int right[3];
    int top[3];
    int bottom[3];

    int radius[3];
} ContinuityData;

static void least_squares(int n, least_squares_data* d, float* a, float* b)
{
	float interval_x = (float)(d[n - 1].integral_x - d[0].integral_x);
	float interval_y = (float)(d[n - 1].integral_y - d[0].integral_y);
	float interval_xy = (float)(d[n - 1].integral_xy - d[0].integral_xy);
	float interval_xsqr = (float)(d[n - 1].integral_xsqr - d[0].integral_xsqr);

	/* Add 0.001f to denominator to prevent division by zero. */
	*a = ((float)n * interval_xy - interval_x * interval_y) / ((interval_xsqr * (float)n - interval_x * interval_x) + 0.001f);
	*b = (interval_y - *a * interval_x) / (float)n;
}

static void least_squares64(int n, least_squares_data64* d, double* a, double* b)
{
	double interval_x = (double)(d[n - 1].integral_x - d[0].integral_x);
	double interval_y = (double)(d[n - 1].integral_y - d[0].integral_y);
	double interval_xy = (double)(d[n - 1].integral_xy - d[0].integral_xy);
	double interval_xsqr = (double)(d[n - 1].integral_xsqr - d[0].integral_xsqr);

	/* Add 0.001f to denominator to prevent division by zero. */
	*a = ((double)n * interval_xy - interval_x * interval_y) / ((interval_xsqr * (double)n - interval_x * interval_x) + 0.001f);
	*b = (interval_y - *a * interval_x) / (double)n;
}

static uint8_t float_to_u8(float x)
{
	return (uint8_t)lrintf(MIN(MAX(x, 0), UINT8_MAX));
}

static uint16_t double_to_u16(double x)
{
	return (uint16_t)lrint(MIN(MAX(x, 0), UINT16_MAX));
}

static size_t edgefixer_required_buffer_b(int n)
{
	return n * sizeof(least_squares_data);
}

static size_t edgefixer_required_buffer_w(int n)
{
	return n * sizeof(least_squares_data64);
}
static void edgefixer_process_edge_b(void* xptr, const void* yptr, int x_dist_to_next, int y_dist_to_next, int n, int radius, void* tmp)
{
	uint8_t* x = reinterpret_cast<uint8_t*>(xptr);
	const uint8_t* y = reinterpret_cast<const uint8_t*>(yptr);

	least_squares_data* buf = (least_squares_data*)tmp;
	float a, b;
	int i;

	buf[0].integral_x = x[0];
	buf[0].integral_y = y[0];
	buf[0].integral_xy = x[0] * y[0];
	buf[0].integral_xsqr = x[0] * x[0];

	for (i = 1; i < n; ++i) {
		uint16_t _x = x[static_cast<int64_t>(i) * x_dist_to_next / sizeof(uint8_t)];
		uint16_t _y = y[static_cast<int64_t>(i) * y_dist_to_next / sizeof(uint8_t)];

		buf[i].integral_x = buf[i - 1].integral_x + _x;
		buf[i].integral_y = buf[i - 1].integral_y + _y;
		buf[i].integral_xy = buf[i - 1].integral_xy + _x * _y;
		buf[i].integral_xsqr = buf[i - 1].integral_xsqr + _x * _x;
	}

	if (radius) {
		for (i = 0; i < n; ++i) {
			int left = i - radius;
			int right = i + radius;

			if (left < 0)
				left = 0;
			if (right > n - 1)
				right = n - 1;
			least_squares(right - left + 1, buf + left, &a, &b);
			x[static_cast<int64_t>(i) * x_dist_to_next / sizeof(uint8_t)] = float_to_u8(x[static_cast<int64_t>(i) * x_dist_to_next / sizeof(uint8_t)] * a + b);
		}
	}
	else {
		least_squares(n, buf, &a, &b);
		for (i = 0; i < n; ++i) {
			x[static_cast<int64_t>(i) * x_dist_to_next / sizeof(uint8_t)] = float_to_u8(x[static_cast<int64_t>(i) * x_dist_to_next / sizeof(uint8_t)] * a + b);
		}
	}
}

static void edgefixer_process_edge_w(void* xptr, const void* yptr, int x_dist_to_next, int y_dist_to_next, int n, int radius, void* tmp)
{
	uint16_t* x = reinterpret_cast<uint16_t*>(xptr);
	const uint16_t* y = reinterpret_cast<const uint16_t*>(yptr);

	least_squares_data64* buf = (least_squares_data64*)tmp;
	double a, b;
	int i;

	buf[0].integral_x = x[0];
	buf[0].integral_y = y[0];
	buf[0].integral_xy = (long long)x[0] * y[0];
	buf[0].integral_xsqr = (long long)x[0] * x[0];

	for (i = 1; i < n; ++i) {
		uint32_t _x = x[static_cast<int64_t>(i) * x_dist_to_next / sizeof(uint16_t)];
		uint32_t _y = y[static_cast<int64_t>(i) * y_dist_to_next / sizeof(uint16_t)];

		buf[i].integral_x = buf[i - 1].integral_x + _x;
		buf[i].integral_y = buf[i - 1].integral_y + _y;
		buf[i].integral_xy = buf[i - 1].integral_xy + static_cast<int64_t>(_x) * _y;
		buf[i].integral_xsqr = buf[i - 1].integral_xsqr + static_cast<int64_t>(_x) * _x;
	}

	if (radius) {
		for (i = 0; i < n; ++i) {
			int left = i - radius;
			int right = i + radius;

			if (left < 0)
				left = 0;
			if (right > n - 1)
				right = n - 1;
			least_squares64(right - left + 1, buf + left, &a, &b);
			x[static_cast<int64_t>(i) * x_dist_to_next / sizeof(uint16_t)] = double_to_u16(x[static_cast<int64_t>(i) * x_dist_to_next / sizeof(uint16_t)] * a + b);
		}
	}
	else {
		least_squares64(n, buf, &a, &b);
		for (i = 0; i < n; ++i) {
			x[static_cast<int64_t>(i) * x_dist_to_next / sizeof(uint16_t)] = double_to_u16(x[static_cast<int64_t>(i) * x_dist_to_next / sizeof(uint16_t)] * a + b);
		}
	}
}

static void VS_CC continuityInit(VSMap *in, VSMap *out, void **instanceData, VSNode *node, VSCore *core, const VSAPI *vsapi)
{
    ContinuityData *d = (ContinuityData *) * instanceData;
    vsapi->setVideoInfo(d->vi, 1, node);
}

static const VSFrameRef  *VS_CC continuityGetFrame(int n, int activationReason, void **instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi)
{
    ContinuityData *d = (ContinuityData *) * instanceData;

    if (activationReason == arInitial)
    {
        vsapi->requestFrameFilter(n, d->node, frameCtx);
    }
    else if (activationReason == arAllFramesReady)
    {
        const VSFrameRef *src = vsapi->getFrameFilter(n, d->node, frameCtx);
		VSFrameRef *dst = vsapi->copyFrame(src, core);
		
		size_t(*required_buffer)(int) = d->vi->format->bytesPerSample == 2 ? edgefixer_required_buffer_w : edgefixer_required_buffer_b;
		void (*process_edge)(void*, const void*, int, int, int, int, void*) = d->vi->format->bytesPerSample == 2 ? edgefixer_process_edge_w : edgefixer_process_edge_b;
		
		int h = vsapi->getFrameHeight(src, 0);
		int w = vsapi->getFrameWidth(src, 0);

		void* tmp = malloc(required_buffer(w > h ? w : h));

		for(int j = 0; j < d->vi->format->numPlanes; ++j)
		{
			int height = vsapi->getFrameHeight(src, j);
			int width = vsapi->getFrameWidth(src, j);
			int stride = vsapi->getStride(src, j);
			int step = d->vi->format->bytesPerSample;
			
			uint8_t *dest = vsapi->getWritePtr(dst, j);

			int i;
			// top
			for (i = 0; i < d->top[j]; ++i)
			{
				int ref_row = d->top[j] - i;
				process_edge(dest + stride * (ref_row - static_cast<int64_t>(1)), dest + static_cast<int64_t>(stride) * ref_row, step, step, width, d->radius[j], tmp);
			}

			// bottom
			for (i = 0; i < d->bottom[j]; ++i)
			{
				int ref_row = height - d->bottom[j] - 1 + i;
				process_edge(dest + stride * (ref_row + static_cast<int64_t>(1)), dest + static_cast<int64_t>(stride) * ref_row, step, step, width, d->radius[j], tmp);
			}

			// left
			for (i = 0; i < d->left[j]; ++i)
			{
				int ref_col = d->left[j] - i;
				process_edge(dest + (ref_col - static_cast<int64_t>(1)) * step, dest + ref_col * static_cast<int64_t>(step), stride, stride, height, d->radius[j], tmp);
			}

			// right
			for (i = 0; i < d->right[j]; ++i)
			{
				int ref_col = width - d->right[j] - 1 + i;
				process_edge(dest + (ref_col + static_cast<int64_t>(1)) * step, dest + ref_col * static_cast<int64_t>(step), stride, stride, height, d->radius[j], tmp);
			}
		}
        
        free(tmp);
        vsapi->freeFrame(src);

        return dst;
    }

    return 0;
}

static void VS_CC continuityFree(void *instanceData, VSCore *core, const VSAPI *vsapi)
{
    ContinuityData *d = (ContinuityData *)instanceData;
    vsapi->freeNode(d->node);
    free(d);
}

static void VS_CC  continuityCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi)
{
    ContinuityData d;
    ContinuityData *data;
    int err;

    d.node = vsapi->propGetNode(in, "clip", 0, 0);
    d.vi = vsapi->getVideoInfo(d.node);

    if (!isConstantFormat(d.vi) || d.vi->format->sampleType != stInteger || d.vi->format->bitsPerSample > 16)
    {
        vsapi->setError(out, "ContinuityFixer: only constant format 8..16bit integer input supported");
        vsapi->freeNode(d.node);
        return;
    }
	
	int nPlanes = d.vi->format->numPlanes;
	int nLeft = vsapi->propNumElements(in, "left");
	int nTop = vsapi->propNumElements(in, "top");
	int nRight = vsapi->propNumElements(in, "right");
	int nBottom = vsapi->propNumElements(in, "bottom");
	int nRadius = vsapi->propNumElements(in, "radius");
	
	if(nPlanes > 3)
	{
		vsapi->setError(out, "ContinuityFixer: Too many planes in the source clip");
        vsapi->freeNode(d.node);
        return;
	}	
	
	if(nLeft > nPlanes)
	{
		vsapi->setError(out, "ContinuityFixer: Too many left parameter");
        vsapi->freeNode(d.node);
        return;
	}
	if(nTop > nPlanes)
	{
		vsapi->setError(out, "ContinuityFixer: Too many top parameter");
        vsapi->freeNode(d.node);
        return;
	}
	if(nRight > nPlanes)
	{
		vsapi->setError(out, "ContinuityFixer: Too many right parameter");
        vsapi->freeNode(d.node);
        return;
	}
	if(nBottom > nPlanes)
	{
		vsapi->setError(out, "ContinuityFixer: Too many bottom parameter");
        vsapi->freeNode(d.node);
        return;
	}
	if(nRadius > nPlanes)
	{
		vsapi->setError(out, "ContinuityFixer: Too many radius parameter");
        vsapi->freeNode(d.node);
        return;
	}
	
	for (int i = 0; i < nPlanes; ++i)
    {
		d.left[i] = vsapi->propGetInt(in, "left", i, &err);
		if (err)
			d.left[i] = 0;
		d.top[i] = vsapi->propGetInt(in, "top", i, &err);
		if (err)
			d.top[i] = 0;
		d.right[i] = vsapi->propGetInt(in, "right", i, &err);
		if (err)
			d.right[i] = 0;
		d.bottom[i] = vsapi->propGetInt(in, "bottom", i, &err);
		if (err)
			d.bottom[i] = 0;
	}
	
	for (int i = 0; i < nPlanes; ++i)
    {
		d.radius[i] = vsapi->propGetInt(in, "radius", i, &err);
    
		if (err)
		{
			if(i == 0)
			{
				d.radius[i] = d.vi->height < d.vi->width ? d.vi->height : d.vi->width;
			}
			else
			{
				d.radius[i] = (d.vi->height >> d.vi->format->subSamplingH) < (d.vi->width >> d.vi->format->subSamplingW) ? (d.vi->height >> d.vi->format->subSamplingH) : (d.vi->width >> d.vi->format->subSamplingW);
			}
		}
	}
	
    data = (ContinuityData*) malloc(sizeof(d));
    *data = d;

    vsapi->createFilter(in, out, "ContinuityFixer", continuityInit, continuityGetFrame, continuityFree, fmParallel, 0, data, core);
}

VS_EXTERNAL_API(void) VapourSynthPluginInit(VSConfigPlugin configFunc, VSRegisterFunction registerFunc, VSPlugin *plugin)
{
    configFunc("com.monos.continuityfixer", "cf", "VapourSynth ContinuityFixer port", VAPOURSYNTH_API_VERSION, 1, plugin);
    registerFunc("ContinuityFixer", "clip:clip;"
	                                "left:int[]:opt;"
									"top:int[]:opt;"
									"right:int[]:opt;"
									"bottom:int[]:opt;"
									"radius:int[]:opt;",
									continuityCreate, 0, plugin);
}
