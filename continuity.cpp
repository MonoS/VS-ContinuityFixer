#include <stdlib.h>
#include "VapourSynth.h"
#include "VSHelper.h"
#include <stdint.h>

typedef struct least_squares_data
{
    int64_t integral_x;
    int64_t integral_y;
    int64_t integral_xy;
    int64_t integral_xsqr;
} least_squares_data;

typedef struct
{
    VSNodeRef *node;
    const VSVideoInfo *vi;

    int left;
    int right;
    int top;
    int bottom;

    int radius;
} ContinuityData;

void least_squares(int n, least_squares_data *d, double *a, double *b)
{
    double interval_x = (double) (d[n - 1].integral_x - d[0].integral_x);
    double interval_y = (double) (d[n - 1].integral_y - d[0].integral_y);
    double interval_xy = (double) (d[n - 1].integral_xy - d[0].integral_xy);
    double interval_xsqr = (double) (d[n - 1].integral_xsqr - d[0].integral_xsqr);


    // add 0.001f to denominator to prevent division by zero
    *a = ((double) n * interval_xy - interval_x * interval_y) / ((interval_xsqr * (double) n - interval_x * interval_x) + 0.001f);
    *b = (interval_y - *a * interval_x) / (double) n;
}

size_t required_buffer(int n)
{
    return n * sizeof(least_squares_data);
}

typedef void (*process_function)(uint8_t *x8, const uint8_t *y8, int x_dist_to_next, int y_dist_to_next, int n, int radius, least_squares_data *buf);

template <typename pixel_t>
void process_edge(uint8_t *x8, const uint8_t *y8, int x_dist_to_next, int y_dist_to_next, int n, int radius, least_squares_data *buf)
{
    int i;
    double a, b;
	
	pixel_t *x = (pixel_t *)x8;
	const pixel_t *y = (const pixel_t *)y8;
	
	x_dist_to_next /= sizeof(pixel_t);
	y_dist_to_next /= sizeof(pixel_t);
	
    buf[0].integral_x = x[0];
    buf[0].integral_y = y[0];
    buf[0].integral_xy = x[0] * y[0];
    buf[0].integral_xsqr = x[0] * x[0];

    for (i = 1; i < n; ++i)
    {
        int64_t _x = x[i * x_dist_to_next];
        int64_t _y = y[i * y_dist_to_next];
        buf[i].integral_x = buf[i - 1].integral_x + _x;
        buf[i].integral_y = buf[i - 1].integral_y + _y;
        buf[i].integral_xy = buf[i - 1].integral_xy + _x * _y;
        buf[i].integral_xsqr = buf[i - 1].integral_xsqr + _x * _x;
    }
    if (radius)
    {
        for (i = 0; i < n; ++i)
        {
            int left = i - radius;
            int right = i + radius;
            if (left < 0)
                left = 0;
            if (right > n - 1)
                right = n - 1;
            least_squares(right - left + 1, buf + left, &a, &b);
			
			//Saturate the pixel value
			double test = (x[i * x_dist_to_next] * a + b);
			if(test >= (1 << (sizeof(pixel_t)*8)))
				test = (1 << (sizeof(pixel_t)*8)) - 1;
            x[i * x_dist_to_next] = (pixel_t) test;
        }
    }
    else
    {
        least_squares(n, buf, &a, &b);
		
        for (i = 0; i < n; ++i)
		{
			//Saturate the pixel value
			double test = (x[i * x_dist_to_next] * a + b);
			if(test >= (1 << (sizeof(pixel_t)*8)))
				test = (1 << (sizeof(pixel_t)*8)) - 1;
            x[i * x_dist_to_next] = (pixel_t)test;
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

        int height = vsapi->getFrameHeight(src, 0);
        int width = vsapi->getFrameWidth(src, 0);
        int stride = vsapi->getStride(src, 0);
		int bytesPerSample = vsapi->getFrameFormat(src)->bytesPerSample;

        least_squares_data *buf = (least_squares_data*) malloc(required_buffer(width > height ? width : height));
		
		VSFrameRef *dst = vsapi->copyFrame(src, core);
		
        uint8_t *dest = vsapi->getWritePtr(dst, 0);
		
		process_function proc;
		if (bytesPerSample == 1)
			proc = process_edge<uint8_t>;
		else
			proc = process_edge<uint16_t>;

        int i;
        // top
        for (i = 0; i < d->top; ++i)
        {
            int ref_row = d->top - i;
            proc(dest + stride * (ref_row - 1), dest + stride * ref_row, bytesPerSample, bytesPerSample, width, d->radius, buf);
        }

        // bottom
        for (i = 0; i < d->bottom; ++i)
        {
            int ref_row = height - d->bottom - 1 + i;
            proc(dest + stride * (ref_row + 1), dest + stride * ref_row, bytesPerSample, bytesPerSample, width, d->radius, buf);
        }

        // left
        for (i = 0; i < d->left; ++i)
        {
            int ref_col = d->left - i;
            proc(dest + (ref_col - 1) * bytesPerSample, dest + ref_col * bytesPerSample, stride, stride, height, d->radius, buf);
        }

        // right
        for (i = 0; i < d->right; ++i)
        {
            int ref_col = width - d->right - 1 + i;
            proc(dest + (ref_col + 1) * bytesPerSample, dest + ref_col * bytesPerSample, stride, stride, height, d->radius, buf);
        }
        
        free(buf);
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

    d.bottom = vsapi->propGetInt(in, "bottom", 0, &err);
    d.top =    vsapi->propGetInt(in, "top", 0, &err);
    d.right =  vsapi->propGetInt(in, "right", 0, &err);
    d.left =   vsapi->propGetInt(in, "left", 0, &err);

    d.radius = vsapi->propGetInt(in, "radius", 0, &err);
    if (err)
    {
        d.radius = d.vi->height < d.vi->width ? d.vi->height : d.vi->width;
    }
	
    data = (ContinuityData*) malloc(sizeof(d));
    *data = d;

    vsapi->createFilter(in, out, "ContinuityFixer", continuityInit, continuityGetFrame, continuityFree, fmParallel, 0, data, core);
}

VS_EXTERNAL_API(void) VapourSynthPluginInit(VSConfigPlugin configFunc, VSRegisterFunction registerFunc, VSPlugin *plugin)
{
    configFunc("com.monos.edgefixer", "edgefixer", "VapourSynth edgefixer port", VAPOURSYNTH_API_VERSION, 1, plugin);
    registerFunc("ContinuityFixer", "clip:clip;"
	                                "left:int;"
									"top:int;"
									"right:int;"
									"bottom:int;"
									"radius:int:opt;",
									continuityCreate, 0, plugin);
}
