/*
Copyright 2020 Peter Stoiber

This library is free software; you can redistribute it and/or
modify it under the terms of the GNU Lesser General Public
License as published by the Free Software Foundation; either
version 2.1 of the License, or (at your option) any later version.

This library is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
Lesser General Public License for more details.

You should have received a copy of the GNU Lesser General Public
License along with this library; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA

Please contact the author if you need another license.
This Repository is provided "as is", without warranties of any kind.

*/

#include "libuvc_support.h"



#include <android-libjpeg-turbo/jni/vendor/libjpeg-turbo/libjpeg-turbo-2.0.1/turbojpeg.h>
//#include <android-libjpeg-turbo/jni/vendor/libjpeg-turbo/libjpeg-turbo-2.0.1/jpeglib.h>

#include <jpeg8d/jpeglib.h>

#include <android/bitmap.h>
#include <setjmp.h>

volatile bool write_Ctl_Buffer = false;
int ueberschreitungDerUebertragungslaenge = 0 ;

int verbose = 0;
#define USE_EOF

// Camera Values

static int packetsPerRequest;
static int maxPacketSize;
static int activeUrbs;
static int camStreamingAltSetting;
static int camFormatIndex;
static int camFrameIndex;
static int camFrameInterval;
static int minCamFrameInterval;
static int maxCamFrameInterval;
static int bmHint;
static int imageWidth;
static int imageHeight;
static int camStreamingEndpoint;
static const char *mUsbFs;
static int productID;
static int vendorID;
static int busnum;
static int devaddr;
static uint16_t bcdUVC;
static int laufzeit = 2;
static int fd, i, j, result = -1;
static int camStreamingInterfaceNum;
const char* frameFormat;    //  MJPEG   YUY2
volatile int total = 0;
volatile int totalFrame = 0;
volatile bool initialized = false;
volatile  bool runningStream = false;
volatile bool runningStreamFragment = false;
static uint8_t frameUeberspringen = 0;
static uint8_t numberOfAutoFrames;
static int streamEndPointAdressOverNative;

volatile int stopTheStream;


uvc_context_t *uvcContext;
uvc_device_handle_t *uvcDeviceHandle;
uvc_device_t *mDevice;



libusb_context *ctx_global;
//libusb_device_handle *devh = NULL;

AutotransferStruct autoStruct;
ManualTestFrameStruct manualFrameStruct;
clock_t startTime;
bool exit_stream = false;
// Skip Flip for LowerAndroid
bool low_Android = false;

int initStreamingParmsIntArray[3];
int probedStreamingParmsIntArray[3];
int finalStreamingParmsIntArray_first[3];
int finalStreamingParmsIntArray[3];
autoStreamComplete autoStreamfinished = NULL;
void setAutoStreamComplete(autoStreamComplete autoStream)
{
    autoStreamfinished = autoStream;
}

AutotransferStruct get_autotransferStruct () {
    return autoStruct;
}

typedef struct _Frame_Data
{
    int FrameSize;
    int  FrameBufferSize;
    unsigned char videoframe[];
} FrameData;
FrameData *videoFrameData[2];
volatile uint8_t whichVideoFrameDate;

uint8_t streamControl[48];
uint8_t unpackUsbInt(uint8_t *p, int i);

bool camIsOpen = false;
uint8_t cameraDevice_is_wraped;
uint8_t cameraDevice_is_rewraped;


typedef struct _CTL_Data
{
    int  BufferSize;
    unsigned char ctl_transfer_values[];
} CtlData;
CtlData *ctl_transfer_Data;

void getStreamingParmsArray(int *array, uint8_t *buf) {
    array[0] = buf[2] & 0xf;
    array[1] = buf[3] & 0xf;
    uint8_t pos = 4;
    array[2] = (buf[pos + 3] << 24) | ((buf[pos + 2] & 0xFF) << 16) | ((buf[pos + 1] & 0xFF) << 8) | (buf[pos] & 0xFF);
}

eventCallback sendReceivedDataToJava = NULL;
void setCallback(eventCallback evnHnd) {
    sendReceivedDataToJava = evnHnd;
}

frameComplete sendFrameComplete = NULL;
void setFrameComplete(frameComplete evnHnd){
    sendFrameComplete = evnHnd;
}

jnaFrameCallback jnaSendFrameToJava = NULL;
void setJnaFrameCallback(jnaFrameCallback evnHnd) {
    jnaSendFrameToJava = evnHnd;
}


/// JNI Values
JavaVM* javaVm;
jclass class;
jclass jniHelperClass;
jclass mainActivityObj;
ANativeWindow *mCaptureWindow;

// Stream - Activity
jmethodID javaRetrievedStreamActivityFrameFromLibUsb;
jmethodID javainitializeStreamArray;
// For capturing Images, Videos
jmethodID javaPictureVideoCaptureYUV;
jmethodID javaPictureVideoCaptureMJPEG;
jmethodID javaPictureVideoCaptureRGB;

// SERVICE
jmethodID javaServicePublishResults;
jmethodID javaServiceReturnToStreamActivity;


// WebRtc
jmethodID javaRetrievedFrameFromLibUsb21;
jmethodID javaProcessReceivedMJpegVideoFrameKamera;
jmethodID javaWEBrtcProcessReceivedVideoFrameYuv;


static inline unsigned char sat(int i) {
    return (unsigned char) (i >= 255 ? 255 : (i < 0 ? 0 : i));
}

struct error_mgr {
    struct jpeg_error_mgr super;
    jmp_buf jmp;
};

static void _error_exit(j_common_ptr dinfo) {
    struct error_mgr *myerr = (struct error_mgr *) dinfo->err;
#ifndef NDEBUG
    #if (defined(ANDROID) || defined(__ANDROID__))
	char err_msg[1024];
	(*dinfo->err->format_message)(dinfo, err_msg);
	err_msg[1023] = 0;
	LOGW("err=%s", err_msg);
#else
	(*dinfo->err->output_message)(dinfo);
#endif
#endif
    longjmp(myerr->jmp, 1);
}


/** @internal
 * @brief Event handler thread
 * There's one of these per UVC context.
 * @todo We shouldn't run this if we don't own the USB context
 */
void *_uvc_handle_events(void *arg) {
    uvc_context_t *ctx = (uvc_context_t *) arg;
    LOGD("_uvc_handle_events");
#if defined(__ANDROID__)
    // try to increase thread priority
    int prio = getpriority(PRIO_PROCESS, 0);
    nice(-18);
    if (UNLIKELY(getpriority(PRIO_PROCESS, 0) >= prio)) {
        LOGD("could not change thread priority");
    }
#endif
    for (; !ctx->kill_handler_thread ;)
        libusb_handle_events(ctx->usb_ctx);
    return NULL;
}

/**
 * @internal
 * @brief Spawns a handler thread for the context
 * @ingroup init
 *
 * This should be called at the end of a successful uvc_open if no devices
 * are already open (and being handled).
 */
void uvc_start_handler_thread(uvc_context_t *ctx) {
    if (ctx->own_usb_ctx) {
        pthread_create(&ctx->handler_thread, NULL, _uvc_handle_events, (void*) ctx);
    }
}

int findJpegSegment(unsigned char* a, int dataLen, int segmentType) {
    int p = 2;
    while (p <= dataLen - 6) {
        if ((a[p] & 0xff) != 0xff) {
            LOGD("Unexpected JPEG data structure (marker expected).");
            break;
        }
        int markerCode = a[p + 1] & 0xff;
        if (markerCode == segmentType) {
            return p;
        }
        if (markerCode >= 0xD0 && markerCode <= 0xDA) {       // stop when scan data begins
            break;
        }
        int len = ((a[p + 2] & 0xff) << 8) + (a[p + 3] & 0xff);
        p += len + 2;
    }
    return -1;
}

// see USB video class standard, USB_Video_Payload_MJPEG_1.5.pdf
unsigned char * convertMjpegFrameToJpeg(unsigned char* frameData, int data_size,  int* jpglength_ptr) {
    int frameLen = data_size;
    while (frameLen > 0 && frameData[frameLen - 1] == 0) {
        frameLen--;
    }
    if (frameLen < 100 || (frameData[0] & 0xff) != 0xff || (frameData[1] & 0xff) != 0xD8 || (frameData[frameLen - 2] & 0xff) != 0xff || (frameData[frameLen - 1] & 0xff) != 0xd9) {
        LOGD("Invalid MJPEG frame structure, length= %d", data_size);
    }
    bool hasHuffmanTable = findJpegSegment(frameData, frameLen, 0xC4) != -1;
    exit_stream = false;
    if (hasHuffmanTable) {
        if (data_size == frameLen) {
            *jpglength_ptr = data_size;
            return frameData;
        }
        unsigned char *dest =  (unsigned char*)malloc(sizeof(unsigned char)*frameLen);
        memcpy(dest, frameData, frameLen);
        *jpglength_ptr = frameLen;
        return dest;
    } else {
        int segmentDaPos = findJpegSegment(frameData, frameLen, 0xDA);

        if (segmentDaPos == -1) {
            exit_stream = true;
            LOGD("Segment 0xDA not found in MJPEG frame data.");}
        //          throw new Exception("Segment 0xDA not found in MJPEG frame data.");
        if (exit_stream ==false) {
            int huflength = sizeof(mjpgHuffmanTable) / sizeof(unsigned char);
            int newlength = frameLen + huflength;
            unsigned char * a = (unsigned char*)malloc(sizeof(unsigned char)*newlength);
            memcpy(a, frameData,  segmentDaPos);
            memcpy(a + segmentDaPos, mjpgHuffmanTable,  huflength);
            memcpy(a + segmentDaPos + huflength, frameData + segmentDaPos,  frameLen - segmentDaPos);
            *jpglength_ptr = newlength;
            return a;
        } else
            return NULL;
    }
}


/** @brief Print a message explaining an error in the UVC driver
 * @ingroup diag
 *
 * @param err UVC error code
 * @param msg Optional custom message, prepended to output
 */
void uvc_perror(uvc_error_t err, const char *msg) {
    if (msg && *msg) {
        LOGD("%s: (%d)\n", msg, err);
    } else {
        LOGD("(%d)\n", err);
    }
}

/** @brief Free a frame structure
 * @ingroup frame
 *
 * @param frame Frame to destroy
 */
void uvc_free_frame(uvc_frame_t *frame) {
    if ((frame->data_bytes > 0) && frame->library_owns_data)
        free(frame->data);

    free(frame);
}

/** @internal */
uvc_error_t uvc_ensure_frame_size(uvc_frame_t *frame, size_t need_bytes) {
    if LIKELY(frame->library_owns_data) {
        if UNLIKELY(!frame->data || frame->data_bytes != need_bytes) {
            frame->actual_bytes = frame->data_bytes = need_bytes;	// XXX
            frame->data = realloc(frame->data, frame->data_bytes);
        }
        if (UNLIKELY(!frame->data || !need_bytes))
            return UVC_ERROR_NO_MEM;
        return UVC_SUCCESS;
    } else {
        if (UNLIKELY(!frame->data || frame->data_bytes < need_bytes))
            return UVC_ERROR_NO_MEM;
        return UVC_SUCCESS;
    }
}

/** @brief Convert a frame from YUYV to RGBX8888
 * @ingroup frame
 * @param ini YUYV frame
 * @param out RGBX8888 frame
 */
uvc_error_t uvc_yuyv2rgbx(uvc_frame_t *out) {
    out->width = imageWidth;
    out->height = imageHeight;
    out->frame_format = UVC_FRAME_FORMAT_RGBX;
    if (out->library_owns_data)
        out->step = imageWidth * PIXEL_RGBX;
    out->sequence = 0;
    gettimeofday(&out->capture_time, NULL);
    uint8_t *pyuv = videoFrameData[whichVideoFrameDate]->videoframe;
    const uint8_t *pyuv_end = pyuv + (imageWidth*imageHeight*2) - PIXEL8_YUYV;
    uint8_t *prgbx = out->data;
    const uint8_t *prgbx_end = prgbx + out->data_bytes - PIXEL8_RGBX;

    // YUYV => RGBX8888
#if USE_STRIDE
    if ((imageWidth * 3/2) && out->step && ((imageWidth * 3/2) != out->step)) {
		const int hh = imageHeight < out->height ? imageWidth : out->height;
		const int ww = imageWidth < out->width ? imageWidth : out->width;
		int h, w;
		for (h = 0; h < hh; h++) {
			w = 0;
			pyuv = videoFrameData[whichVideoFrameDate]->videoframe + (imageWidth * 3/2) * h;
			prgbx = out->data + out->step * h;
			for (; (prgbx <= prgbx_end) && (pyuv <= pyuv_end) && (w < ww) ;) {
				IYUYV2RGBX_8(pyuv, prgbx, 0, 0);

				prgbx += PIXEL8_RGBX;
				pyuv += PIXEL8_YUYV;
				w += 8;
			}
		}
	} else {
		// compressed format? XXX if only one of the frame in / out has step, this may lead to crash...
		for (; (prgbx <= prgbx_end) && (pyuv <= pyuv_end) ;) {
			IYUYV2RGBX_8(pyuv, prgbx, 0, 0);

			prgbx += PIXEL8_RGBX;
			pyuv += PIXEL8_YUYV;
		}
	}
#else
    for (; (prgbx <= prgbx_end) && (pyuv <= pyuv_end) ;) {
        IYUYV2RGBX_8(pyuv, prgbx, 0, 0);

        prgbx += PIXEL8_RGBX;
        pyuv += PIXEL8_YUYV;
    }
#endif
    return UVC_SUCCESS;
}

/** @brief Convert a frame from UYVY to RGBX8888
 * @ingroup frame
 * @param ini UYVY frame
 * @param out RGBX8888 frame
 */
uvc_error_t uvc_uyvy2rgbx(unsigned char* data, int data_bytes, int width, int height, uvc_frame_t *out) {
    out->width = width;
    out->height = height;
    out->frame_format = UVC_FRAME_FORMAT_RGBX;
    out->step = width * PIXEL_RGBX;
    out->sequence = 0;
    gettimeofday(&out->capture_time, NULL);
    uint8_t *pyuv = data;
    const uint8_t *pyuv_end = pyuv + data_bytes - PIXEL8_UYVY;
    uint8_t *prgbx = out->data;
    const uint8_t *prgbx_end = prgbx + out->data_bytes - PIXEL8_RGBX;
    // UYVY => RGBX8888
#if USE_STRIDE
    if ((imageWidth * 3/2) && out->step && ((imageWidth * 3/2) != out->step)) {
		const int hh = height < out->height ? height : out->height;
		const int ww = width < out->width ? width : out->width;
		int h, w;
		for (h = 0; h < hh; h++) {
			w = 0;
			pyuv = data + (imageWidth * 3/2) * h;
			prgbx = out->data + out->step * h;
			for (; (prgbx <= prgbx_end) && (pyuv <= pyuv_end) && (w < ww) ;) {
				IUYVY2RGBX_8(pyuv, prgbx, 0, 0);

				prgbx += PIXEL8_RGBX;
				pyuv += PIXEL8_UYVY;
				w += 8;
			}
		}
	} else {
		// compressed format? XXX if only one of the frame in / out has step, this may lead to crash...
		for (; (prgbx <= prgbx_end) && (pyuv <= pyuv_end) ;) {
			IUYVY2RGBX_8(pyuv, prgbx, 0, 0);

			prgbx += PIXEL8_RGBX;
			pyuv += PIXEL8_UYVY;
		}
	}
#else
    for (; (prgbx <= prgbx_end) && (pyuv <= pyuv_end) ;) {
        IUYVY2RGBX_8(pyuv, prgbx, 0, 0);
        prgbx += PIXEL8_RGBX;
        pyuv += PIXEL8_UYVY;
    }
#endif
    return UVC_SUCCESS;
}

/** @brief Convert a frame from YUYV to RGBX8888
 * @ingroup frame
 * @param ini YUYV frame
 * @param out RGBX8888 frame
 */
uvc_error_t uvc_yuyv2_rgbx(unsigned char* data, int data_bytes, int width, int height, uvc_frame_t *out) {
    out->width = width;
    out->height = height;
    out->frame_format = UVC_FRAME_FORMAT_RGBX;
    out->step = width * PIXEL_RGBX;
    out->sequence = 0;
    gettimeofday(&out->capture_time, NULL);
    uint8_t *pyuv = data;
    const uint8_t *pyuv_end = pyuv + data_bytes - PIXEL8_YUYV;
    uint8_t *prgbx = out->data;
    const uint8_t *prgbx_end = prgbx + out->data_bytes - PIXEL8_RGBX;

    // YUYV => RGBX8888
#if USE_STRIDE
    if ((imageWidth * 3/2) && out->step && ((imageWidth * 3/2) != out->step)) {
		const int hh = height < out->height ? height : out->height;
		const int ww = width < out->width ? width : out->width;
		int h, w;
		for (h = 0; h < hh; h++) {
			w = 0;
			pyuv = data + (imageWidth * 3/2) * h;
			prgbx = out->data + out->step * h;
			for (; (prgbx <= prgbx_end) && (pyuv <= pyuv_end) && (w < ww) ;) {
				IYUYV2RGBX_8(pyuv, prgbx, 0, 0);

				prgbx += PIXEL8_RGBX;
				pyuv += PIXEL8_YUYV;
				w += 8;
			}
		}
	} else {
		// compressed format? XXX if only one of the frame in / out has step, this may lead to crash...
		for (; (prgbx <= prgbx_end) && (pyuv <= pyuv_end) ;) {
			IYUYV2RGBX_8(pyuv, prgbx, 0, 0);

			prgbx += PIXEL8_RGBX;
			pyuv += PIXEL8_YUYV;
		}
	}
#else
    for (; (prgbx <= prgbx_end) && (pyuv <= pyuv_end) ;) {
        IYUYV2RGBX_8(pyuv, prgbx, 0, 0);

        prgbx += PIXEL8_RGBX;
        pyuv += PIXEL8_YUYV;
    }
#endif
    return UVC_SUCCESS;
}


/** @brief Convert a frame from UYVY to RGB888
 * @ingroup frame
 * @param ini UYVY frame
 * @param out RGB888 frame
 */
uvc_error_t uvc_uyvy2rgb(unsigned char* data, int data_bytes, int width, int height, uvc_frame_t *out) {
    out->width = width;
    out->height = height;
    out->frame_format = UVC_FRAME_FORMAT_RGB;
    out->step = width * PIXEL_RGB;
    out->sequence = 0;
    gettimeofday(&out->capture_time, NULL);

    uint8_t *pyuv = data;
    const uint8_t *pyuv_end = pyuv + data_bytes - PIXEL8_UYVY;
    uint8_t *prgb = out->data;
    const uint8_t *prgb_end = prgb + out->data_bytes - PIXEL8_RGB;

    // UYVY => RGB888
#if USE_STRIDE
    if ((imageWidth * 3/2) && out->step && ((imageWidth * 3/2) != out->step)) {
		const int hh = height < out->height ? height : out->height;
		const int ww = width < out->width ? width : out->width;
		int h, w;
		for (h = 0; h < hh; h++) {
			w = 0;
			pyuv = data + (imageWidth * 3/2) * h;
			prgb = out->data + out->step * h;
			for (; (prgb <= prgb_end) && (pyuv <= pyuv_end) && (w < ww) ;) {
				IUYVY2RGB_8(pyuv, prgb, 0, 0);

				prgb += PIXEL8_RGB;
				pyuv += PIXEL8_UYVY;
				w += 8;
			}
		}
	} else {
		// compressed format? XXX if only one of the frame in / out has step, this may lead to crash...
		for (; (prgb <= prgb_end) && (pyuv <= pyuv_end) ;) {
			IUYVY2RGB_8(pyuv, prgb, 0, 0);

			prgb += PIXEL8_RGB;
			pyuv += PIXEL8_UYVY;
		}
	}
#else
    for (; (prgb <= prgb_end) && (pyuv <= pyuv_end) ;) {
        IUYVY2RGB_8(pyuv, prgb, 0, 0);
        prgb += PIXEL8_RGB;
        pyuv += PIXEL8_UYVY;
    }
#endif
    return UVC_SUCCESS;
}

/** @brief Convert a frame from YUYV to RGB888
 * @ingroup frame
 *
 * @param in YUYV frame
 * @param out RGB888 frame
 */
uvc_error_t uvc_yuyv2rgb(unsigned char* data, int data_bytes, int width, int height, uvc_frame_t *out) {
    out->width = width;
    out->height = height;
    out->frame_format = UVC_FRAME_FORMAT_RGB;
    out->step = width * PIXEL_RGB;
    out->sequence = 0;
    gettimeofday(&out->capture_time, NULL);
    uint8_t *pyuv = data;
    const uint8_t *pyuv_end = pyuv + data_bytes - PIXEL8_YUYV;
    uint8_t *prgb = out->data;
    const uint8_t *prgb_end = prgb + out->data_bytes - PIXEL8_RGB;
#if USE_STRIDE
    if ((imageWidth * 3/2) && out->step && ((imageWidth * 3/2) != out->step)) {
		const int hh = height < out->height ? height : out->height;
		const int ww = width < out->width ? width : out->width;
		int h, w;
		for (h = 0; h < hh; h++) {
			w = 0;
			pyuv = data + (imageWidth * 3/2) * h;
			prgb = out->data + out->step * h;
			for (; (prgb <= prgb_end) && (pyuv <= pyuv_end) && (w < ww) ;) {
				IYUYV2RGB_8(pyuv, prgb, 0, 0);

				prgb += PIXEL8_RGB;
				pyuv += PIXEL8_YUYV;
				w += 8;
			}
		}
	} else {
		// compressed format? XXX if only one of the frame in / out has step, this may lead to crash...
		for (; (prgb <= prgb_end) && (pyuv <= pyuv_end) ;) {
			IYUYV2RGB_8(pyuv, prgb, 0, 0);

			prgb += PIXEL8_RGB;
			pyuv += PIXEL8_YUYV;
		}
	}
#else
    // YUYV => RGB888
    for (; (prgb <= prgb_end) && (pyuv <= pyuv_end) ;) {
        IYUYV2RGB_8(pyuv, prgb, 0, 0);

        prgb += PIXEL8_RGB;
        pyuv += PIXEL8_YUYV;
    }
#endif
    return UVC_SUCCESS;
}

/** @brief Convert a frame from RGB888 to RGBX8888
 * @ingroup frame
 * @param ini RGB888 frame
 * @param out RGBX8888 frame
 */
uvc_error_t uvc_rgb2rgbx(unsigned char* data, int data_bytes, int width, int height, uvc_frame_t *out) {
    out->width = width;
    out->height = height;
    out->frame_format = UVC_FRAME_FORMAT_RGBX;
    if (out->library_owns_data)
        out->step = width * PIXEL_RGBX;
    out->sequence = 0;
    gettimeofday(&out->capture_time, NULL);
    uint8_t *prgb = data;
    const uint8_t *prgb_end = prgb + data_bytes - PIXEL8_RGB;
    uint8_t *prgbx = out->data;
    const uint8_t *prgbx_end = prgbx + out->data_bytes - PIXEL8_RGBX;
    // RGB888 to RGBX8888
#if USE_STRIDE
    if (width * PIXEL_RGB && out->step && (width * PIXEL_RGB != out->step)) {
		const int hh = height < out->height ? height : out->height;
		const int ww = width < out->width ? width : out->width;
		int h, w;
		for (h = 0; h < hh; h++) {
			w = 0;
			prgb = data + width * PIXEL_RGB * h;
			prgbx = out->data + out->step * h;
			for (; (prgbx <= prgbx_end) && (prgb <= prgb_end) && (w < ww) ;) {
				RGB2RGBX_8(prgb, prgbx, 0, 0);

				prgb += PIXEL8_RGB;
				prgbx += PIXEL8_RGBX;
				w += 8;
			}
		}
	} else {
		// compressed format? XXX if only one of the frame in / out has step, this may lead to crash...
		for (; (prgbx <= prgbx_end) && (prgb <= prgb_end) ;) {
			RGB2RGBX_8(prgb, prgbx, 0, 0);

			prgb += PIXEL8_RGB;
			prgbx += PIXEL8_RGBX;
		}
	}
#else
    for (; (prgbx <= prgbx_end) && (prgb <= prgb_end) ;) {
        RGB2RGBX_8(prgb, prgbx, 0, 0);

        prgb += PIXEL8_RGB;
        prgbx += PIXEL8_RGBX;
    }
#endif
    return UVC_SUCCESS;
}

/** @brief Convert a frame from MJPEG to RGB8888
 * @ingroup frame
 * @param out RGB888 frame
 */
unsigned char* uvc_mjpeg2rgb(int* rgblength_ptr) {

    unsigned char *jpg_buffer;
    int jpeglength = total;
    int* jpglength_ptr = &jpeglength;
    jpg_buffer = convertMjpegFrameToJpeg(videoFrameData[whichVideoFrameDate]->videoframe, total, jpglength_ptr);
    //LOGD("jpg length jpglength_ptr = %d", *jpglength_ptr);
    unsigned long jpg_size = *jpglength_ptr;
    struct jpeg_decompress_struct cinfo;
    struct jpeg_error_mgr jerr;
    unsigned long rgb_size;
    unsigned char *rgb_buffer;
    int row_stride, width, height, pixel_size;
    cinfo.err = jpeg_std_error(&jerr);
    jpeg_create_decompress(&cinfo);
    jpeg_mem_src(&cinfo, jpg_buffer, jpg_size);
    int rc = jpeg_read_header(&cinfo, TRUE);
    if (rc != 1) {
        LOGD("File does not seem to be a normal JPEG");
        exit(EXIT_FAILURE);
    }
    jpeg_start_decompress(&cinfo);
    width = cinfo.output_width;
    height = cinfo.output_height;
    pixel_size = cinfo.output_components;
    //LOGD( "Proc: Image is %d by %d with %d components",width, height, pixel_size);
    rgb_size = width * height * pixel_size;
    rgb_buffer = (unsigned char*) malloc(rgb_size);
    row_stride = width * pixel_size;
    while (cinfo.output_scanline < cinfo.output_height) {
        unsigned char *buffer_array[1];
        buffer_array[0] = rgb_buffer + \
						   (cinfo.output_scanline) * row_stride;
        jpeg_read_scanlines(&cinfo, buffer_array, 1);
    }
    jpeg_finish_decompress(&cinfo);
    jpeg_destroy_decompress(&cinfo);
    *rgblength_ptr = rgb_size;
    return rgb_buffer;
}


/** @brief Allocate a frame structure
 * @ingroup frame
 *
 * @param data_bytes Number of bytes to allocate, or zero
 * @return New frame, or NULL on error
 */
uvc_frame_t *uvc_allocate_frame(size_t data_bytes) {
    uvc_frame_t *frame = malloc(sizeof(*frame));	// FIXME using buffer pool is better performance(5-30%) than directory use malloc everytime.

    if (UNLIKELY(!frame))
        return NULL;

#ifndef __ANDROID__
    // XXX in many case, it is not neccesary to clear because all fields are set before use
	// therefore we remove this to improve performace, but be care not to forget to set fields before use
	memset(frame, 0, sizeof(*frame));	// bzero(frame, sizeof(*frame)); // bzero is deprecated
#endif
//	frame->library_owns_data = 1;	// XXX moved to lower

    if (LIKELY(data_bytes > 0)) {
        frame->library_owns_data = 1;
        frame->actual_bytes = frame->data_bytes = data_bytes;	// XXX
        frame->data = malloc(data_bytes);

        if (UNLIKELY(!frame->data)) {
            free(frame);
            return NULL ;
        }
    }

    return frame;
}


/** @brief Duplicate a frame, preserving color format
 * @ingroup frame
 *
 * @param in Original frame
 * @param out Duplicate frame
 */
uvc_error_t uvc_duplicate_frame(uvc_frame_t *out) {

    out->width = imageWidth;
    out->height = imageHeight;
    out->frame_format = UVC_FRAME_FORMAT_YUYV;
    if (out->library_owns_data)
        out->step = imageWidth*2;
    out->sequence = 0;
    gettimeofday(&out->capture_time, NULL);
    out->actual_bytes = total;	// XXX

#if USE_STRIDE	 // XXX
    if (in->step && out->step) {
		const int istep = imageWidth*2;
		const int ostep = out->step;
		const int hh = imageHeight < out->height ? imageHeight : out->height;
		const int rowbytes = istep < ostep ? istep : ostep;
		register void *ip = videoFrameData->videoframe;
		register void *op = out->data;
		int h;
		for (h = 0; h < hh; h += 4) {
			memcpy(op, ip, rowbytes);
			ip += istep; op += ostep;
			memcpy(op, ip, rowbytes);
			ip += istep; op += ostep;
			memcpy(op, ip, rowbytes);
			ip += istep; op += ostep;
			memcpy(op, ip, rowbytes);
			ip += istep; op += ostep;
		}
	} else {
		// compressed format? XXX if only one of the frame in / out has step, this may lead to crash...
		memcpy(out->data, in->data, in->actual_bytes);
	}
#else
    memcpy(out->data, videoFrameData[whichVideoFrameDate]->videoframe, total); // XXX
#endif
    return UVC_SUCCESS;
}

uvc_error_t uvc_yuyv2iyuv420SP(uvc_frame_t *in, uvc_frame_t *out) {
    if (UNLIKELY(uvc_ensure_frame_size(out, (in->width * in->height * 3) / 2) < 0))
        return UVC_ERROR_NO_MEM;

    const uint8_t *src = in->data;
    uint8_t *dest =out->data;
    const int32_t width = in->width;
    const int32_t height = in->height;
    const int32_t src_width = in->step;
    const int32_t src_height = in->height;
    const int32_t dest_width = out->width = out->step = in->width;
    const int32_t dest_height = out->height = in->height;

    const uint32_t hh = src_height < dest_height ? src_height : dest_height;
    uint8_t *uv = dest + dest_width * dest_height;
    int h, w;
    for (h = 0; h < hh - 1; h += 2) {
        uint8_t *y0 = dest + width * h;
        uint8_t *y1 = y0 + width;
        const uint8_t *yuv = src + src_width * h;
        for (w = 0; w < width; w += 4) {
            *(y0++) = yuv[0];	// y
            *(y0++) = yuv[2];	// y'
            *(y0++) = yuv[4];	// y''
            *(y0++) = yuv[6];	// y'''
            *(uv++) = yuv[3];	// v
            *(uv++) = yuv[1];	// u
            *(uv++) = yuv[7];	// v
            *(uv++) = yuv[5];	// u
            *(y1++) = yuv[src_width+0];	// y on next low
            *(y1++) = yuv[src_width+2];	// y' on next low
            *(y1++) = yuv[src_width+4];	// y''  on next low
            *(y1++) = yuv[src_width+6];	// y'''  on next low
            yuv += 8;	// (1pixel=2bytes)x4pixels=8bytes
        }
    }

    return(UVC_SUCCESS);
}

////////////////////////////////////////////////////////////////////////// YUV Methods

int rotation = 0;
volatile bool horizontalFlip = false;
volatile bool verticalFlip = false;
volatile bool imageCapture = false;
volatile bool imageCapturelongClick = false;

volatile bool videoCapture = false;
volatile bool videoCaptureLongClick = false;


void setRotation(int rot, int horizontalFl, int verticalFl) {
    rotation = rot;
    if (horizontalFl == 1) horizontalFlip = true;
    else horizontalFlip = false;
    if (verticalFl == 1) verticalFlip = true;
    else verticalFlip = false;
    LOGD("Rotation SET !!!!! horizontalFlip = %d,    verticalFlip = %d", horizontalFl, verticalFl);
}

int ARGBCopy(const uint8_t* src_argb,
             int src_stride_argb,
             uint8_t* dst_argb,
             int dst_stride_argb,
             int width,
             int height) {
    if (!src_argb || !dst_argb || width <= 0 || height == 0) {
        return -1;
    }
    // Negative height means invert the image.
    if (height < 0) {
        height = -height;
        src_argb = src_argb + (height - 1) * src_stride_argb;
        src_stride_argb = -src_stride_argb;
    }

    CopyPlane(src_argb, src_stride_argb, dst_argb, dst_stride_argb, width * 4,
              height);
    return 0;
}

void ScaleARGBRowDownEven_C(const uint8_t* src_argb,
                            ptrdiff_t src_stride,
                            int src_stepx,
                            uint8_t* dst_argb,
                            int dst_width) {
    const uint32_t* src = (const uint32_t*)(src_argb);
    uint32_t* dst = (uint32_t*)(dst_argb);
    (void)src_stride;
    int x;
    for (x = 0; x < dst_width - 1; x += 2) {
        dst[0] = src[0];
        dst[1] = src[src_stepx];
        src += src_stepx * 2;
        dst += 2;
    }
    if (dst_width & 1) {
        dst[0] = src[0];
    }
}

void flip_vertically(uvc_frame_t *img)
{
    const size_t bytes_per_pixel = PIXEL_RGBX ;
    // stride = Ausschreitung
    const size_t stride = img->width * bytes_per_pixel;
    unsigned char *row = malloc(stride);
    unsigned char *low = img->data;
    unsigned char *high = &img->data[(img->height - 1) * stride];
    for (; low < high; low += stride, high -= stride) {
        memcpy(row, low, stride);
        memcpy(low, high, stride);
        memcpy(high, row, stride);
    }
    free(row);
}

int ARGBMirror(const uint8_t* src_argb,
               int src_stride_argb,
               uint8_t* dst_argb,
               int dst_stride_argb,
               int width,
               int height) {
    int y;
    void (*ARGBMirrorRow)(const uint8_t* src, uint8_t* dst, int width) =
    ARGBMirrorRow_C;
    if (!src_argb || !dst_argb || width <= 0 || height == 0) {
        return -1;
    }
    // Negative height means invert the image.
    if (height < 0) {
        height = -height;
        src_argb = src_argb + (height - 1) * src_stride_argb;
        src_stride_argb = -src_stride_argb;
    }
#if defined(HAS_ARGBMIRRORROW_NEON)
    if (TestCpuFlag(kCpuHasNEON)) {
    ARGBMirrorRow = ARGBMirrorRow_Any_NEON;
    if (IS_ALIGNED(width, 8)) {
      ARGBMirrorRow = ARGBMirrorRow_NEON;
    }
  }
#endif
#if defined(HAS_ARGBMIRRORROW_SSE2)
    if (TestCpuFlag(kCpuHasSSE2)) {
    ARGBMirrorRow = ARGBMirrorRow_Any_SSE2;
    if (IS_ALIGNED(width, 4)) {
      ARGBMirrorRow = ARGBMirrorRow_SSE2;
    }
  }
#endif
#if defined(HAS_ARGBMIRRORROW_AVX2)
    if (TestCpuFlag(kCpuHasAVX2)) {
    ARGBMirrorRow = ARGBMirrorRow_Any_AVX2;
    if (IS_ALIGNED(width, 8)) {
      ARGBMirrorRow = ARGBMirrorRow_AVX2;
    }
  }
#endif
#if defined(HAS_ARGBMIRRORROW_MMI)
    if (TestCpuFlag(kCpuHasMMI)) {
    ARGBMirrorRow = ARGBMirrorRow_Any_MMI;
    if (IS_ALIGNED(width, 2)) {
      ARGBMirrorRow = ARGBMirrorRow_MMI;
    }
  }
#endif
#if defined(HAS_ARGBMIRRORROW_MSA)
    if (TestCpuFlag(kCpuHasMSA)) {
    ARGBMirrorRow = ARGBMirrorRow_Any_MSA;
    if (IS_ALIGNED(width, 16)) {
      ARGBMirrorRow = ARGBMirrorRow_MSA;
    }
  }
#endif

    // Mirror plane
    for (y = 0; y < height; ++y) {
        ARGBMirrorRow(src_argb, dst_argb, width);
        src_argb += src_stride_argb;
        dst_argb += dst_stride_argb;
    }
    return 0;
}

uvc_frame_t *flip_horizontal(uvc_frame_t *frame) {

    uvc_frame_t *flip_img = uvc_allocate_frame(imageWidth * imageHeight * 4);
    flip_img->frame_format = UVC_FRAME_FORMAT_RGBX;
    if (flip_img->library_owns_data)
        flip_img->step = imageWidth * PIXEL_RGBX;
    flip_img->sequence = 0;
    flip_img->capture_time = frame->capture_time;
    int src_stride_argb = frame->width * 4;
    uint* dst_argb = flip_img->data;
    int width = frame->width;
    int height = frame->height;
    int dst_stride_argb = frame->width * 4;
    flip_img->width = frame->width ;
    flip_img->height = frame->height;
    ARGBMirror(frame->data, src_stride_argb,
    dst_argb, dst_stride_argb,
    width, height);
    uvc_free_frame(frame);
    return flip_img;
}

uvc_frame_t *checkFlip(uvc_frame_t *rot_img) {
    if (!verticalFlip && !horizontalFlip) return rot_img;
    else if (verticalFlip && horizontalFlip) flip_vertically(rot_img);
    if (verticalFlip) { flip_vertically(rot_img); return flip_horizontal(rot_img); }
    if (horizontalFlip) return flip_horizontal(rot_img);
    return rot_img;
}

uvc_frame_t *checkRotation(uvc_frame_t *rgbx) {
    if (rotation == 0) return checkFlip(rgbx);
    uvc_frame_t *rot_img = uvc_allocate_frame(imageWidth * imageHeight * 4);
    rot_img->frame_format = UVC_FRAME_FORMAT_RGBX;
    if (rot_img->library_owns_data)
        rot_img->step = imageWidth * PIXEL_RGBX;
    rot_img->sequence = 0;
    rot_img->capture_time = rgbx->capture_time;
    int src_stride_argb = imageWidth * 4;
    uint* dst_argb = rot_img->data;
    int src_width = imageWidth;
    int src_height = imageHeight;

    if (rotation == 90) {
        rot_img->width = imageHeight ;
        rot_img->height = imageWidth;
        int dst_stride_argb = imageHeight * 4;
        enum RotationMode mode = kRotate90;
        ARGBRotate(rgbx->data,  src_stride_argb,
                   rot_img->data, dst_stride_argb,
                   src_width, src_height,
                   mode);
        uvc_free_frame(rgbx);
        return checkFlip(rot_img);
    } else if (rotation == 180) {
        rot_img->width = imageWidth;
        rot_img->height = imageHeight;
        int dst_stride_argb = imageWidth * 4;
        enum RotationMode mode = kRotate180;
        ARGBRotate(rgbx->data,  src_stride_argb,
                   rot_img->data, dst_stride_argb,
                   src_width, src_height,
                   mode);
        uvc_free_frame(rgbx);
        return checkFlip(rot_img);
    } else if (rotation == 270) {
        rot_img->width = imageHeight ;
        rot_img->height = imageWidth;
        int dst_stride_argb = imageHeight * 4;
        enum RotationMode mode = kRotate270;
        ARGBRotate(rgbx->data,  src_stride_argb,
                   rot_img->data, dst_stride_argb,
                   src_width, src_height,
                   mode);
        uvc_free_frame(rgbx);
        return checkFlip(rot_img);
    }
    return rot_img;
}

///////////////////////////////////////////////////   STANDARD CAMERA FUNCTIONS  /////////////////////////////////////////

void initStreamingParms_controltransfer(libusb_device_handle *handle, bool createPointer) {
    LOGD("bool createPointer = %d", createPointer);
    size_t length;
    if (bcdUVC >= 0x0150)
        length = 48;
    else if (bcdUVC >= 0x0110)
        length = 34;
    else
        length = 26;
    LOGD("length = %d", length);
    if (createPointer == true) {
        ctl_transfer_Data = malloc(sizeof *ctl_transfer_Data + sizeof(unsigned char[48*4]));
        ctl_transfer_Data->BufferSize = 48 * 4;
        memset(ctl_transfer_Data->ctl_transfer_values, 0, sizeof(unsigned char) * (ctl_transfer_Data->BufferSize));
    }
    uint8_t buffer[length];
    for (i = 0; i < length; i++) {
        buffer[i] = 0x00;
    }
    buffer[0] = bmHint; // what fields shall be kept fixed (0x01: dwFrameInterval)
    buffer[1] = 0x00; //
    buffer[2] = camFormatIndex; // video format index
    buffer[3] = camFrameIndex; // video frame index
    buffer[4] = (camFrameInterval & 0xFF); // interval
    buffer[5] = ((camFrameInterval >> 8)& 0xFF); //   propose:   0x4c4b40 (500 ms)
    buffer[6] = ((camFrameInterval >> 16)& 0xFF); //   agreement: 0x1312d0 (125 ms)
    buffer[7] = ((camFrameInterval >> 24)& 0xFF); //
    int b;

    if (createPointer == true) {
        memcpy(ctl_transfer_Data->ctl_transfer_values, buffer , length);
    }
    getStreamingParmsArray(initStreamingParmsIntArray , buffer);
    // wanted Pharms
    LOGD("initStreamingParmsIntArray[0] = %d", initStreamingParmsIntArray[0]);
    LOGD("initStreamingParmsIntArray[1] = %d", initStreamingParmsIntArray[1]);
    LOGD("initStreamingParmsIntArray[2] = %d", initStreamingParmsIntArray[2]);
    int len = libusb_control_transfer(handle, RT_CLASS_INTERFACE_SET, SET_CUR, (VS_PROBE_CONTROL << 8), camStreamingInterfaceNum, buffer, sizeof (buffer), 2000);
    if (len != sizeof (buffer)) {
        LOGD("\nCamera initialization failed. Streaming parms probe set failed, len= %d.\n", len);
    } else {
        LOGD("1st: InitialContolTransfer Sucessful");
        LOGD("Camera initialization success, len= %d.\n", len);
    }
    len = libusb_control_transfer(handle, RT_CLASS_INTERFACE_GET, GET_CUR, (VS_PROBE_CONTROL << 8), camStreamingInterfaceNum, buffer, sizeof (buffer), 500);
    if (len != sizeof (buffer)) {
        LOGD("Camera initialization failed. Streaming parms probe set failed, len= %d.\n", len);
        if (createPointer == true) {
            memset(ctl_transfer_Data->ctl_transfer_values + 47, 0, length);
        }
    } else {
        if (createPointer == true) {
            memcpy(ctl_transfer_Data->ctl_transfer_values + 47, buffer, length);
        }
        LOGD ("2nd: CTL suressful");
    }

    getStreamingParmsArray(probedStreamingParmsIntArray , buffer);
/*
    LOGD("probedStreamingParmsIntArray[0] = %d", probedStreamingParmsIntArray[0]);
    LOGD("probedStreamingParmsIntArray[1] = %d", probedStreamingParmsIntArray[1]);
    LOGD("probedStreamingParmsIntArray[2] = %d", probedStreamingParmsIntArray[2]);

    LOGD("Probed Streaming Pharms:       ");
    for (int i = 0; i < sizeof(buffer); i++)
        if (buffer[i] != 0){
            LOGD("[%d ] ", buffer[i]);}
*/
    len = libusb_control_transfer(handle, RT_CLASS_INTERFACE_SET, SET_CUR, (VS_COMMIT_CONTROL << 8), camStreamingInterfaceNum, buffer, sizeof (buffer), 2000);
    if (len != sizeof (buffer)) {
        LOGD("FAILED -- 3rd CTL failed");
        LOGD("Camera initialization failed. Streaming parms commit set failed, len= %d.", len);
        if (createPointer == true) {
            memset(ctl_transfer_Data->ctl_transfer_values + 95, 0, length);
        }
    } else {
        if (createPointer == true) {
            memcpy(ctl_transfer_Data->ctl_transfer_values + 95, buffer, length);
        }
        LOGD("3rd: CTL Sucessful");
    }
    getStreamingParmsArray(finalStreamingParmsIntArray_first , buffer);
    len = libusb_control_transfer(handle, RT_CLASS_INTERFACE_GET, GET_CUR, (short) (VS_COMMIT_CONTROL << 8), camStreamingInterfaceNum, buffer, sizeof (buffer), 2000);
    if (len != sizeof (buffer)) {
        LOGD("ERROR 4th CTL FAILED");
        LOGD("Camera initialization failed. Streaming parms commit get failed, len= %d.", len);
        if (createPointer == true) {
            memset(ctl_transfer_Data->ctl_transfer_values + 143, 0, length);
        }
    } else {
        LOGD("4th: FinalCTL Sucessful");
        if (createPointer == true) {
            memcpy(ctl_transfer_Data->ctl_transfer_values + 143, buffer, length);
        }
    }
    getStreamingParmsArray(finalStreamingParmsIntArray , buffer);
}

void print_endpoint(const struct libusb_endpoint_descriptor *endpoint, int bInterfaceNumber) {
    int i, ret;
    if (bInterfaceNumber == 1) {
        streamEndPointAdressOverNative = endpoint->bEndpointAddress;
    }
    LOGD("        wMaxPacketSize:   %d\n", endpoint->wMaxPacketSize);
    fflush(stdout);
}

void print_altsetting(const struct libusb_interface_descriptor *interface) {
    uint8_t i;

    if (interface->bInterfaceSubClass == SC_VIDEOCONTROL) {
        int camControlInterfaceNum = interface->bInterfaceNumber;

    }

    if (interface->bInterfaceSubClass == SC_VIDEOSTREAMING) {
        camStreamingInterfaceNum = interface->bInterfaceNumber;

    }
    LOGD("  interface->bNumEndpoints = %d\n", interface->bNumEndpoints);

    for (i = 0; i < interface->bNumEndpoints; i++)
        print_endpoint(&interface->endpoint[i], interface->bInterfaceNumber);
}

void print_interface(const struct libusb_interface *interface) {
    int i;
    LOGD("  interface->num_altsetting = %d\n", interface->num_altsetting);
    for (i = 0; i < interface->num_altsetting; i++)
        print_altsetting(&interface->altsetting[i]);
}

void print_configuration(struct libusb_config_descriptor *config) {
    uint8_t i;
    LOGD("  Configuration:\n");
    LOGD("    wTotalLength:         %d\n", config->wTotalLength);
    LOGD("    bNumInterfaces:       %d\n", config->bNumInterfaces);
    LOGD("    bConfigurationValue:  %d\n", config->bConfigurationValue);
    LOGD("    iConfiguration:       %d\n", config->iConfiguration);
    LOGD("    bmAttributes:         %02xh\n", config->bmAttributes);
    LOGD("    MaxPower:             %d\n\n", config->MaxPower);
    fflush(stdout);


    for (i = 0; i < config->bNumInterfaces; i++)
        // printf("                 Interface Nummer:       %d\n\n", i);
        print_interface(&config->interface[i]);
    fflush(stdout);

}

int print_device(libusb_device *dev, int level, libusb_device_handle *handle, struct libusb_device_descriptor desc) {
    char description[256];
    char string[256];
    int ret;
    uint8_t i;
    LOGD("Print Device");
    verbose = 1;
    ret = libusb_get_device_descriptor(dev, &desc);
    if (ret < 0) {
        LOGD(stderr, "failed to get device descriptor");
        return -1;
    }

        LOGD("\nKamera gefunden\n");
        fflush(stdout);
        ret = libusb_open(dev, &handle);
        if (LIBUSB_SUCCESS == ret) {
            if (desc.iManufacturer) {
                ret = libusb_get_string_descriptor_ascii(handle, desc.iManufacturer, string, sizeof (string));
                if (ret > 0) {
                    snprintf(description, sizeof (description), "%s - ", string);
                } else
                    snprintf(description, sizeof (description), "%04X - ", desc.idVendor);
            } else
                snprintf(description, sizeof (description), "%04X - ",
                         desc.idVendor);
            if (desc.iProduct) {
                ret = libusb_get_string_descriptor_ascii(handle, desc.iProduct, string, sizeof (string));
                if (ret > 0)
                    snprintf(description + strlen(description), sizeof (description) -
                                                                strlen(description), "%s", string);
                else
                    snprintf(description + strlen(description), sizeof (description) -
                                                                strlen(description), "%04X", desc.idProduct);
            } else
                snprintf(description + strlen(description), sizeof (description) -
                                                            strlen(description), "%04X", desc.idProduct);
        } else {
            snprintf(description, sizeof (description), "%04X - %04X",
                     desc.idVendor, desc.idProduct);
        }
        LOGD("%.*sDev (bus %d, device %d): %s\n", level * 2, "                    ", libusb_get_bus_number(dev), libusb_get_device_address(dev), description);
        if (handle && verbose) {
            if (desc.iSerialNumber) {
                ret = libusb_get_string_descriptor_ascii(handle, desc.iSerialNumber, string, sizeof (string));
                if (ret > 0)
                    LOGD("%.*s  - Serial Number: %s\n", level * 2,
                           "                    ", string);
            }
        }
        if (verbose) {
            for (i = 0; i < desc.bNumConfigurations; i++) {
                struct libusb_config_descriptor *config;
                ret = libusb_get_config_descriptor(dev, i, &config);
                if (LIBUSB_SUCCESS != ret) {
                    LOGD("  Couldn't retrieve descriptors\n");
                    continue;
                }
                print_configuration(config);
                libusb_free_config_descriptor(config);
            }
        }
        return 0;
}

int wrap_camera_device(int FD, uvc_context_t **pctx, struct libusb_context *usb_ctx, uvc_device_handle_t **devh, uvc_device_t *dev) {
    uvc_error_t ret = UVC_SUCCESS;

    LOGD("wrap_camera_device()");

    uvc_context_t *ctx = calloc(1, sizeof(*ctx));
    uvc_device_handle_t *internal_devh;
    internal_devh = calloc(1, sizeof(*internal_devh));
    internal_devh->dev = dev;
    dev = calloc(1, sizeof(*dev));
    ret = libusb_set_option(ctx->usb_ctx, LIBUSB_OPTION_NO_DEVICE_DISCOVERY, NULL);
    if (ret != LIBUSB_SUCCESS) {
        __android_log_print(ANDROID_LOG_ERROR, TAG,"libusb_set_option failed: %d\n", ret);
        return -1;
    }
    ret = libusb_init(&ctx->usb_ctx);
    if (ret < 0) {
        __android_log_print(ANDROID_LOG_INFO, TAG,
                            "libusb_init failed: %d\n", ret);
        LOGD("Trying to initialize with NULL CTX");
        ret = libusb_init(NULL);
        if (ret < 0) {
            __android_log_print(ANDROID_LOG_INFO, TAG,
                                "libusb_init failed: %d\n", ret);
            LOGD("Trying to initialize with NULL CTX");
            return -1;
        } else LOGD("libusb_init only sucessful without Context");
    } else LOGD("libusb_init with Context sucessful");


    ctx->own_usb_ctx = 1;
    ret = libusb_wrap_sys_device(ctx->usb_ctx, (intptr_t)FD, &internal_devh->usb_devh);
    if (ret < 0) {
        __android_log_print(ANDROID_LOG_INFO, TAG,
                            "libusb_wrap_sys_device failed: %d\n", ret);
        return -2;
    }
    else if (internal_devh->usb_devh == NULL) {
        __android_log_print(ANDROID_LOG_INFO, TAG,
                            "libusb_wrap_sys_device returned invalid handle\n");
        return -3;
    }
    cameraDevice_is_wraped = 1;
    //dev->ctx = ctx;
    LOGD("get the device");
    dev->usb_dev = libusb_get_device(internal_devh->usb_devh);
    dev->ref++;	// これ排他制御要るんちゃうかなぁ(｡･_･｡)
    libusb_ref_device(dev->usb_dev);

    if (ctx->own_usb_ctx && ctx->open_devices == NULL) {
        /* Since this is our first device, we need to spawn the event handler thread */
        LOGD("Starting Handler Thread");
        uvc_start_handler_thread(ctx);
    }
    DL_APPEND(ctx->open_devices, internal_devh);

    *devh = internal_devh;

    if (ctx != NULL)
        *pctx = ctx;

    return 0;
}

int libUsb_open_def_fd(int vid, int pid, const char *serial, int FD, int busnum, int devaddr) {
    unsigned char data[64];
    if (!cameraDevice_is_wraped) {
        if (wrap_camera_device(FD, &uvcContext, NULL, &uvcDeviceHandle, mDevice) == 0) LOGD("Successfully wraped The CameraDevice");
        else return -1;
    }
    for (int if_num = 0; if_num < (camStreamingInterfaceNum + 1); if_num++) {
        if (libusb_kernel_driver_active(uvcDeviceHandle->usb_devh, if_num)) {
            libusb_detach_kernel_driver(uvcDeviceHandle->usb_devh, if_num);
        }
        int rc = libusb_claim_interface(uvcDeviceHandle->usb_devh, if_num);
        if (rc < 0) {
            LOGD(stderr, "Error claiming interface: %s\n",
                    libusb_error_name(rc));
        } else {
            LOGD("Interface %d erfolgreich eingehängt;\n", if_num);
        }
    }
    LOGD("Print Device");
    struct libusb_device_descriptor desc;
    print_device(libusb_get_device(uvcDeviceHandle->usb_devh) , 0, uvcDeviceHandle->usb_devh, desc);

    int r = libusb_set_interface_alt_setting(uvcDeviceHandle->usb_devh, camStreamingInterfaceNum, 0); // camStreamingAltSetting = 7;    // 7 = 3x1024 bytes packet size
    if (r != LIBUSB_SUCCESS) {
        LOGD("libusb_set_interface_alt_setting(uvcDeviceHandle->usb_devh, 1, 1) failed with error %d\n", r);
    } else {
        LOGD("Altsettings sucessfuly set! %d ; Altsetting = 0\n", r);
    }
    __android_log_print(ANDROID_LOG_INFO, TAG,
                        "libusb_control_transfer start\n");
    initStreamingParms(uvcDeviceHandle->usb_devh);
    __android_log_print(ANDROID_LOG_INFO, TAG, "uvcDeviceHandle->usb_devh: %p\n", uvcDeviceHandle->usb_devh);
    LOGD("Exit");
    return 0;
}

void native_uvc_unref_device() {

    if(cameraDevice_is_wraped){
        uvcContext->kill_handler_thread = 1;
        libusb_close(uvcDeviceHandle->usb_devh);
        pthread_join(uvcContext->handler_thread, NULL);
        libusb_unref_device(mDevice->usb_dev);
        mDevice->ref--;
        uvcContext->own_usb_ctx = 0;
        cameraDevice_is_wraped = 0;
    }
}

int set_the_native_Values (int FD, int packetsPerReques, int maxPacketSiz, int activeUrb, int camStreamingAltSettin, int camFormatInde,
           int camFrameInde, int camFrameInterva, int imageWidt, int imageHeigh, int camStreamingEndpointAdress, int camStreamingInterfaceNumber,
           const char* frameformat, int numberOfAutoFrame, int bcdUVC_int, int lowAndroid) {
    fd = FD;
    numberOfAutoFrames = numberOfAutoFrame;
    packetsPerRequest = packetsPerReques;
    maxPacketSize = maxPacketSiz;
    activeUrbs = activeUrb;
    camStreamingAltSetting = camStreamingAltSettin;
    camFormatIndex = camFormatInde;
    camFrameIndex = camFrameInde;
    camFrameInterval = camFrameInterva;
    camStreamingEndpoint = camStreamingEndpointAdress;
    camStreamingInterfaceNum = camStreamingInterfaceNumber;
    imageWidth = imageWidt;
    imageHeight = imageHeigh;
    frameFormat = frameformat;
    mUsbFs = mUsbFs;
    vendorID = vendorID;
    productID = productID;
    busnum = busnum;
    devaddr = devaddr;
    if (strcmp(frameFormat, "MJPEG") == 0) {
        for (int num = 0; num < 2; num++) {
            videoFrameData[num] = malloc(sizeof *videoFrameData + sizeof(char[imageWidt*imageHeigh*2]));
            videoFrameData[num]->FrameSize = imageWidt * imageHeigh;
            videoFrameData[num]->FrameBufferSize = videoFrameData[num]->FrameSize * 3;
        }
    } else if (strcmp(frameFormat, "YUY2") == 0 ||   strcmp(frameFormat, "UYVY")   ) {
        for (int num = 0; num < 2; num++) {
            videoFrameData[num] = malloc(sizeof *videoFrameData + sizeof(char[imageWidt*imageHeigh*2]));
            videoFrameData[num]->FrameSize = imageWidt * imageHeigh;
            videoFrameData[num]->FrameBufferSize = videoFrameData[num]->FrameSize * 2;
        }
    } else if (strcmp(frameFormat, "NV21") == 0) {
        for (int num = 0; num < 2; num++) {
            videoFrameData[num] = malloc(sizeof *videoFrameData + sizeof(char[imageWidt*imageHeigh*2]));
            videoFrameData[num]->FrameSize = imageWidt * imageHeigh;
            videoFrameData[num]->FrameBufferSize = videoFrameData[num]->FrameSize * 3 / 2;
        }
    } else {
        for (int num = 0; num < 2; num++) {
            videoFrameData[num] = malloc(sizeof *videoFrameData + sizeof(char[imageWidt*imageHeigh*2]));
            videoFrameData[num]->FrameSize = imageWidt * imageHeigh;
            videoFrameData[num]->FrameBufferSize = videoFrameData[num]->FrameSize * 2;
        }
    }
    bcdUVC = bcdUVC_int;
    LOGD("bcdUVC = %d", bcdUVC);
    if (lowAndroid == 1) low_Android = true;
    initialized = true;
    result ++;
    return result;
}


bool compareArrays(int a[], int b[]) {
    if(memcmp(a, b, sizeof(a)) == 0) return true;
    return false;
}

bool compareStreamingParmsValues() {
    if ( !compareArrays( initStreamingParmsIntArray, probedStreamingParmsIntArray ) || !compareArrays( initStreamingParmsIntArray, finalStreamingParmsIntArray_first )  )  {
        if (initStreamingParmsIntArray[0] != finalStreamingParmsIntArray_first[0]) {
            LOGD("The Controltransfer returned differnt Format Index's\n\n");
            LOGD("Your entered 'Camera Format Index' Values is: %d", initStreamingParmsIntArray[0]);
            LOGD("The 'Camera Format Index' from the Camera Controltransfer is: %d", finalStreamingParmsIntArray_first[0]);
        }
        if (initStreamingParmsIntArray[1] != finalStreamingParmsIntArray_first[1]) {
            LOGD("The Controltransfer returned differnt Frame Index's\n\n");
            LOGD("Your entered 'Camera Frame Index' Values is: " + initStreamingParmsIntArray[1], "\n");
            LOGD("The 'Camera Frame Index' from the Camera Controltransfer is: %d" , finalStreamingParmsIntArray_first[1] );
        }
        if (initStreamingParmsIntArray[2] != finalStreamingParmsIntArray_first[2]) {
            LOGD("The Controltransfer returned differnt FrameIntervall Index's\n\n");
            LOGD("Your entered 'Camera FrameIntervall' Values is: %d", initStreamingParmsIntArray[2] );
            LOGD("The 'Camera FrameIntervall' Value from the Camera Controltransfer is: %d", finalStreamingParmsIntArray_first[2] );
        }
        LOGD("The Values for the Control Transfer have a grey color in the 'edit values' screen");
        LOGD("To get the correct values for you camera, read out the UVC specifications of the camera manualy, or try out the 'Set Up With UVC Settings' Button");
        LOGD ("compareStreamingParmsValues returned false");
        return false;
    } else {
        LOGD("Camera Controltransfer Sucessful !\n\nThe returned Values from the Camera Controltransfer fits to your entered Values\nYou can proceed starting a test run!");
        return true;
    }
    return 0;
}

int initStreamingParms(int FD) {
    LOGD("initStreamingParms");
    if (!cameraDevice_is_wraped) {
        if (wrap_camera_device(FD, &uvcContext, NULL, &uvcDeviceHandle, mDevice) == 0) LOGD("Successfully wraped The CameraDevice");
        else return -1;
    }
    LOGD("trying to claim the interfaces");
    for (int if_num = 0; if_num < (camStreamingInterfaceNum + 1); if_num++) {
        if (libusb_kernel_driver_active(uvcDeviceHandle->usb_devh, if_num)) {
            libusb_detach_kernel_driver(uvcDeviceHandle->usb_devh, if_num);
        }
        int rc = libusb_claim_interface(uvcDeviceHandle->usb_devh, if_num);
        if (rc < 0) {
            fprintf(stderr, "Error claiming interface: %s\n",
                    libusb_error_name(rc));
        } else {
            printf("Interface %d erfolgreich eingehängt;\n", if_num);
        }
    }
    LOGD("Print Device");

    struct libusb_device_descriptor desc;
    print_device(libusb_get_device(uvcDeviceHandle->usb_devh) , 0, uvcDeviceHandle->usb_devh, desc);
    int r = libusb_set_interface_alt_setting(uvcDeviceHandle->usb_devh, camStreamingInterfaceNum, 0); // camStreamingAltSetting = 7;    // 7 = 3x1024 bytes packet size
    if (r != LIBUSB_SUCCESS) {
        LOGD("libusb_set_interface_alt_setting(uvcDeviceHandle->usb_devh, 1, 1) failed with error %d\n", r);
        return -4;
    } else {
        LOGD("Die Alternativeinstellungen wurden erfolgreich gesetzt: %d ; Altsetting = 0\n", r);
    }
    __android_log_print(ANDROID_LOG_INFO, TAG,
                        "libusb_control_transfer start\n");
    initStreamingParms_controltransfer(uvcDeviceHandle->usb_devh, false);
    camIsOpen = compareStreamingParmsValues();
    if (camIsOpen) return 0;
    else return -4;
}

int fetchTheCamStreamingEndpointAdress (int FD) {
    if (!camIsOpen) {
        int ret;
        enum libusb_error rc;
        if (!cameraDevice_is_wraped) {
            if (wrap_camera_device(FD, &uvcContext, NULL, &uvcDeviceHandle, mDevice) == 0) LOGD("Successfully wraped The CameraDevice");
            else return -1;
        }
        camIsOpen = true;
    }
    for (int if_num = 0; if_num < (2); if_num++) {
        if (libusb_kernel_driver_active(uvcDeviceHandle->usb_devh, if_num)) {
            libusb_detach_kernel_driver(uvcDeviceHandle->usb_devh, if_num);
        }
        int rc = libusb_claim_interface(uvcDeviceHandle->usb_devh, if_num);
        if (rc < 0) {
            fprintf(stderr, "Error claiming interface: %s\n",
                    libusb_error_name(rc));
        } else {
            printf("Interface %d erfolgreich eingehängt;\n", if_num);
        }
    }
    struct libusb_device_descriptor desc;
    print_device(libusb_get_device(uvcDeviceHandle->usb_devh) , 0, uvcDeviceHandle->usb_devh, desc);
    return streamEndPointAdressOverNative;
}

// JNA Methods:
void setImageCapture() {    imageCapture = true;}
void setImageCaptureLongClick() {    imageCapturelongClick = true;}
void startVideoCapture() {   videoCapture = true;}
void stopVideoCapture() {    videoCapture = false;}
void startVideoCaptureLongClick() {    videoCaptureLongClick = true;}
void stopVideoCaptureLongClick() { videoCaptureLongClick = false;}


unsigned char * probeCommitControl(int bmHin, int camFormatInde, int camFrameInde, int camFrameInterva, int FD) {
    bmHint = bmHin;
    camFormatIndex = camFormatInde;
    camFrameIndex = camFrameInde;
    camFrameInterval = camFrameInterva;
    if (!camIsOpen) {
        int ret;
        enum libusb_error rc;
        if (!cameraDevice_is_wraped) {
            if (wrap_camera_device(FD, &uvcContext, NULL, &uvcDeviceHandle, mDevice) == 0) LOGD("Successfully wraped The CameraDevice");
            else return NULL;
        }
        camIsOpen = true;
    }
    for (int if_num = 0; if_num < (camStreamingInterfaceNum + 1); if_num++) {
        if (libusb_kernel_driver_active(uvcDeviceHandle->usb_devh, if_num)) {
            libusb_detach_kernel_driver(uvcDeviceHandle->usb_devh, if_num);
        }
        int rc = libusb_claim_interface(uvcDeviceHandle->usb_devh, if_num);
        if (rc < 0) {
            fprintf(stderr, "Error claiming interface: %s\n",
                    libusb_error_name(rc));
        } else {
            printf("Interface %d erfolgreich eingehängt;\n", if_num);
        }
    }
    struct libusb_device_descriptor desc;
    print_device(libusb_get_device(uvcDeviceHandle->usb_devh) , 0, uvcDeviceHandle->usb_devh, desc);
    int r = libusb_set_interface_alt_setting(uvcDeviceHandle->usb_devh, camStreamingInterfaceNum, 0); // camStreamingAltSetting = 7;    // 7 = 3x1024 bytes packet size
    if (r != LIBUSB_SUCCESS) {
        LOGD("libusb_set_interface_alt_setting(uvcDeviceHandle->usb_devh, 1, 1) failed with error %d\n", r);
        return NULL;
    } else {
        LOGD("Die Alternativeinstellungen wurden erfolgreich gesetzt: %d ; Altsetting = 0\n", r);
    }

    __android_log_print(ANDROID_LOG_INFO, TAG,
                        "libusb_control_transfer start\n");

    initStreamingParms_controltransfer(uvcDeviceHandle->usb_devh, true);
    camIsOpen = compareStreamingParmsValues();
    if (camIsOpen) return ctl_transfer_Data;
    else return ctl_transfer_Data;
}

void eof_isoc_transfer_completion_handler_automaticdetection(uvc_stream_handle_t *strmh) {
    /*
    pthread_mutex_lock(&strmh->cb_mutex);
    {
        //tmp_buf = strmh->holdbuf;
        strmh->hold_bfh_err = strmh->bfh_err;	// XXX
        strmh->hold_bytes = total;
        //strmh->holdbuf = strmh->outbuf;
        //strmh->outbuf = tmp_buf;
        strmh->hold_last_scr = strmh->last_scr;
        strmh->hold_pts = strmh->pts;
        strmh->hold_seq = strmh->seq;
        pthread_cond_broadcast(&strmh->cb_cond);
    }
    pthread_mutex_unlock(&strmh->cb_mutex);
*/
    autoStruct.sframeLenArray[autoStruct.frameCnt] = autoStruct.frameLen;
    LOGD("Frame received");
    ueberschreitungDerUebertragungslaenge = 0;
    if (frameUeberspringen == 0) {
        ++totalFrame;
        if (total < videoFrameData[whichVideoFrameDate]->FrameBufferSize) {
            LOGD(stderr, "insufficient frame data.\n");
        }
        LOGD("Länge des Frames = %d\n", total);
        autoStruct.frameCnt ++;
        if (numberOfAutoFrames == totalFrame) {
            LOGD("calling autoStreamfinished");
            runningStream = false;
            autoStreamfinished();
        }
        autoStruct.frameLen = 0;
    } else {
        LOGD("Länge des Frames (Übersprungener Frame) = %d\n", total);
        frameUeberspringen = 0;
    }
    total = 0;
}

void isoc_transfer_completion_handler_automaticdetection(struct libusb_transfer *the_transfer) {
    //LOGD("Iso Transfer Callback Function");
    uvc_stream_handle_t *strmh = the_transfer->user_data;
    unsigned char *p;
    int packetLen;
    int i;
    p = the_transfer->buffer;
    autoStruct.requestCnt ++;
    for (i = 0; i < the_transfer->num_iso_packets; i++, p += maxPacketSize) {
        if (the_transfer->iso_packet_desc[i].status == LIBUSB_TRANSFER_COMPLETED) {
            autoStruct.packetCnt ++;
            packetLen = the_transfer->iso_packet_desc[i].actual_length;
            // packet only contains an acknowledge?
            if (packetLen == 0) {
                autoStruct.packet0Cnt++;
            }
            if (packetLen == 12) {
                autoStruct.packet12Cnt++;
            }
            if (packetLen < 2) {
                continue;
            }
            // error packet
            if (p[1] & UVC_STREAM_ERR) // bmHeaderInfoh
            {
                autoStruct.packetErrorCnt ++;
                LOGD("UVC_STREAM_ERR --> Package %d", i);
                frameUeberspringen = 1;
                continue;
            }
            /* The frame ID bit was flipped, but we have image data sitting
        around from prior transfers. This means the camera didn't send
        an EOF for the last transfer of the previous frame or some frames losted. */
            if ((strmh->fid != (p[1] & UVC_STREAM_FID) )  && total ) {
                LOGD("UVC_STREAM_FID");
                LOGD("Wert fid = %d, Wert strmh = %d", (p[1] & UVC_STREAM_FID), strmh->fid);
                eof_isoc_transfer_completion_handler_automaticdetection(strmh);
            }
            strmh->fid = p[1] & UVC_STREAM_FID;
            packetLen -= p[0];
            if (packetLen + total > videoFrameData[whichVideoFrameDate]->FrameBufferSize) {
                if (ueberschreitungDerUebertragungslaenge == 1) {
                    LOGD(stderr, "Die Framegröße musste gekürzt werden.\n");
                    ueberschreitungDerUebertragungslaenge = 1;
                    fflush(stdout);
                }
                packetLen = videoFrameData[whichVideoFrameDate]->FrameBufferSize - total;
            }
            memcpy(videoFrameData[whichVideoFrameDate]->videoframe + total, p + p[0], packetLen);
            total += packetLen;
            autoStruct.frameLen += packetLen;
            if ((p[1] & UVC_STREAM_EOF) && total != 0) {
                eof_isoc_transfer_completion_handler_automaticdetection(strmh);
            }
        }
    }
    if (runningStream) if (libusb_submit_transfer(the_transfer) != 0) {
            LOGD(stderr, "Die Übertragung ist gescheitert. \n");
        }
}

void startAutoDetection () {
    if (camIsOpen) {
        uvc_stream_handle_t *strmh = NULL;
        strmh = calloc(1, sizeof(*strmh));
        if (UNLIKELY(!strmh)) {
            int ret = UVC_ERROR_NO_MEM;
            //goto fail;
        }
        strmh->running = 1;
        strmh->seq = 0;
        strmh->fid = 0;
        strmh->pts = 0;
        strmh->last_scr = 0;
        strmh->bfh_err = 0;	// XXX

        totalFrame = 0;
        int r = libusb_set_interface_alt_setting(uvcDeviceHandle->usb_devh, camStreamingInterfaceNum, camStreamingAltSetting); // camStreamingAltSetting = 7;    // 7 = 3x1024 bytes packet size
        if (r != LIBUSB_SUCCESS) {
            LOGD("libusb_set_interface_alt_setting(uvcDeviceHandle->usb_devh, 1, 1) failed with error %d\n", r);
        } else {
            LOGD("Die Alternativeinstellungen wurden erfolgreich gesetzt: %d ; Altsetting = %d\n", r, camStreamingAltSetting);
        }
        autoStruct.requestCnt = 0;
        autoStruct.frameCnt = 0;
        autoStruct.frameLen = 0;
        autoStruct.packet0Cnt= 0;
        autoStruct.packet12Cnt= 0;
        autoStruct.packetCnt= 0;
        autoStruct.packetDataCnt= 0;
        autoStruct.packetErrorCnt= 0;
        autoStruct.packetHdr8Ccnt= 0;
        for(int ii = 0; ii < 5; ii++) autoStruct.sframeLenArray[ii] = 0;
        // ------------------------------------------------------------
        // do an isochronous transfer
        struct libusb_transfer * xfers[activeUrbs];
        for (i = 0; i < activeUrbs; i++) {
            xfers[i] = libusb_alloc_transfer(packetsPerRequest);
            uint8_t *data = malloc(maxPacketSize * packetsPerRequest);

            libusb_fill_iso_transfer(
                    xfers[i], uvcDeviceHandle->usb_devh, camStreamingEndpoint,
                    data, maxPacketSize*packetsPerRequest, packetsPerRequest,
                    isoc_transfer_completion_handler_automaticdetection, (void*) strmh, 0);
            libusb_set_iso_packet_lengths(xfers[i], maxPacketSize);
            for (int j = 0; j < packetsPerRequest; j++) {
                xfers[i]->iso_packet_desc[j].status = -1;
            }

        }
        for (i = 0; i < activeUrbs; i++) {
            if (libusb_submit_transfer(xfers[i]) != 0) {
                fprintf(stderr, "submit xfer failed.\n");
            }
        }
        runningStream = true;
        while (runningStream) {
            if (runningStream == false) {
                break;
            }
            libusb_handle_events(uvcContext->usb_ctx);
        }
        LOGD("trying to initialize pthread ...");
        pthread_mutex_init(&strmh->cb_mutex, NULL);
        pthread_cond_init(&strmh->cb_cond, NULL);
        LOGD("inizialisation complete");

    }
}

void closeLibUsb() {
    //pthread_cond_destroy(&strmh->cb_cond);
    //pthread_mutex_destroy(&strmh->cb_mutex);
    libusb_set_interface_alt_setting(uvcDeviceHandle->usb_devh,1,0);
    libusb_release_interface(uvcDeviceHandle->usb_devh, 0);
    libusb_release_interface(uvcDeviceHandle->usb_devh, 1);
    //libusb_close(devh);
    //libusb_exit(ctx);
    //close(fd);
}

void probeCommitControl_cleanup()
{
    free(ctl_transfer_Data->ctl_transfer_values);
    LOGD("probeCommitControl_cleanup Complete");
}

void stopJavaVM() {
    (*javaVm)->DetachCurrentThread(javaVm);
}

void stopStreaming() {
    LOGD("native stopStreaming");
    runningStream = false;
}

void eof_isoc_transfer_completion_handler_five_sec(uvc_stream_handle_t *strmh) {
    pthread_mutex_lock(&strmh->cb_mutex);
    {
        /* swap the buffers */
        //tmp_buf = strmh->holdbuf;
        strmh->hold_bfh_err = strmh->bfh_err;	// XXX
        strmh->hold_bytes = total;
        //strmh->holdbuf = strmh->outbuf;
        //strmh->outbuf = tmp_buf;
        strmh->hold_last_scr = strmh->last_scr;
        strmh->hold_pts = strmh->pts;
        strmh->hold_seq = strmh->seq;
        //pthread_cond_broadcast(&strmh->cb_cond);
    }
    pthread_mutex_unlock(&strmh->cb_mutex);

//manualFrameStruct.sframeLenArray[manualFrameStruct.frameCnt] = manualFrameStruct.frameLen;
    LOGD("Frame received_5_frame");
    ueberschreitungDerUebertragungslaenge = 0;
    if (frameUeberspringen == 0) {
        ++totalFrame;
        if (strmh->hold_bytes < videoFrameData[whichVideoFrameDate]->FrameBufferSize) {
            LOGD(stderr, "insufficient frame data.\n");
        }
        LOGD("Länge des Frames = %d\n", strmh->hold_bytes);
        //manualFrameStruct.frameCnt ++;
        LOGD("calling sendReceivedDataToJava");
        //runningStream = false;
        sendReceivedDataToJava(videoFrameData[whichVideoFrameDate], strmh->hold_bytes);
        //manualFrameStruct.frameLen = 0;
    } else {
        LOGD("Länge des Frames (Übersprungener Frame) = %d\n", strmh->hold_bytes);
        frameUeberspringen = 0;
    }
    clock_t now = clock();
    float seconds = (float)(now - startTime) / CLOCKS_PER_SEC;
    LOGD("%.f seconds since the Stream is running.\n", seconds);

    if(seconds >= 5) {
        LOGD ("5 sec");
        runningStream = false;
    }
    else if(seconds >= 4) LOGD ("4 sec");
    else if(seconds >= 3) LOGD ("3 sec");
    else if(seconds >= 2) LOGD ("2 sec");
    else if(seconds >= 1) LOGD ("1 sec");
    else LOGD ("0 sec");

    strmh->seq++;
    total = 0;
    strmh->last_scr = 0;
    strmh->pts = 0;
    strmh->bfh_err = 0;	// XXX
}

void isoc_transfer_completion_handler_five_sec(struct libusb_transfer *the_transfer) {
    uvc_stream_handle_t *strmh = the_transfer->user_data;
    //LOGD("Iso Transfer Callback Function");
    unsigned char *p;
    int packetLen;
    int i;
    p = the_transfer->buffer;
    manualFrameStruct.requestCnt ++;
    for (i = 0; i < the_transfer->num_iso_packets; i++, p += maxPacketSize) {
        if (the_transfer->iso_packet_desc[i].status == LIBUSB_TRANSFER_COMPLETED) {
            //manualFrameStruct.packetCnt ++;
            packetLen = the_transfer->iso_packet_desc[i].actual_length;
            // packet only contains an acknowledge?
            if (packetLen == 0) {
                manualFrameStruct.packet0Cnt++;
            }
            if (packetLen == 12) {
                //manualFrameStruct.packet12Cnt++;
            }
            if (packetLen < 2) {
                continue;
            }
            /* The frame ID bit was flipped, but we have image data sitting
        around from prior transfers. This means the camera didn't send
        an EOF for the last transfer of the previous frame or some frames losted. */
            if ((strmh->fid != (p[1] & UVC_STREAM_FID) )  && total ) {
                LOGD("UVC_STREAM_FID");
                LOGD("Wert fid = %d, Wert strmh = %d", (p[1] & UVC_STREAM_FID), strmh->fid);
                eof_isoc_transfer_completion_handler_five_sec(strmh);
            }
            strmh->fid = p[1] & UVC_STREAM_FID;
            // error packet
            if (p[1] & UVC_STREAM_ERR) // bmHeaderInfoh
            {
                //manualFrameStruct.packetErrorCnt ++;
                LOGD("UVC_STREAM_ERR --> Package %d", i);
                frameUeberspringen = 1;
                continue;
            }
            packetLen -= p[0];
            if (packetLen + total > videoFrameData[whichVideoFrameDate]->FrameBufferSize) {
                if (ueberschreitungDerUebertragungslaenge == 1) {
                    LOGD(stderr, "Die Framegröße musste gekürzt werden.\n");
                    ueberschreitungDerUebertragungslaenge = 1;
                    fflush(stdout);
                }
                packetLen = videoFrameData[whichVideoFrameDate]->FrameBufferSize - total;
            }
            memcpy(videoFrameData[whichVideoFrameDate]->videoframe + total, p + p[0], packetLen);
            total += packetLen;
            //manualFrameStruct.frameLen += packetLen;
            if ((p[1] & UVC_STREAM_EOF) && total != 0) {
                LOGD("UVC_STREAM_EOF");
                eof_isoc_transfer_completion_handler_five_sec(strmh);
            }
        }
    }
    if (runningStream) if (libusb_submit_transfer(the_transfer) != 0) {
            LOGD(stderr, "Die Übertragung ist gescheitert. \n");
        }
}

void eof_isoc_transfer_completion_handler_one_frame(uvc_stream_handle_t *strmh){
    pthread_mutex_lock(&strmh->cb_mutex);
    {
        /* swap the buffers */
        //tmp_buf = strmh->holdbuf;
        strmh->hold_bfh_err = strmh->bfh_err;	// XXX
        strmh->hold_bytes = total;
        //strmh->holdbuf = strmh->outbuf;
        //strmh->outbuf = tmp_buf;
        strmh->hold_last_scr = strmh->last_scr;
        strmh->hold_pts = strmh->pts;
        strmh->hold_seq = strmh->seq;
        pthread_cond_broadcast(&strmh->cb_cond);
    }
    pthread_mutex_unlock(&strmh->cb_mutex);
    manualFrameStruct.sframeLenArray[manualFrameStruct.frameCnt] = manualFrameStruct.frameLen;
    LOGD("Frame received_one_Frame");
    ueberschreitungDerUebertragungslaenge = 0;
    if (frameUeberspringen == 0) {
        ++totalFrame;
        if (strmh->hold_bytes < videoFrameData[whichVideoFrameDate]->FrameBufferSize) {
            LOGD(stderr, "insufficient frame data.\n");
        }
        LOGD("Länge des Frames = %d\n", strmh->hold_bytes);
        manualFrameStruct.frameCnt ++;
        LOGD("calling sendReceivedDataToJava");
        runningStream = false;
        sendReceivedDataToJava(videoFrameData[whichVideoFrameDate], strmh->hold_bytes);
        manualFrameStruct.frameLen = 0;
    } else {
        LOGD("Länge des Frames (Übersprungener Frame) = %d\n", strmh->hold_bytes);
        frameUeberspringen = 0;
    }
    strmh->seq++;
    total = 0;
    strmh->last_scr = 0;
    strmh->pts = 0;
    strmh->bfh_err = 0;	// XXX
    LOGD("trying to initialize pthread ...");
    pthread_mutex_init(&strmh->cb_mutex, NULL);
    pthread_cond_init(&strmh->cb_cond, NULL);
    LOGD("inizialisation complete");
}

void isoc_transfer_completion_handler_one_frame(struct libusb_transfer *the_transfer) {
    //LOGD("Iso Transfer Callback Function");
    uvc_stream_handle_t *strmh = the_transfer->user_data;

    unsigned char *p;
    int packetLen;
    int i;
    p = the_transfer->buffer;
    manualFrameStruct.requestCnt ++;
    for (i = 0; i < the_transfer->num_iso_packets; i++, p += maxPacketSize) {
        if (the_transfer->iso_packet_desc[i].status == LIBUSB_TRANSFER_COMPLETED) {
            manualFrameStruct.packetCnt ++;
            packetLen = the_transfer->iso_packet_desc[i].actual_length;
            // packet only contains an acknowledge?
            if (packetLen == 0) {
                manualFrameStruct.packet0Cnt++;
            }
            if (packetLen == 12) {
                manualFrameStruct.packet12Cnt++;
            }
            if (packetLen < 2) {
                continue;
            }
            // error packet
            if (p[1] & UVC_STREAM_ERR) // bmHeaderInfoh
            {
                manualFrameStruct.packetErrorCnt ++;
                LOGD("UVC_STREAM_ERR --> Package %d", i);
                frameUeberspringen = 1;
                continue;
            }
            /* The frame ID bit was flipped, but we have image data sitting
        around from prior transfers. This means the camera didn't send
        an EOF for the last transfer of the previous frame or some frames losted. */

            if ((strmh->fid != (p[1] & UVC_STREAM_FID) )  && total ) {
                LOGD("UVC_STREAM_FID");
                LOGD("Wert fid = %d, Wert strmh = %d", (p[1] & UVC_STREAM_FID), strmh->fid);
                eof_isoc_transfer_completion_handler_one_frame(strmh);
            }
            strmh->fid = p[1] & UVC_STREAM_FID;
            packetLen -= p[0];
            if (packetLen + total > videoFrameData[whichVideoFrameDate]->FrameBufferSize) {
                if (ueberschreitungDerUebertragungslaenge == 1) {
                    LOGD(stderr, "Die Framegröße musste gekürzt werden.\n");
                    ueberschreitungDerUebertragungslaenge = 1;
                    fflush(stdout);
                }
                packetLen = videoFrameData[whichVideoFrameDate]->FrameBufferSize - total;
            }
            memcpy(videoFrameData[whichVideoFrameDate]->videoframe + total, p + p[0], packetLen);
            total += packetLen;
            manualFrameStruct.frameLen += packetLen;
            if ((p[1] & UVC_STREAM_EOF) && total != 0) {
                LOGD("UVC_STREAM_EOF");
                eof_isoc_transfer_completion_handler_one_frame(strmh);
            }
        }
    }
    if (runningStream) if (libusb_submit_transfer(the_transfer) != 0) {
            LOGD(stderr, "Die Übertragung ist gescheitert. \n");
        }
}

void clearManualFrameStruct() {
    manualFrameStruct.frameCnt = 0;
    manualFrameStruct.frameLen = 0;
    for (i = 0; i < 5; i++) {
        manualFrameStruct.sframeLenArray[i] = 0;
    }
    manualFrameStruct.packet0Cnt = 0;
    manualFrameStruct.packetHdr8Ccnt = 0;
    manualFrameStruct.requestCnt = 0;
    manualFrameStruct.packetErrorCnt = 0;
    manualFrameStruct.packetDataCnt = 0;
    manualFrameStruct.packetCnt = 0;
    manualFrameStruct.packet12Cnt = 0;
}

/** @internal
 * @brief Swap the working buffer with the presented buffer and notify consumers
 */
void eof_isoc_transfer_completion_handler_stream(uvc_stream_handle_t *strmh) {
    LOGD("IsoStream over JNA --> Frame received");
/*
    pthread_mutex_lock(&strmh->cb_mutex);
    {
        //tmp_buf = strmh->holdbuf;
        strmh->hold_bfh_err = strmh->bfh_err;	// XXX
        strmh->hold_bytes = total;
        //strmh->holdbuf = strmh->outbuf;
        //strmh->outbuf = tmp_buf;
        strmh->hold_last_scr = strmh->last_scr;
        strmh->hold_pts = strmh->pts;
        strmh->hold_seq = strmh->seq;
        pthread_cond_broadcast(&strmh->cb_cond);
    }
    pthread_mutex_unlock(&strmh->cb_mutex);
*/
    ueberschreitungDerUebertragungslaenge = 0;
    if (frameUeberspringen == 0) {
        ++totalFrame;
        if (total < videoFrameData[whichVideoFrameDate]->FrameBufferSize) {
            LOGD(stderr, "insufficient frame data.\n");
        }
        //LOGD("Länge des Frames = %d\n", strmh->hold_bytes);
        //LOGD("calling sendReceivedDataToJava");
        jnaSendFrameToJava(videoFrameData[whichVideoFrameDate], total);
        runningStream = false;
    } else {
        LOGD("Länge des Frames (Übersprungener Frame) = %d\n", total);
        frameUeberspringen = 0;
    }
    total = 0;
}

void isoc_transfer_completion_handler_stream(struct libusb_transfer *the_transfer) {
    //LOGD("Iso Transfer Callback Function");
    uvc_stream_handle_t *strmh = the_transfer->user_data;
    unsigned char *p;
    int packetLen;
    int i;
    p = the_transfer->buffer;
    for (i = 0; i < the_transfer->num_iso_packets; i++, p += maxPacketSize) {
        if (the_transfer->iso_packet_desc[i].status == LIBUSB_TRANSFER_COMPLETED) {
            packetLen = the_transfer->iso_packet_desc[i].actual_length;
            if (packetLen < 2) {
                continue;
            }
            // error packet
            if (p[1] & UVC_STREAM_ERR) // bmHeaderInfoh
            {
                LOGD("UVC_STREAM_ERR --> Package %d", i);
                frameUeberspringen = 1;
                continue;
            }
            /* The frame ID bit was flipped, but we have image data sitting
           around from prior transfers. This means the camera didn't send
           an EOF for the last transfer of the previous frame or some frames losted. */
            if ((strmh->fid != (p[1] & UVC_STREAM_FID) )  && total ) {
                LOGD("UVC_STREAM_FID");
                LOGD("Wert fid = %d, Wert strmh = %d", (p[1] & UVC_STREAM_FID), strmh->fid);
                eof_isoc_transfer_completion_handler_stream(strmh);
            }
            strmh->fid = p[1] & UVC_STREAM_FID;


            packetLen -= p[0];
            if (packetLen + total > videoFrameData[whichVideoFrameDate]->FrameBufferSize) {
                if (ueberschreitungDerUebertragungslaenge == 1) {
                    LOGD(stderr, "Die Framegröße musste gekürzt werden.\n");
                    ueberschreitungDerUebertragungslaenge = 1;
                    fflush(stdout);
                }
                packetLen = videoFrameData[whichVideoFrameDate]->FrameBufferSize - total;
            }
            memcpy(videoFrameData[whichVideoFrameDate]->videoframe + total, p + p[0], packetLen);
            total += packetLen;
            if ((p[1] & UVC_STREAM_EOF) && total != 0) {
                eof_isoc_transfer_completion_handler_stream(strmh);
            }
        }
    }
    if (runningStream) if (libusb_submit_transfer(the_transfer) != 0) {
            LOGD(stderr, "Die Übertragung ist gescheitert. \n");
        }
}

void getFramesOverLibUsb( int yuvFrameIsZero, int stream, int whichTestrun) {
    // Trying Stream Handle
    uvc_stream_handle_t *strmh = NULL;
    strmh = calloc(1, sizeof(*strmh));
    if (UNLIKELY(!strmh)) {
        int ret = UVC_ERROR_NO_MEM;
        //goto fail;
    }
    strmh->running = 1;
    strmh->seq = 0;
    strmh->fid = 0;
    strmh->pts = 0;
    strmh->last_scr = 0;
    strmh->bfh_err = 0;	// XXX
    LOGD("trying to initialize pthread ...");
    pthread_mutex_init(&strmh->cb_mutex, NULL);
    pthread_cond_init(&strmh->cb_cond, NULL);
    LOGD("inizialisation complete");


    clearManualFrameStruct();
    int requestMode = 0;
    probeCommitControl(bmHint, camFormatIndex, camFrameIndex, camFrameInterval, fd);
    //probeCommitControl_cleanup();
    LOGD("ISO Stream");
    runningStream = true;
    int r = libusb_set_interface_alt_setting(uvcDeviceHandle->usb_devh, camStreamingInterfaceNum,
                                             camStreamingAltSetting); // camStreamingAltSetting = 7;    // 7 = 3x1024 bytes packet size
    if (r != LIBUSB_SUCCESS) {
        LOGD("libusb_set_interface_alt_setting(uvcDeviceHandle->usb_devh, 1, 1) failed with error %d\n", r);
    } else {
        LOGD("Die Alternativeinstellungen wurden erfolgreich gesetzt: %d ; Altsetting = %d\n", r,
             camStreamingAltSetting);
    }
    if (activeUrbs > 16) activeUrbs = 16;
    struct libusb_transfer *xfers[activeUrbs];
    if(stream == 1 || whichTestrun == 0) {
        for (i = 0; i < activeUrbs; i++) {
            xfers[i] = libusb_alloc_transfer(packetsPerRequest);
            uint8_t *data = malloc(maxPacketSize * packetsPerRequest);
            libusb_fill_iso_transfer(
                    xfers[i], uvcDeviceHandle->usb_devh, camStreamingEndpoint,
                    data, maxPacketSize * packetsPerRequest, packetsPerRequest,
                    isoc_transfer_completion_handler_stream, NULL, 5000);
            libusb_set_iso_packet_lengths(xfers[i], maxPacketSize);
        }
    }
    if (whichTestrun == 1){
        for (i = 0; i < activeUrbs; i++) {
            xfers[i] = libusb_alloc_transfer(packetsPerRequest);
            uint8_t *data = malloc(maxPacketSize * packetsPerRequest);
            libusb_fill_iso_transfer(
                    xfers[i], uvcDeviceHandle->usb_devh, camStreamingEndpoint,
                    data, maxPacketSize * packetsPerRequest, packetsPerRequest,
                    isoc_transfer_completion_handler_one_frame, (void*) strmh, 5000);
            libusb_set_iso_packet_lengths(xfers[i], maxPacketSize);
        }
    } else if (whichTestrun == 5){
        startTime = clock();  /* set current time;  */
        for (i = 0; i < activeUrbs; i++) {
            xfers[i] = libusb_alloc_transfer(packetsPerRequest);
            uint8_t *data = malloc(maxPacketSize * packetsPerRequest);
            libusb_fill_iso_transfer(
                    xfers[i], uvcDeviceHandle->usb_devh, camStreamingEndpoint,
                    data, maxPacketSize * packetsPerRequest, packetsPerRequest,
                    isoc_transfer_completion_handler_five_sec, (void*) strmh, 5000);
            libusb_set_iso_packet_lengths(xfers[i], maxPacketSize);
        }
    }

    runningStream = true;
    for (i = 0; i < activeUrbs; i++) {
        if (libusb_submit_transfer(xfers[i]) != 0) {
            LOGD(stderr, "submit xfer failed.\n");
        }
    }
    runningStream = true;
    while (runningStream) {
        if (runningStream == false) {
            LOGD("stopped because runningStream == false !!  --> libusb_event_handling STOPPED");
            break;
        }
        libusb_handle_events(uvcContext->usb_ctx);
    }
}

//////////////////////// LIB UVC FUNCTIONS

static void copyFrame(const uint8_t *src, uint8_t *dest, const int width, int height, const int stride_src, const int stride_dest) {
    const int h8 = height % 8;
    for (int i = 0; i < h8; i++) {
        memcpy(dest, src, width);
        dest += stride_dest; src += stride_src;
    }
    for (int i = 0; i < height; i += 8) {
        memcpy(dest, src, width);
        dest += stride_dest; src += stride_src;
        memcpy(dest, src, width);
        dest += stride_dest; src += stride_src;
        memcpy(dest, src, width);
        dest += stride_dest; src += stride_src;
        memcpy(dest, src, width);
        dest += stride_dest; src += stride_src;
        memcpy(dest, src, width);
        dest += stride_dest; src += stride_src;
        memcpy(dest, src, width);
        dest += stride_dest; src += stride_src;
        memcpy(dest, src, width);
        dest += stride_dest; src += stride_src;
        memcpy(dest, src, width);
        dest += stride_dest; src += stride_src;
    }
}

// transfer specific frame data to the Surface(ANativeWindow)
int copyToSurface(uvc_frame_t *frame, ANativeWindow **window) {
    // ENTER();
    int result = 0;
    if (LIKELY(*window)) {
        ANativeWindow_Buffer buffer;
        if (LIKELY(ANativeWindow_lock(*window, &buffer, NULL) == 0)) {
            // source = frame data
            const uint8_t *src = (uint8_t *)frame->data;
            const int src_w = frame->width * PREVIEW_PIXEL_BYTES;
            const int src_step = frame->width * PREVIEW_PIXEL_BYTES;
            // destination = Surface(ANativeWindow)
            uint8_t *dest = (uint8_t *)buffer.bits;
            const int dest_w = buffer.width * PREVIEW_PIXEL_BYTES;
            const int dest_step = buffer.stride * PREVIEW_PIXEL_BYTES;
            // use lower transfer bytes
            const int w = src_w < dest_w ? src_w : dest_w;
            // use lower height
            const int h = frame->height < buffer.height ? frame->height : buffer.height;
            // transfer from frame data to the Surface
            copyFrame(src, dest, w, h, src_step, dest_step);
            ANativeWindow_unlockAndPost(*window);
        } else {
            result = -1;
        }
    } else {
        result = -1;
    }
    return result; //RETURN(result, int);
}

///////////////////// ACTIVITY FUNCTIONS

void eof_cb_jni_stream_ImageView(uvc_stream_handle_t *strmh) {

    /*
    pthread_mutex_lock(&strmh->cb_mutex);
    {
        //tmp_buf = strmh->holdbuf;
        strmh->hold_bfh_err = strmh->bfh_err;	// XXX
        strmh->hold_bytes = total;
        //strmh->holdbuf = strmh->outbuf;
        //strmh->outbuf = tmp_buf;
        strmh->hold_last_scr = strmh->last_scr;
        strmh->hold_pts = strmh->pts;
        strmh->hold_seq = strmh->seq;
        pthread_cond_broadcast(&strmh->cb_cond);
    }
    pthread_mutex_unlock(&strmh->cb_mutex);
    */

    LOGD("JniIsoStream --> Frame received");
    ueberschreitungDerUebertragungslaenge = 0;
    if (frameUeberspringen == 0) {
        ++totalFrame;
        if (runningStream == false) stopStreaming();

        JNIEnv * jenv;
        int errorCode = (*javaVm)->AttachCurrentThread(javaVm, (void**) &jenv, NULL);
        jbyteArray array = (*jenv)->NewByteArray(jenv, videoFrameData[whichVideoFrameDate]->FrameBufferSize);
        // Main Activity
        (*jenv)->SetByteArrayRegion(jenv, array, 0, videoFrameData[whichVideoFrameDate]->FrameBufferSize, (jbyte *) videoFrameData[whichVideoFrameDate]->videoframe);
        (*jenv)->CallVoidMethod(jenv, mainActivityObj, javainitializeStreamArray, array);
        runningStream = false;


    } else {
        LOGD("Länge des Frames (Übersprungener Frame) = %d\n", total);
        frameUeberspringen = 0;
    }

    total = 0;
}

void cb_jni_stream_ImageView(struct libusb_transfer *the_transfer) {
    //LOGD("Iso Transfer Callback Function");
    uvc_stream_handle_t *strmh = the_transfer->user_data;
    unsigned char *p;
    int packetLen;
    int i;
    p = the_transfer->buffer;
    for (i = 0; i < the_transfer->num_iso_packets; i++, p += maxPacketSize) {
        if (the_transfer->iso_packet_desc[i].status == LIBUSB_TRANSFER_COMPLETED) {
            packetLen = the_transfer->iso_packet_desc[i].actual_length;
            if (packetLen < 2) {
                continue;
            }
            // error packet
            if (p[1] & UVC_STREAM_ERR) // bmHeaderInfoh
            {
                LOGD("UVC_STREAM_ERR --> Package %d", i);
                frameUeberspringen = 1;
                continue;
            }
            /* The frame ID bit was flipped, but we have image data sitting
         around from prior transfers. This means the camera didn't send
         an EOF for the last transfer of the previous frame or some frames losted. */
            if ((strmh->fid != (p[1] & UVC_STREAM_FID) )  && total ) {
                LOGD("UVC_STREAM_FID");
                LOGD("Wert fid = %d, Wert strmh = %d", (p[1] & UVC_STREAM_FID), strmh->fid);
                eof_cb_jni_stream_ImageView(strmh);
            }
            strmh->fid = p[1] & UVC_STREAM_FID;

            packetLen -= p[0];
            if (packetLen + total > videoFrameData[whichVideoFrameDate]->FrameBufferSize) {
                if (ueberschreitungDerUebertragungslaenge == 1) {
                    ueberschreitungDerUebertragungslaenge = 1;
                    fflush(stdout);
                }
                packetLen = videoFrameData[whichVideoFrameDate]->FrameBufferSize - total;
            }
            memcpy(videoFrameData[whichVideoFrameDate]->videoframe + total, p + p[0], packetLen);
            total += packetLen;
            if ((p[1] & UVC_STREAM_EOF) && total != 0) {
                eof_cb_jni_stream_ImageView(strmh);
            }
        }
    }
    if (runningStream) if (libusb_submit_transfer(the_transfer) != 0) {
            LOGD(stderr, "SUBMISSION FAILED. \n");
        }
}


JNIEXPORT void JNICALL Java_humer_UvcCamera_StartIsoStreamActivity_JniIsoStreamActivity
        (JNIEnv *env, jobject obj, jint stream, jint frameIndex) {
    if (initialized) {
        uvc_stream_handle_t *strmh = NULL;
        strmh = calloc(1, sizeof(*strmh));
        if (UNLIKELY(!strmh)) {
            int ret = UVC_ERROR_NO_MEM;
            //goto fail;
        }
        strmh->running = 1;
        strmh->seq = 0;
        strmh->fid = 0;
        strmh->pts = 0;
        strmh->last_scr = 0;
        strmh->bfh_err = 0;	// XXX
        probeCommitControl(bmHint, camFormatIndex, camFrameIndex, camFrameInterval, fd);
        //probeCommitControl_cleanup();
        LOGD("ISO Stream");
        int status = (*env)->GetJavaVM(env, &javaVm);
        if(status != 0) {
            LOGE("failed to attach javaVm");
        }
        class = (*env)->GetObjectClass(env, obj);
        jniHelperClass = (*env)->NewGlobalRef(env, class);
        mainActivityObj = (*env)->NewGlobalRef(env, obj);
        javainitializeStreamArray = (*env)->GetMethodID(env, jniHelperClass, "initializeStreamArray", "([B)V");
        runningStream = true;
        int r = libusb_set_interface_alt_setting(uvcDeviceHandle->usb_devh, camStreamingInterfaceNum,
                                                 camStreamingAltSetting); // camStreamingAltSetting = 7;    // 7 = 3x1024 bytes packet size
        if (r != LIBUSB_SUCCESS) {
            LOGD("libusb_set_interface_alt_setting(uvcDeviceHandle->usb_devh, 1, 1) failed with error %d\n", r);
        } else {
            LOGD("Die Alternativeinstellungen wurden erfolgreich gesetzt: %d ; Altsetting = %d\n", r,
                 camStreamingAltSetting);
        }
        if (activeUrbs > 16) activeUrbs = 16;
        struct libusb_transfer *xfers[activeUrbs];
        for (i = 0; i < activeUrbs; i++) {
            xfers[i] = libusb_alloc_transfer(packetsPerRequest);
            uint8_t *data = malloc(maxPacketSize * packetsPerRequest);
            libusb_fill_iso_transfer(
                    xfers[i], uvcDeviceHandle->usb_devh, camStreamingEndpoint,
                    data, maxPacketSize * packetsPerRequest, packetsPerRequest,
                    cb_jni_stream_ImageView, (void*) strmh, 5000);
            libusb_set_iso_packet_lengths(xfers[i], maxPacketSize);
        }
        runningStream = true;
        for (i = 0; i < activeUrbs; i++) {
            if (libusb_submit_transfer(xfers[i]) != 0) {
                LOGD(stderr, "submit xfer failed.\n");
            }
        }
        runningStream = true;
        while (runningStream) {
            if (runningStream == false) {
                LOGD("stopped because runningStream == false !!  --> libusb_event_handling STOPPED");
                break;
            }
            libusb_handle_events(uvcContext->usb_ctx);
        }
        LOGD("ISO Stream Start -- finished");
    }
}

void eof_cb_jni_stream_Surface_Activity(uvc_stream_handle_t *strmh) {
    /*
    pthread_mutex_lock(&strmh->cb_mutex);
    {
        //tmp_buf = strmh->holdbuf;
        strmh->hold_bfh_err = strmh->bfh_err;	// XXX
        strmh->hold_bytes = total;
        //strmh->holdbuf = strmh->outbuf;
        //strmh->outbuf = tmp_buf;
        strmh->hold_last_scr = strmh->last_scr;
        strmh->hold_pts = strmh->pts;
        strmh->hold_seq = strmh->seq;
        pthread_cond_broadcast(&strmh->cb_cond);
    }
    pthread_mutex_unlock(&strmh->cb_mutex);
*/
    LOGD("Frame received_Surface_Act");
    ueberschreitungDerUebertragungslaenge = 0;
    if (frameUeberspringen == 0) {
        ++totalFrame;
        if (runningStream == false) stopStreaming();
        uvc_frame_t *rgb;
        uvc_error_t ret;
        // We'll convert the image from YUV/JPEG to BGR, so allocate space
        rgb = uvc_allocate_frame(imageWidth * imageHeight * 4);
        if (!rgb) {
            printf("unable to allocate rgb frame!");
            return;
        }
        // Do the BGR conversion
        ret = uvc_yuyv2rgbx(rgb);
        if (ret) {
            uvc_perror(ret, "uvc_any2bgr");
            uvc_free_frame(rgb);
            return;
        }
        copyToSurface(rgb, &mCaptureWindow);
        uvc_free_frame(rgb);
    } else {
        LOGD("Länge des Frames (Übersprungener Frame) = %d\n", total);
        frameUeberspringen = 0;
    }
    total = 0;
}

void cb_jni_stream_Surface_Activity(struct libusb_transfer *the_transfer) {
    uvc_stream_handle_t *strmh = the_transfer->user_data;
    if (runningStream == false) stopStreaming();
    //LOGD("Iso Transfer Callback Function");
    unsigned char *p;
    int packetLen;
    int i;
    p = the_transfer->buffer;
    for (i = 0; i < the_transfer->num_iso_packets; i++, p += maxPacketSize) {
        if (the_transfer->iso_packet_desc[i].status == LIBUSB_TRANSFER_COMPLETED) {
            packetLen = the_transfer->iso_packet_desc[i].actual_length;
            if (packetLen < 2) {
                continue;
            }
            // error packet
            if (p[1] & UVC_STREAM_ERR) // bmHeaderInfoh
            {
                LOGD("UVC_STREAM_ERR --> Package %d", i);
                frameUeberspringen = 1;
                continue;
            }

            /* The frame ID bit was flipped, but we have image data sitting
        around from prior transfers. This means the camera didn't send
        an EOF for the last transfer of the previous frame or some frames losted. */
            if ((strmh->fid != (p[1] & UVC_STREAM_FID) )  && total ) {
                LOGD("UVC_STREAM_FID");
                LOGD("Wert fid = %d, Wert strmh = %d", (p[1] & UVC_STREAM_FID), strmh->fid);
                eof_cb_jni_stream_Surface_Activity(strmh);
            }
            strmh->fid = p[1] & UVC_STREAM_FID;



            packetLen -= p[0];
            if (packetLen + total > videoFrameData[whichVideoFrameDate]->FrameBufferSize) {
                if (ueberschreitungDerUebertragungslaenge == 1) {
                    ueberschreitungDerUebertragungslaenge = 1;
                    fflush(stdout);
                }
                packetLen = videoFrameData[whichVideoFrameDate]->FrameBufferSize - total;
            }
            memcpy(videoFrameData[whichVideoFrameDate]->videoframe + total, p + p[0], packetLen);
            total += packetLen;
            if ((p[1] & UVC_STREAM_EOF) && total != 0) {
                eof_cb_jni_stream_Surface_Activity(strmh);
            }
        }
    }
    if (runningStream) if (libusb_submit_transfer(the_transfer) != 0) {
            LOGD(stderr, "SUBMISSION FAILED. \n");
        }
}

JNIEXPORT void JNICALL Java_humer_UvcCamera_StartIsoStreamActivity_JniIsoStreamActivitySurface
        (JNIEnv *env, jobject obj, jobject jSurface, jint stream, jint frameIndex) {

    ANativeWindow *preview_window = jSurface ? ANativeWindow_fromSurface(env, jSurface) : NULL;
    // WINDOW_FORMAT_RGBA_8888
    ANativeWindow_setBuffersGeometry(preview_window, imageWidth, imageHeight, WINDOW_FORMAT_RGBA_8888);
    mCaptureWindow = preview_window;
    probeCommitControl(bmHint, camFormatIndex, camFrameIndex, camFrameInterval, fd);
    if (initialized) {
        uvc_stream_handle_t *strmh = NULL;
        strmh = calloc(1, sizeof(*strmh));
        if (UNLIKELY(!strmh)) {
            int ret = UVC_ERROR_NO_MEM;
            //goto fail;
        }
        strmh->running = 1;
        strmh->seq = 0;
        strmh->fid = 0;
        strmh->pts = 0;
        strmh->last_scr = 0;
        strmh->bfh_err = 0;	// XXX
        int status = (*env)->GetJavaVM(env, &javaVm);
        if(status != 0) {
            LOGE("failed to attach javaVm");
        }
        class = (*env)->GetObjectClass(env, obj);
        jniHelperClass = (*env)->NewGlobalRef(env, class);
        mainActivityObj = (*env)->NewGlobalRef(env, obj);
        int r = libusb_set_interface_alt_setting(uvcDeviceHandle->usb_devh, camStreamingInterfaceNum,
                                                 camStreamingAltSetting); // camStreamingAltSetting = 7;    // 7 = 3x1024 bytes packet size
        if (r != LIBUSB_SUCCESS) {
            LOGD("libusb_set_interface_alt_setting(uvcDeviceHandle->usb_devh, 1, 1) failed with error %d\n", r);
        } else {
            LOGD("Die Alternativeinstellungen wurden erfolgreich gesetzt: %d ; Altsetting = %d\n", r,
                 camStreamingAltSetting);
        }
        if (activeUrbs > 16) activeUrbs = 16;
        struct libusb_transfer *xfers[activeUrbs];
        for (i = 0; i < activeUrbs; i++) {
            xfers[i] = libusb_alloc_transfer(packetsPerRequest);
            uint8_t *data = malloc(maxPacketSize * packetsPerRequest);
            libusb_fill_iso_transfer(
                    xfers[i], uvcDeviceHandle->usb_devh, camStreamingEndpoint,
                    data, maxPacketSize * packetsPerRequest, packetsPerRequest,
                    cb_jni_stream_Surface_Activity, (void*) strmh, 5000);
            libusb_set_iso_packet_lengths(xfers[i], maxPacketSize);
        }
        for (i = 0; i < activeUrbs; i++) {
            if (libusb_submit_transfer(xfers[i]) != 0) {
                LOGD(stderr, "submit xfer failed.\n");
            }
        }
        runningStream = true;
        while (runningStream) {
            if (runningStream == false) {
                LOGD("stopped because runningStream == false !!  --> libusb_event_handling STOPPED");
                break;
            }
            libusb_handle_events(uvcContext->usb_ctx);
        }
        LOGD("ISO Stream Start -- finished");
    }
}

JNIEXPORT void JNICALL Java_humer_UvcCamera_StartIsoStreamActivity_JniSetSurfaceView
        (JNIEnv *env, jobject obj, jobject jSurface) {
    if (jSurface != NULL ) {
        ANativeWindow *preview_window = jSurface ? ANativeWindow_fromSurface(env, jSurface) : NULL;
        // WINDOW_FORMAT_RGBA_8888
        ANativeWindow_setBuffersGeometry(preview_window, imageWidth, imageHeight, WINDOW_FORMAT_RGBA_8888);
        mCaptureWindow = preview_window;
        LOGD("mCaptureWindow, JniSetSurfaceView");
    }
    int status = (*env)->GetJavaVM(env, &javaVm);
    if(status != 0) {
        LOGE("failed to attach javaVm");
    }
    class = (*env)->GetObjectClass(env, obj);
    jniHelperClass = (*env)->NewGlobalRef(env, class);
    mainActivityObj = (*env)->NewGlobalRef(env, obj);
    javaRetrievedStreamActivityFrameFromLibUsb = (*env)->GetMethodID(env, jniHelperClass, "retrievedStreamActivityFrameFromLibUsb", "([B)V");
    javaPictureVideoCaptureYUV  = (*env)->GetMethodID(env, jniHelperClass, "pictureVideoCaptureYUV", "([B)V");
    javaPictureVideoCaptureMJPEG  = (*env)->GetMethodID(env, jniHelperClass, "pictureVideoCaptureMJPEG", "([B)V");
    javaPictureVideoCaptureRGB =  (*env)->GetMethodID(env, jniHelperClass, "pictureVideoCaptureRGB", "([B)V");
}

JNIEXPORT void JNICALL Java_humer_UvcCamera_StartIsoStreamActivity_JniSetSurfaceYuv
        (JNIEnv *env, jobject obj, jobject jSurface) {
    if (jSurface != NULL ) {
        ANativeWindow *preview_window = jSurface ? ANativeWindow_fromSurface(env, jSurface) : NULL;
        // WINDOW_FORMAT_RGBA_8888
        ANativeWindow_setBuffersGeometry(preview_window, imageWidth, imageHeight, WINDOW_FORMAT_RGBA_8888);
        mCaptureWindow = preview_window;
        LOGD("mCaptureWindow, JniSetSurfaceYuv");

    }
    int status = (*env)->GetJavaVM(env, &javaVm);
    if(status != 0) {
        LOGE("failed to attach javaVm");
    }
    class = (*env)->GetObjectClass(env, obj);
    jniHelperClass = (*env)->NewGlobalRef(env, class);
    mainActivityObj = (*env)->NewGlobalRef(env, obj);
    javaRetrievedStreamActivityFrameFromLibUsb = (*env)->GetMethodID(env, jniHelperClass, "retrievedStreamActivityFrameFromLibUsb", "([B)V");
    javaPictureVideoCaptureYUV  = (*env)->GetMethodID(env, jniHelperClass, "pictureVideoCaptureYUV", "([B)V");
    javaPictureVideoCaptureMJPEG  = (*env)->GetMethodID(env, jniHelperClass, "pictureVideoCaptureMJPEG", "([B)V");
    javaPictureVideoCaptureRGB =  (*env)->GetMethodID(env, jniHelperClass, "pictureVideoCaptureRGB", "([B)V");
}

//////////// SERVICE


// Init SurfaceView for Service
JNIEXPORT void JNICALL Java_humer_UvcCamera_StartIsoStreamActivity_JniPrepairStreamOverSurface
        (JNIEnv *env, jobject obj) {


    if (!cameraDevice_is_rewraped) {
        LOGD("rewrapping");
        native_uvc_unref_device();
        wrap_camera_device(fd, &uvcContext, NULL, &uvcDeviceHandle, mDevice);
        cameraDevice_is_rewraped = 1;
    }
    if (initialized) {
        probeCommitControl(bmHint, camFormatIndex, camFrameIndex, camFrameInterval, fd);
        runningStream = true;
        int r = libusb_set_interface_alt_setting(uvcDeviceHandle->usb_devh, camStreamingInterfaceNum,
                                                 camStreamingAltSetting); // camStreamingAltSetting = 7;    // 7 = 3x1024 bytes packet size
        if (r != LIBUSB_SUCCESS) {
            LOGD("libusb_set_interface_alt_setting(uvcDeviceHandle->usb_devh, 1, 1) failed with error %d\n", r);
        } else {
            LOGD("Die Alternativeinstellungen wurden erfolgreich gesetzt: %d ; Altsetting = %d\n", r,
                 camStreamingAltSetting);
        }
        if (activeUrbs > 16) activeUrbs = 16;
    }
}


/** @internal
 * @brief Populate the fields of a frame to be handed to user code
 * must be called with stream cb lock held!
 */
void _uvc_populate_frame(uvc_stream_handle_t *strmh) {
    size_t alloc_size = videoFrameData[whichVideoFrameDate]->FrameBufferSize;
    uvc_frame_t *frame = &strmh->frame;
    //uvc_frame_desc_t *frame_desc;

    /** @todo this stuff that hits the main config cache should really happen
     * in start() so that only one thread hits these data. all of this stuff
     * is going to be reopen_on_change anyway
     */
    if (strcmp(frameFormat, "MJPEG") == 0) frame->frame_format = UVC_FRAME_FORMAT_MJPEG;
    else if (strcmp(frameFormat, "YUY2") == 0) frame->frame_format = UVC_FRAME_FORMAT_YUYV;


    frame->width = imageWidth;
    frame->height = imageHeight;
    // XXX set actual_bytes to zero when erro bits is on
    frame->actual_bytes = LIKELY(!strmh->hold_bfh_err) ? strmh->hold_bytes : 0;

    switch (frame->frame_format) {
        case UVC_FRAME_FORMAT_YUYV:
            frame->step = frame->width * 2;
            break;
        case UVC_FRAME_FORMAT_MJPEG:
            frame->step = 0;
            break;
        default:
            frame->step = 0;
            break;
    }
    LOGD ("realloc");
    /* copy the image data from the hold buffer to the frame (unnecessary extra buf?) */
    if (UNLIKELY(frame->data_bytes < strmh->hold_bytes)) {
        frame->data = realloc(frame->data, strmh->hold_bytes);	// TODO add error handling when failed realloc
        frame->data_bytes = strmh->hold_bytes;
    }

    memcpy(frame->data, videoFrameData[whichVideoFrameDate]->videoframe, strmh->hold_bytes/*frame->data_bytes*/);	// XXX

    /** @todo set the frame time */
}


/** Poll for a frame
 * @ingroup streaming
 *
 * @param devh UVC device
 * @param[out] frame Location to store pointer to captured frame (NULL on error)
 * @param timeout_us >0: Wait at most N microseconds; 0: Wait indefinitely; -1: return immediately
 */
uvc_error_t uvc_stream_get_frame(uvc_stream_handle_t *strmh,
                                 uvc_frame_t **frame, int32_t timeout_us) {
    time_t add_secs;
    time_t add_nsecs;
    struct timespec ts;
    struct timeval tv;

    if (UNLIKELY(!strmh->running))
        return UVC_ERROR_INVALID_PARAM;

    //if (UNLIKELY(strmh->user_cb))
    //    return UVC_ERROR_CALLBACK_EXISTS;

    pthread_mutex_lock(&strmh->cb_mutex);
    {
        if (strmh->last_polled_seq < strmh->hold_seq) {
            _uvc_populate_frame(strmh);
            *frame = &strmh->frame;
            strmh->last_polled_seq = strmh->hold_seq;
        } else if (timeout_us != -1) {
            if (!timeout_us) {
                pthread_cond_wait(&strmh->cb_cond, &strmh->cb_mutex);
            } else {
                add_secs = timeout_us / 1000000;
                add_nsecs = (timeout_us % 1000000) * 1000;
                ts.tv_sec = 0;
                ts.tv_nsec = 0;

#if _POSIX_TIMERS > 0
                clock_gettime(CLOCK_REALTIME, &ts);
#else
                gettimeofday(&tv, NULL);
				ts.tv_sec = tv.tv_sec;
				ts.tv_nsec = tv.tv_usec * 1000;
#endif

                ts.tv_sec += add_secs;
                ts.tv_nsec += add_nsecs;

                pthread_cond_timedwait(&strmh->cb_cond, &strmh->cb_mutex, &ts);
            }

            if (LIKELY(strmh->last_polled_seq < strmh->hold_seq)) {
                _uvc_populate_frame(strmh);
                *frame = &strmh->frame;
                strmh->last_polled_seq = strmh->hold_seq;
            } else {
                *frame = NULL;
            }
        } else {
            *frame = NULL;
        }
    }
    pthread_mutex_unlock(&strmh->cb_mutex);
    return UVC_SUCCESS;
}


/** @internal
 * @brief Swap the working buffer with the presented buffer and notify consumers
 */
static void _uvc_swap_buffers(uvc_stream_handle_t *strmh) {


    pthread_mutex_lock(&strmh->cb_mutex);
    {

        LOGD("strmh->fid = %d",strmh->fid);
        whichVideoFrameDate = strmh->fid;
        videoFrameData[whichVideoFrameDate]->FrameSize = total;
        strmh->hold_bfh_err = strmh->bfh_err;	// XXX
        strmh->hold_bytes = total;
        strmh->hold_last_scr = strmh->last_scr;
        strmh->hold_pts = strmh->pts;
        strmh->hold_seq = strmh->seq;
        pthread_cond_broadcast(&strmh->cb_cond);
    }
    pthread_mutex_unlock(&strmh->cb_mutex);

    strmh->seq++;
    strmh->got_bytes = 0;
    strmh->last_scr = 0;
    strmh->pts = 0;
    strmh->bfh_err = 0;	// XXX
    //total = 0;
}


void eof_cb_jni_stream_Surface(uvc_stream_handle_t *strmh) {




    ueberschreitungDerUebertragungslaenge = 0;
    //_uvc_swap_buffers(strmh);





    if (frameUeberspringen == 0) {
        ++totalFrame;
        if (runningStream == false) stopStreaming();
        if (strcmp(frameFormat, "MJPEG") == 0) {
            ///////////////////   MJPEG     /////////////////////////
            if (!low_Android) {
                unsigned char *jpg_buffer;
                int jpeglength = total;
                int* jpglength_ptr = &jpeglength;
                jpg_buffer = convertMjpegFrameToJpeg(videoFrameData[whichVideoFrameDate]->videoframe, total, jpglength_ptr);
                //LOGD("jpg length jpglength_ptr = %d", *jpglength_ptr);
                unsigned long jpg_size = *jpglength_ptr;
                struct jpeg_decompress_struct cinfo;
                struct jpeg_error_mgr jerr;
                unsigned long rgb_size;
                unsigned char *rgb_buffer;
                int row_stride, width, height, pixel_size;
                cinfo.err = jpeg_std_error(&jerr);
                jpeg_create_decompress(&cinfo);
                jpeg_mem_src(&cinfo, jpg_buffer, jpg_size);
                int rc = jpeg_read_header(&cinfo, TRUE);
                if (rc != 1) {
                    LOGD("File does not seem to be a normal JPEG");
                    exit(EXIT_FAILURE);
                }
                jpeg_start_decompress(&cinfo);
                width = cinfo.output_width;
                height = cinfo.output_height;
                pixel_size = cinfo.output_components;
                //LOGD( "Proc: Image is %d by %d with %d components",width, height, pixel_size);
                rgb_size = width * height * pixel_size;
                rgb_buffer = (unsigned char*) malloc(rgb_size);
                row_stride = width * pixel_size;
                while (cinfo.output_scanline < cinfo.output_height) {
                    unsigned char *buffer_array[1];
                    buffer_array[0] = rgb_buffer + \
						   (cinfo.output_scanline) * row_stride;
                    jpeg_read_scanlines(&cinfo, buffer_array, 1);
                }
                jpeg_finish_decompress(&cinfo);
                jpeg_destroy_decompress(&cinfo);
                uvc_frame_t *rgbx;
                uvc_error_t ret;
                rgbx = uvc_allocate_frame(imageWidth * imageHeight * 4);
                if (!rgbx) {
                    LOGD("unable to allocate rgb frame!");
                    return;
                }
                uvc_rgb2rgbx(rgb_buffer, rgb_size, width, height, rgbx);
                uvc_frame_t *rgb_rot_nFlip = checkRotation(rgbx);
                copyToSurface(rgb_rot_nFlip, &mCaptureWindow);
                if (imageCapture) {
                    imageCapture = false;
                    LOGD("IMAGE CAPT 1");
                    JNIEnv * jenv;
                    int errorCode = (*javaVm)->AttachCurrentThread(javaVm, (void**) &jenv, NULL);
                    jbyteArray array = (*jenv)->NewByteArray(jenv, jpg_size);
                    // Main Activity
                    (*jenv)->SetByteArrayRegion(jenv, array, 0, jpg_size, (jbyte *) jpg_buffer);
                    LOGD("javaPictureVideoCaptureMJPEG");
                    (*jenv)->CallVoidMethod(jenv, mainActivityObj, javaPictureVideoCaptureMJPEG, array);
                    LOGD("complete");
                } else if (imageCapturelongClick) {
                    imageCapturelongClick = false;
                    JNIEnv * jenv;
                    int errorCode = (*javaVm)->AttachCurrentThread(javaVm, (void**) &jenv, NULL);
                    jbyteArray array = (*jenv)->NewByteArray(jenv, jpg_size);
                    // Main Activity
                    (*jenv)->SetByteArrayRegion(jenv, array, 0, jpg_size, (jbyte *) jpg_buffer);
                    (*jenv)->CallVoidMethod(jenv, mainActivityObj, javaPictureVideoCaptureMJPEG, array);
                }
                if (videoCapture) {
                    JNIEnv * jenv;
                    int errorCode = (*javaVm)->AttachCurrentThread(javaVm, (void**) &jenv, NULL);
                    jbyteArray array = (*jenv)->NewByteArray(jenv, jpg_size);
                    // Main Activity
                    (*jenv)->SetByteArrayRegion(jenv, array, 0, jpg_size, (jbyte *) jpg_buffer);
                    (*jenv)->CallVoidMethod(jenv, mainActivityObj, javaPictureVideoCaptureMJPEG, array);
                } else if (videoCaptureLongClick) {
                    JNIEnv * jenv;
                    int errorCode = (*javaVm)->AttachCurrentThread(javaVm, (void**) &jenv, NULL);
                    jbyteArray array = (*jenv)->NewByteArray(jenv, jpg_size);
                    // Main Activity
                    (*jenv)->SetByteArrayRegion(jenv, array, 0, jpg_size, (jbyte *) jpg_buffer);
                    (*jenv)->CallVoidMethod(jenv, mainActivityObj, javaPictureVideoCaptureMJPEG, array);
                }
                free(rgb_buffer);
                uvc_free_frame(rgb_rot_nFlip);
            } else {
                //////      Image Processing For Low Level Android
                uvc_error_t ret;
                // We'll convert the image from YUV/JPEG to BGR, so allocate space
                //rgbx = uvc_allocate_frame(imageWidth * imageHeight * 4);
                unsigned char *rgb_buffer;
                int rgblength = total;
                int* rgblength_ptr = &rgblength;
                rgb_buffer = uvc_mjpeg2rgb(rgblength_ptr);
                uvc_frame_t *rgbx;
                rgbx = uvc_allocate_frame(imageWidth * imageHeight * 4);
                if (!rgbx) {
                    LOGD("unable to allocate rgb frame!");
                    return;
                }
                uvc_rgb2rgbx(rgb_buffer, rgblength, imageWidth, imageHeight, rgbx);
                copyToSurface(rgbx, &mCaptureWindow);
                free(rgb_buffer);
                uvc_free_frame(rgbx);
            }
        } else {   ///////////////////   YUV     /////////////////////////
            //LOGD("YUV ...");
            if (!low_Android) {
                uvc_frame_t *rgbx;
                uvc_error_t ret;
                // We'll convert the image from YUV/JPEG to BGR, so allocate space
                rgbx = uvc_allocate_frame(imageWidth * imageHeight * 4);
                if (!rgbx) {
                    LOGD("unable to allocate rgb frame!");
                    return;
                }
                if (strcmp(frameFormat, "YUY2") == 0) {
                    ret = uvc_yuyv2_rgbx(videoFrameData[whichVideoFrameDate]->videoframe, total, imageWidth, imageHeight, rgbx);
                } else if (strcmp(frameFormat, "UYVY") == 0) {
                    ret = uvc_uyvy2rgbx(videoFrameData[whichVideoFrameDate]->videoframe, total, imageWidth, imageHeight, rgbx);
                }
                uvc_frame_t *rgb_rot_nFlip = checkRotation(rgbx);
                if (imageCapture) {
                    imageCapture = false;
                    uvc_frame_t *rgb;
                    uvc_error_t ret;
                    // We'll convert the image from YUV/JPEG to BGR, so allocate space
                    rgb = uvc_allocate_frame(imageWidth * imageHeight * 3);
                    if (!rgb) {
                        LOGD("unable to allocate rgb frame!");
                        return;
                    }
                    if (strcmp(frameFormat, "YUY2") == 0) {
                        ret = uvc_yuyv2rgb(videoFrameData[whichVideoFrameDate]->videoframe, total, imageWidth, imageHeight, rgb);
                    } else if (strcmp(frameFormat, "UYVY") == 0) {
                        ret = uvc_uyvy2rgb(videoFrameData[whichVideoFrameDate]->videoframe, total, imageWidth, imageHeight, rgb);
                    }
                    JNIEnv * jenv;
                    int errorCode = (*javaVm)->AttachCurrentThread(javaVm, (void**) &jenv, NULL);
                    jbyteArray array = (*jenv)->NewByteArray(jenv, imageWidth * imageHeight * 3);
                    // Main Activity
                    (*jenv)->SetByteArrayRegion(jenv, array, 0, imageWidth * imageHeight * 3, (jbyte *) rgb->data);
                    (*jenv)->CallVoidMethod(jenv, mainActivityObj, javaPictureVideoCaptureRGB, array);
                } else if (imageCapturelongClick) {
                    imageCapturelongClick = false;
                    JNIEnv * jenv;
                    int errorCode = (*javaVm)->AttachCurrentThread(javaVm, (void**) &jenv, NULL);
                    jbyteArray array = (*jenv)->NewByteArray(jenv, total);
                    // Main Activity
                    (*jenv)->SetByteArrayRegion(jenv, array, 0, total, (jbyte *) videoFrameData[whichVideoFrameDate]->videoframe);
                    (*jenv)->CallVoidMethod(jenv, mainActivityObj, javaPictureVideoCaptureYUV, array);
                }
                if (videoCapture) {
                    uvc_frame_t *rgb;
                    uvc_error_t ret;
                    // We'll convert the image from YUV/JPEG to BGR, so allocate space
                    rgb = uvc_allocate_frame(imageWidth * imageHeight * 3);
                    if (!rgb) {
                        printf("unable to allocate rgb frame!");
                        return;
                    }
                    if (strcmp(frameFormat, "YUY2") == 0) {
                        ret = uvc_yuyv2rgb(videoFrameData[whichVideoFrameDate]->videoframe, total, imageWidth, imageHeight, rgb);
                    } else if (strcmp(frameFormat, "UYVY") == 0) {
                        ret = uvc_uyvy2rgb(videoFrameData[whichVideoFrameDate]->videoframe, total, imageWidth, imageHeight, rgb);
                    }
                    JNIEnv * jenv;
                    int errorCode = (*javaVm)->AttachCurrentThread(javaVm, (void**) &jenv, NULL);
                    jbyteArray array = (*jenv)->NewByteArray(jenv, imageWidth * imageHeight * 3);
                    // Main Activity
                    (*jenv)->SetByteArrayRegion(jenv, array, 0, imageWidth * imageHeight * 3, (jbyte *) rgb->data);
                    (*jenv)->CallVoidMethod(jenv, mainActivityObj, javaPictureVideoCaptureRGB, array);
                } else if (videoCaptureLongClick) {
                    JNIEnv * jenv;
                    int errorCode = (*javaVm)->AttachCurrentThread(javaVm, (void**) &jenv, NULL);
                    jbyteArray array = (*jenv)->NewByteArray(jenv, total);
                    // Main Activity
                    (*jenv)->SetByteArrayRegion(jenv, array, 0, total, (jbyte *) videoFrameData[whichVideoFrameDate]->videoframe);
                    (*jenv)->CallVoidMethod(jenv, mainActivityObj, javaPictureVideoCaptureYUV, array);
                }
                copyToSurface(rgb_rot_nFlip, &mCaptureWindow);
                uvc_free_frame(rgb_rot_nFlip);
            } else {
                //////      Image Processing For Low Level Android
                uvc_frame_t *rgbx;
                uvc_error_t ret;
                // We'll convert the image from YUV/JPEG to BGR, so allocate space
                rgbx = uvc_allocate_frame(imageWidth * imageHeight * 4);
                if (!rgbx) {
                    LOGD("unable to allocate rgb frame!");
                    return;
                }
                if (strcmp(frameFormat, "YUY2") == 0) {
                    ret = uvc_yuyv2_rgbx(videoFrameData[whichVideoFrameDate]->videoframe, total, imageWidth, imageHeight, rgbx);
                } else if (strcmp(frameFormat, "UYVY") == 0) {
                    ret = uvc_uyvy2rgbx(videoFrameData[whichVideoFrameDate]->videoframe, total, imageWidth, imageHeight, rgbx);
                }
                copyToSurface(rgbx, &mCaptureWindow);
                uvc_free_frame(rgbx);
            }
        }
    } else {
        LOGD("Länge des Frames (Übersprungener Frame) = %d\n", total);
        frameUeberspringen = 0;
    }

    total = 0;
}

void cb_jni_stream_Surface(struct libusb_transfer *the_transfer) {
    uvc_stream_handle_t *strmh = the_transfer->user_data;
    if (runningStream == false) stopStreaming();
    //LOGD("Iso Transfer Callback Function");
    unsigned char *p;
    int packetLen;
    int i;
    p = the_transfer->buffer;
    int packet_id;
    uint8_t check_header;

    struct libusb_iso_packet_descriptor *pkt;
    size_t header_len;
    uint8_t header_info;

    for (i = 0; i < the_transfer->num_iso_packets; i++, p += maxPacketSize) {

        if (the_transfer->iso_packet_desc[i].status == LIBUSB_TRANSFER_COMPLETED) {
            packetLen = the_transfer->iso_packet_desc[i].actual_length;
            if (packetLen == 0) {
                continue;
            }
            if (packetLen < 2) {
                continue;
            }
            // error packet
            if (p[1] & UVC_STREAM_ERR) // bmHeaderInfoh
            {
                LOGD("UVC_STREAM_ERR --> Package %d", i);
                frameUeberspringen = 1;
                continue;
            }

            if ((strmh->fid != (p[1] & UVC_STREAM_FID) )  && total ) {
                LOGD("UVC_STREAM_FID");
                LOGD("Wert fid = %d, Wert strmh = %d", (p[1] & UVC_STREAM_FID), strmh->fid);
                eof_cb_jni_stream_Surface(strmh);
            }
            strmh->fid = p[1] & UVC_STREAM_FID;

            packetLen -= p[0];
            if (packetLen + total > videoFrameData[strmh->fid]->FrameBufferSize) {
                if (ueberschreitungDerUebertragungslaenge == 1) {
                    ueberschreitungDerUebertragungslaenge = 1;
                    fflush(stdout);
                }
                packetLen = videoFrameData[strmh->fid]->FrameBufferSize - total;
            }
            memcpy(videoFrameData[strmh->fid]->videoframe + total, p + p[0], packetLen);
            total += packetLen;
            if ((p[1] & UVC_STREAM_EOF) && total != 0) {
                LOGD("strmh->fid = %d",strmh->fid);
                eof_cb_jni_stream_Surface(strmh);
            }
        }
    }
    if (runningStream) if (libusb_submit_transfer(the_transfer) != 0) {
            LOGD(stderr, "SUBMISSION FAILED. \n");
        }
    else {
        // Close the Stream Handle
        if (!stopTheStream) {
            stopTheStream = 1;
            pthread_cond_destroy(&strmh->cb_cond);
            pthread_mutex_destroy(&strmh->cb_mutex);
        }
    }
    //LOGD("strmh->fid = %d",strmh->fid);



}


JNIEXPORT void JNICALL Java_humer_UvcCamera_StartIsoStreamActivity_JniStreamOverSurface
        (JNIEnv *env, jobject obj) {
    LOGD("\nJniServiceOverSurface\n");
    if (initialized) {
        uvc_stream_handle_t *strmh = NULL;
        strmh = calloc(1, sizeof(*strmh));
        if (UNLIKELY(!strmh)) {
            int ret = UVC_ERROR_NO_MEM;
            //goto fail;
        }
        strmh->running = 1;
        strmh->seq = 0;
        strmh->fid = 0;
        strmh->pts = 0;
        strmh->last_scr = 0;
        strmh->bfh_err = 0;	// XXX
        LOGD("trying to initialize pthread ...");
        pthread_mutex_init(&strmh->cb_mutex, NULL);
        pthread_cond_init(&strmh->cb_cond, NULL);
        stopTheStream = 0;
        whichVideoFrameDate = 0;

        uint8_t LIBUVC_XFER_BUF_SIZE;
        if (strcmp(frameFormat, "MJPEG") == 0) {
            LIBUVC_XFER_BUF_SIZE = imageWidth * imageHeight * 3;
        } else if (strcmp(frameFormat, "YUY2") == 0 ||   strcmp(frameFormat, "UYVY")   ) {
            LIBUVC_XFER_BUF_SIZE = imageWidth * imageHeight * 2;
        } else if (strcmp(frameFormat, "NV21") == 0) {
            LIBUVC_XFER_BUF_SIZE = imageWidth * imageHeight *  3 / 2;
        } else {
            LIBUVC_XFER_BUF_SIZE = imageWidth * imageHeight * 2;
        }
       // strmh->outbuf = malloc(LIBUVC_XFER_BUF_SIZE);
       // strmh->holdbuf = malloc(LIBUVC_XFER_BUF_SIZE);
        strmh->size_buf = LIBUVC_XFER_BUF_SIZE;





        LOGD("inizialisation complete");
        struct libusb_transfer *xfers[activeUrbs];
        for (i = 0; i < activeUrbs; i++) {
            xfers[i] = libusb_alloc_transfer(packetsPerRequest);
            uint8_t *data = malloc(maxPacketSize * packetsPerRequest);
            libusb_fill_iso_transfer(
                    xfers[i], uvcDeviceHandle->usb_devh, camStreamingEndpoint,
                    data, maxPacketSize * packetsPerRequest, packetsPerRequest,
                    cb_jni_stream_Surface, (void*) strmh, 5000);
            libusb_set_iso_packet_lengths(xfers[i], maxPacketSize);
        }
        runningStream = true;
        for (i = 0; i < activeUrbs; i++) {
            if (libusb_submit_transfer(xfers[i]) != 0) {
                LOGD(stderr, "submit xfer failed.\n");
            }
        }
        LOGD("JniServiceOverSurface -- finished");


        uvc_frame_t *frame;
        uvc_error_t ret;
        // We'll convert the image from YUV/JPEG to BGR, so allocate space
        frame = uvc_allocate_frame(imageWidth * imageHeight *3/ 2);


        /*
        while (runningStream) {
            if (runningStream == false) {
                LOGD("stopped because runningStream == false !!  --> libusb_event_handling STOPPED");
                break;
            }
            libusb_handle_events(uvcContext->usb_ctx);
        }
*/

        //if (uvc_stream_get_frame(strmh, frame, 2000) != UVC_SUCCESS) LOGD("Faild to get the frame");




        //else LOGD("optained a frame");

        //LOGD("Frame length: %d", frame->data_bytes);



    }
}

//////////////////////// Webrtc WebRtc webrtc

void eof_cb_jni_WebRtc_Service(uvc_stream_handle_t *strmh) {
    /*
    pthread_mutex_lock(&strmh->cb_mutex);
    {
        //tmp_buf = strmh->holdbuf;
        strmh->hold_bfh_err = strmh->bfh_err;	// XXX
        strmh->hold_bytes = total;
        //strmh->holdbuf = strmh->outbuf;
        //strmh->outbuf = tmp_buf;
        strmh->hold_last_scr = strmh->last_scr;
        strmh->hold_pts = strmh->pts;
        strmh->hold_seq = strmh->seq;
        pthread_cond_broadcast(&strmh->cb_cond);
    }
    pthread_mutex_unlock(&strmh->cb_mutex);
*/
    ueberschreitungDerUebertragungslaenge = 0;
    if (frameUeberspringen == 0) {
        ++totalFrame;
        if (runningStream == false) stopStreaming();
        if (strcmp(frameFormat, "MJPEG") == 0) {

            JNIEnv * jenv;
            int errorCode = (*javaVm)->AttachCurrentThread(javaVm, (void**) &jenv, NULL);
            jbyteArray array = (*jenv)->NewByteArray(jenv, total);
            (*jenv)->SetByteArrayRegion(jenv, array, 0, total, (jbyte *) videoFrameData[whichVideoFrameDate]->videoframe);
            // Service
            (*jenv)->CallVoidMethod(jenv, mainActivityObj, javaProcessReceivedMJpegVideoFrameKamera, array);
        } else {
            LOGD("frameFormat = %s", frameFormat);
            uvc_frame_t *yuyv;
            uvc_error_t ret;
            // We'll convert the image from YUV/JPEG to BGR, so allocate space
            yuyv = uvc_allocate_frame(imageWidth * imageHeight *3/ 2);
            uvc_duplicate_frame(yuyv);
            uvc_frame_t *nv21;
            // We'll convert the image from YUV/JPEG to BGR, so allocate space
            nv21 = uvc_allocate_frame(imageWidth * imageHeight * 3 / 2);
            uvc_yuyv2iyuv420SP (yuyv, nv21);
            JNIEnv * jenv;
            int errorCode = (*javaVm)->AttachCurrentThread(javaVm, (void**) &jenv, NULL);
            jbyteArray array = (*jenv)->NewByteArray(jenv, nv21->data_bytes);
            (*jenv)->SetByteArrayRegion(jenv, array, 0, nv21->data_bytes, (jbyte *) nv21->data);
            // Service
            LOGD("CallVoidMethod");
            (*jenv)->CallVoidMethod(jenv, mainActivityObj, javaRetrievedFrameFromLibUsb21, array);
            uvc_free_frame(nv21);
        }
    } else {
        LOGD("Länge des Frames (Übersprungener Frame) = %d\n", total);
        frameUeberspringen = 0;
    }
    total = 0;
}

void cb_jni_WebRtc_Service(struct libusb_transfer *the_transfer) {
    uvc_stream_handle_t *strmh = the_transfer->user_data;
    //LOGD("Iso Transfer Callback Function");
    unsigned char *p;
    int packetLen;
    int i;
    p = the_transfer->buffer;
    for (i = 0; i < the_transfer->num_iso_packets; i++, p += maxPacketSize) {
        if (the_transfer->iso_packet_desc[i].status == LIBUSB_TRANSFER_COMPLETED) {
            packetLen = the_transfer->iso_packet_desc[i].actual_length;
            if (packetLen < 2) {
                continue;
            }
            // error packet
            if (p[1] & UVC_STREAM_ERR) // bmHeaderInfoh
            {
                LOGD("UVC_STREAM_ERR --> Package %d", i);
                frameUeberspringen = 1;
                continue;
            }
            /* The frame ID bit was flipped, but we have image data sitting
        around from prior transfers. This means the camera didn't send
        an EOF for the last transfer of the previous frame or some frames losted. */
            if ((strmh->fid != (p[1] & UVC_STREAM_FID) )  && total ) {

                LOGD("UVC_STREAM_FID");
                LOGD("Wert fid = %d, Wert strmh = %d", (p[1] & UVC_STREAM_FID), strmh->fid);
                eof_cb_jni_WebRtc_Service(strmh);
            }
            strmh->fid = p[1] & UVC_STREAM_FID;

            packetLen -= p[0];
            if (packetLen + total > videoFrameData[whichVideoFrameDate]->FrameBufferSize) {
                if (ueberschreitungDerUebertragungslaenge == 1) {
                    ueberschreitungDerUebertragungslaenge = 1;
                    fflush(stdout);
                }
                packetLen = videoFrameData[whichVideoFrameDate]->FrameBufferSize - total;
            }
            memcpy(videoFrameData[whichVideoFrameDate]->videoframe + total, p + p[0], packetLen);
            total += packetLen;
            if ((p[1] & UVC_STREAM_EOF) && total != 0) {
                eof_cb_jni_WebRtc_Service(strmh);
            }
        }
    }
    if (runningStream) if (libusb_submit_transfer(the_transfer) != 0) {
            LOGD(stderr, "SUBMISSION FAILED. \n");
        }
}

JNIEXPORT void JNICALL Java_com_example_androidthings_videortc_UsbCapturer_JniWebRtcJavaMethods
        (JNIEnv *env, jobject obj) {
    int status = (*env)->GetJavaVM(env, &javaVm);
    if(status != 0) {
        LOGE("failed to attach javaVm");
    }
    class = (*env)->GetObjectClass(env, obj);
    jniHelperClass = (*env)->NewGlobalRef(env, class);
    mainActivityObj = (*env)->NewGlobalRef(env, obj);
    javaRetrievedFrameFromLibUsb21 = (*env)->GetMethodID(env, jniHelperClass, "retrievedFrameFromLibUsbNV21", "([B)V");
    javaProcessReceivedMJpegVideoFrameKamera = (*env)->GetMethodID(env, jniHelperClass, "processReceivedMJpegVideoFrameKamera", "([B)V");
    javaWEBrtcProcessReceivedVideoFrameYuv = (*env)->GetMethodID(env, jniHelperClass, "usbCapturerProcessReceivedVideoFrameYuvFromJni", "([B)V");
    LOGD("WeBRTC - Native Methods and Values from JNI initialized");
}

void prepairTheStream_WebRtc_Service() {
    LOGD("prepairTheStream_WebRtc_Service()");

    if (cameraDevice_is_wraped) {
        LOGD("native_uvc_unref_device()");
        native_uvc_unref_device();
        LOGD("rewrap()");
        wrap_camera_device(fd, &uvcContext, NULL, &uvcDeviceHandle, mDevice);
    }

    probeCommitControl(bmHint, camFormatIndex, camFrameIndex, camFrameInterval, fd);

    int r = libusb_set_interface_alt_setting(uvcDeviceHandle->usb_devh, camStreamingInterfaceNum,
                                             camStreamingAltSetting); // camStreamingAltSetting = 7;    // 7 = 3x1024 bytes packet size
    if (r != LIBUSB_SUCCESS) {
        LOGD("libusb_set_interface_alt_setting(uvcDeviceHandle->usb_devh, 1, 1) failed with error %d\n", r);
    } else {
        LOGD("Altsettings sucessfully set:\nAltsetting = %d\n",
             camStreamingAltSetting);
    }
    if (activeUrbs > 16) activeUrbs = 16;
}

void lunchTheStream_WebRtc_Service() {
    LOGD("lunchTheStream_WebRtc_Service()");
    if (initialized) {
        struct libusb_transfer *xfers[activeUrbs];
        for (i = 0; i < activeUrbs; i++) {
            xfers[i] = libusb_alloc_transfer(packetsPerRequest);
            uint8_t *data = malloc(maxPacketSize * packetsPerRequest);
            libusb_fill_iso_transfer(
                    xfers[i], uvcDeviceHandle->usb_devh, camStreamingEndpoint,
                    data, maxPacketSize * packetsPerRequest, packetsPerRequest,
                    cb_jni_WebRtc_Service, NULL, 5000);
            libusb_set_iso_packet_lengths(xfers[i], maxPacketSize);
        }
        runningStream = true;
        for (i = 0; i < activeUrbs; i++) {
            if (libusb_submit_transfer(xfers[i]) != 0) {
                LOGD(stderr, "submit xfer failed.\n");
            }
        }

        /*
        while (runningStream) {
            if (runningStream == false) {
                LOGD("stopped because runningStream == false !!  --> libusb_event_handling STOPPED");
                break;
            }
            libusb_handle_events(uvcContext->usb_ctx);
        }
         */
        LOGD("ISO Stream Start -- finished");
    }
}

// Frame Conversation JNA METHOD:
unsigned char* convertUYVYtoJPEG (unsigned char* UYVY_frame_array, int* jpgLength, int UYVYframeLength, int width, int height) {
    uvc_frame_t *rgb;
    // We'll convert the image from YUV/JPEG to BGR, so allocate space
    rgb = uvc_allocate_frame(width * height * 4);
    if (!rgb) {
        printf("unable to allocate rgb frame!");
        return NULL;
    }
    uvc_error_t ret;
    ret = uvc_uyvy2rgbx(UYVY_frame_array, UYVYframeLength, width, height, rgb);
    if (ret) {
        uvc_perror(ret, "uvc_any2bgr");
        uvc_free_frame(rgb);
        return NULL;
    }
    const int JPEG_QUALITY = 100;
    const int COLOR_COMPONENTS = 3;
    int _width = rgb->width;
    int _height = rgb->height;
    long unsigned int _jpegSize = 0;
    unsigned char* _compressedImage = NULL; //!< Memory is allocated by tjCompress2 if _jpegSize == 0
    tjhandle _jpegCompressor = tjInitCompress();
    int r = tjCompress2(_jpegCompressor, rgb->data, _width, _width*4, _height, TJPF_RGBA,
                          &_compressedImage, &_jpegSize, TJSAMP_444, JPEG_QUALITY,
                          TJFLAG_FASTDCT);
    if (r == 0) LOGD("tjCompress2 sucessful, ret = %d", ret);
    else LOGD("tjCompress2 failed !!!!!!, ret = %d", ret);
    if (r != 0 ) return NULL;
    LOGD("_jpegSize = %d", _jpegSize);
    *jpgLength = _jpegSize;
    tjDestroy(_jpegCompressor);
    return _compressedImage;
}

// Frame Conversation JNA METHOD:
unsigned char* convertYUY2toJPEG (unsigned char* YUY2_frame_array, int* jpgLength, int YUY2frameLength, int width, int height) {

    uvc_frame_t *rgb;
    // We'll convert the image from YUV/JPEG to BGR, so allocate space
    rgb = uvc_allocate_frame(width * height * 4);
    if (!rgb) {
        printf("unable to allocate rgb frame!");
        return NULL;
    }
    uvc_error_t ret;
    ret = uvc_yuyv2_rgbx(YUY2_frame_array, YUY2frameLength, width, height, rgb);
    if (ret) {
        uvc_perror(ret, "uvc_any2bgr");
        uvc_free_frame(rgb);
        return NULL;
    }
    const int JPEG_QUALITY = 100;
    const int COLOR_COMPONENTS = 3;
    int _width = rgb->width;
    int _height = rgb->height;
    long unsigned int _jpegSize = 0;
    unsigned char* _compressedImage = NULL; //!< Memory is allocated by tjCompress2 if _jpegSize == 0
    tjhandle _jpegCompressor = tjInitCompress();
    int r = tjCompress2(_jpegCompressor, rgb->data, _width, _width*4, _height, TJPF_RGBA,
                        &_compressedImage, &_jpegSize, TJSAMP_444, JPEG_QUALITY,
                        TJFLAG_FASTDCT);
    if (r == 0) LOGD("tjCompress2 sucessful, ret = %d", ret);
    else LOGD("tjCompress2 failed !!!!!!, ret = %d", ret);
    if (r != 0 ) return NULL;
    LOGD("_jpegSize = %d", _jpegSize);
    *jpgLength = _jpegSize;
    tjDestroy(_jpegCompressor);
    return _compressedImage;
}

// Frame Conversation JNI METHOD:
void
Java_humer_UvcCamera_StartIsoStreamActivity_UYVYpixeltobmp( JNIEnv* env, jobject thiz, jbyteArray data, jobject bitmap, int im_width, int im_height){
    jsize num_bytes = (*env)->GetArrayLength(env, data);
    unsigned char* UYVY_frame_array;
    UYVY_frame_array = (char *) malloc (num_bytes);
    jbyte  *lib = (*env)->GetByteArrayElements(env , data, 0);
    memcpy ( UYVY_frame_array , lib , num_bytes ) ;
    uvc_frame_t *rgbx;
    // We'll convert the image from YUV/JPEG to BGR, so allocate space
    rgbx = uvc_allocate_frame(im_width * im_height * 4);
    if (!rgbx) {
        LOGD("unable to allocate rgb frame!");
        return;
    }
    uvc_error_t ret;
    int UYVYframeLength = num_bytes;
    ret = uvc_uyvy2rgbx(UYVY_frame_array, UYVYframeLength, im_width, im_height, rgbx);
    AndroidBitmapInfo  info;
    void*              pixels;
    int i;
    int *colors;
    int width=0;
    int height=0;
    if ((ret = AndroidBitmap_getInfo(env, bitmap, &info)) < 0) {
        LOGD("AndroidBitmap_getInfo() failed ! error=%d", ret);
        LOGE("AndroidBitmap_getInfo() failed ! error=%d", ret);
        return;
    }
    width = info.width;
    height = info.height;
    if (info.format != ANDROID_BITMAP_FORMAT_RGBA_8888) {
        LOGD("Bitmap format is not RGBA_8888 !");
        LOGE("Bitmap format is not RGBA_8888 !");
        return;
    }
    if ((ret = AndroidBitmap_lockPixels(env, bitmap, &pixels)) < 0) {
        LOGD("AndroidBitmap_lockPixels() failed ! error=%d", ret);
        LOGE("AndroidBitmap_lockPixels() failed ! error=%d", ret);
    }
    memcpy(pixels, rgbx->data, width*height*4);
    AndroidBitmap_unlockPixels(env, bitmap);
}

// Frame Conversation JNI METHOD:
void
Java_humer_UvcCamera_StartIsoStreamActivity_YUY2pixeltobmp( JNIEnv* env, jobject thiz, jbyteArray data, jobject bitmap, int im_width, int im_height){
    jsize num_bytes = (*env)->GetArrayLength(env, data);
    unsigned char* YUY2_frame_array;
    YUY2_frame_array = (char *) malloc (num_bytes);
    jbyte  *lib = (*env)->GetByteArrayElements(env , data, 0);
    memcpy ( YUY2_frame_array , lib , num_bytes ) ;
    uvc_frame_t *rgbx;
    // We'll convert the image from YUV/JPEG to BGR, so allocate space
    rgbx = uvc_allocate_frame(im_width * im_height * 4);
    if (!rgbx) {
        LOGD("unable to allocate rgb frame!");
        return;
    }
    uvc_error_t ret;
    int YUY2frameLength = num_bytes;
    ret = uvc_yuyv2_rgbx(YUY2_frame_array, YUY2frameLength, im_width, im_height, rgbx);
    AndroidBitmapInfo  info;
    void*              pixels;
    int i;
    int *colors;
    int width=0;
    int height=0;
    if ((ret = AndroidBitmap_getInfo(env, bitmap, &info)) < 0) {
        LOGD("AndroidBitmap_getInfo() failed ! error=%d", ret);
        LOGE("AndroidBitmap_getInfo() failed ! error=%d", ret);
        return;
    }
    width = info.width;
    height = info.height;
    if (info.format != ANDROID_BITMAP_FORMAT_RGBA_8888) {
        LOGD("Bitmap format is not RGBA_8888 !");
        LOGE("Bitmap format is not RGBA_8888 !");
        return;
    }
    if ((ret = AndroidBitmap_lockPixels(env, bitmap, &pixels)) < 0) {
        LOGD("AndroidBitmap_lockPixels() failed ! error=%d", ret);
        LOGE("AndroidBitmap_lockPixels() failed ! error=%d", ret);
    }
    memcpy(pixels, rgbx->data, width*height*4);
    AndroidBitmap_unlockPixels(env, bitmap);
}

// Bitmap Method

void JNICALL Java_humer_UvcCamera_StartIsoStreamActivity_frameToBitmap( JNIEnv* env, jobject thiz, jobject bitmap) {

    uvc_frame_t *rgbx;
    uvc_frame_t *rgb;
    uvc_error_t ret;
    // We'll convert the image from YUV/JPEG to RGBX, so allocate space
    rgbx = uvc_allocate_frame(imageWidth * imageHeight * 4);
    if (!rgbx) {
        LOGD("unable to allocate rgb frame!");
        return;
    }
    if (strcmp(frameFormat, "YUY2") == 0) {
        ret = uvc_yuyv2_rgbx(videoFrameData[whichVideoFrameDate]->videoframe, total, imageWidth, imageHeight, rgbx);
    } else if (strcmp(frameFormat, "UYVY") == 0) {
        ret = uvc_uyvy2rgbx(videoFrameData[whichVideoFrameDate]->videoframe, total, imageWidth, imageHeight, rgbx);
    } else if (strcmp(frameFormat, "MJPEG") == 0) {
        uvc_frame_t *rgb;
        unsigned char *rgb_buffer;
        int rgblength = total;
        int* rgblength_ptr = &rgblength;
        ret = uvc_mjpeg2rgb(rgblength_ptr);


        uvc_rgb2rgbx(rgb->data, rgb->data_bytes, rgb->width, rgb->height, rgbx);
        uvc_free_frame(rgb);
    }

    AndroidBitmapInfo  info;
    void*              pixels;
    int i;
    int *colors;
    int width=0;
    int height=0;
    if ((ret = AndroidBitmap_getInfo(env, bitmap, &info)) < 0) {
        LOGD("AndroidBitmap_getInfo() failed ! error=%d", ret);
        LOGE("AndroidBitmap_getInfo() failed ! error=%d", ret);
        return;
    }
    width = info.width;
    height = info.height;
    if (info.format != ANDROID_BITMAP_FORMAT_RGBA_8888) {
        LOGD("Bitmap format is not RGBA_8888 !");
        LOGE("Bitmap format is not RGBA_8888 !");
        return;
    }
    if ((ret = AndroidBitmap_lockPixels(env, bitmap, &pixels)) < 0) {
        LOGD("AndroidBitmap_lockPixels() failed ! error=%d", ret);
        LOGE("AndroidBitmap_lockPixels() failed ! error=%d", ret);
    }
    memcpy(pixels, rgbx->data, width*height*4);
    AndroidBitmap_unlockPixels(env, bitmap);
}

/* // Service Methods
 *
JNIEXPORT void JNICALL Java_humer_UvcCamera_LibUsb_StartIsoStreamService_JniServiceOverSurface
        (JNIEnv *env, jobject obj) {
    LOGD("\nJniServiceOverSurface\n");
    if (initialized) {
        uvc_stream_handle_t *strmh = NULL;
        strmh = calloc(1, sizeof(*strmh));
        if (UNLIKELY(!strmh)) {
            int ret = UVC_ERROR_NO_MEM;
            //goto fail;
        }
        strmh->running = 1;
        strmh->seq = 0;
        strmh->fid = 0;
        strmh->pts = 0;
        strmh->last_scr = 0;
        strmh->bfh_err = 0;	// XXX
        LOGD("trying to initialize pthread ...");
        pthread_mutex_init(&strmh->cb_mutex, NULL);
        pthread_cond_init(&strmh->cb_cond, NULL);
        LOGD("inizialisation complete");
        struct libusb_transfer *xfers[activeUrbs];
        for (i = 0; i < activeUrbs; i++) {
            xfers[i] = libusb_alloc_transfer(packetsPerRequest);
            uint8_t *data = malloc(maxPacketSize * packetsPerRequest);
            libusb_fill_iso_transfer(
                    xfers[i], uvcDeviceHandle->usb_devh, camStreamingEndpoint,
                    data, maxPacketSize * packetsPerRequest, packetsPerRequest,
                    cb_jni_stream_Surface_Service, (void*) strmh, 5000);
            libusb_set_iso_packet_lengths(xfers[i], maxPacketSize);
        }
        runningStream = true;
        for (i = 0; i < activeUrbs; i++) {
            if (libusb_submit_transfer(xfers[i]) != 0) {
                LOGD(stderr, "submit xfer failed.\n");
            }
        }

        while (runningStream) {
            if (runningStream == false) {
                LOGD("stopped because runningStream == false !!  --> libusb_event_handling STOPPED");
                break;
            }
            libusb_handle_events(uvcContext->usb_ctx);
        }
        LOGD("JniServiceOverSurface -- finished");
    }
}

 JNIEXPORT void JNICALL Java_humer_UvcCamera_LibUsb_StartIsoStreamService_JniServiceOverBitmap
        (JNIEnv *env, jobject obj) {
    if (initialized) {
        uvc_stream_handle_t *strmh = NULL;
        strmh = calloc(1, sizeof(*strmh));
        if (UNLIKELY(!strmh)) {
            int ret = UVC_ERROR_NO_MEM;
            //goto fail;
        }
        strmh->running = 1;
        strmh->seq = 0;
        strmh->fid = 0;
        strmh->pts = 0;
        strmh->last_scr = 0;
        strmh->bfh_err = 0;	// XXX
        struct libusb_transfer *xfers[activeUrbs];
        for (i = 0; i < activeUrbs; i++) {
            xfers[i] = libusb_alloc_transfer(packetsPerRequest);
            uint8_t *data = malloc(maxPacketSize * packetsPerRequest);
            libusb_fill_iso_transfer(
                    xfers[i], uvcDeviceHandle->usb_devh, camStreamingEndpoint,
                    data, maxPacketSize * packetsPerRequest, packetsPerRequest,
                    cb_jni_stream_Bitmap_Service, (void*) strmh, 5000);
            libusb_set_iso_packet_lengths(xfers[i], maxPacketSize);
        }
        runningStream = true;
        for (i = 0; i < activeUrbs; i++) {
            if (libusb_submit_transfer(xfers[i]) != 0) {
                LOGD(stderr, "submit xfer failed.\n");
            }
        }
        while (runningStream) {
            if (runningStream == false) {
                LOGD("stopped because runningStream == false !!  --> libusb_event_handling STOPPED");
                break;
            }
            libusb_handle_events(uvcContext->usb_ctx);
        }
        LOGD("ISO Stream Start -- finished");
    }
}

 JNIEXPORT void JNICALL Java_humer_UvcCamera_StartIsoStreamActivity_JniGetAnotherFrame
        (JNIEnv *env, jobject obj, jint stream, jint frameIndex) {
    if (initialized) {
        uvc_stream_handle_t *strmh = NULL;
        strmh = calloc(1, sizeof(*strmh));
        if (UNLIKELY(!strmh)) {
            int ret = UVC_ERROR_NO_MEM;
            //goto fail;
        }
        strmh->running = 1;
        strmh->seq = 0;
        strmh->fid = 0;
        strmh->pts = 0;
        strmh->last_scr = 0;
        strmh->bfh_err = 0;	// XXX
        struct libusb_transfer *xfers[activeUrbs];
        for (i = 0; i < activeUrbs; i++) {
            xfers[i] = libusb_alloc_transfer(packetsPerRequest);
            uint8_t *data = malloc(maxPacketSize * packetsPerRequest);
            libusb_fill_iso_transfer(
                    xfers[i], uvcDeviceHandle->usb_devh, camStreamingEndpoint,
                    data, maxPacketSize * packetsPerRequest, packetsPerRequest,
                    cb_jni_stream_ImageView, (void*) strmh, 5000);
            libusb_set_iso_packet_lengths(xfers[i], maxPacketSize);
        }
        runningStream = true;
        for (i = 0; i < activeUrbs; i++) {
            if (libusb_submit_transfer(xfers[i]) != 0) {
                LOGD(stderr, "submit xfer failed.\n");
            }
        }
        runningStream = true;
        while (runningStream) {
            if (runningStream == false) {
                LOGD("stopped because runningStream == false !!  --> libusb_event_handling STOPPED");
                break;
            }
            libusb_handle_events(uvcContext->usb_ctx);
        }
        LOGD("ISO Stream Start -- finished");
    }
}

 // Init SurfaceView for Service
JNIEXPORT void JNICALL Java_humer_UvcCamera_LibUsb_StartIsoStreamService_JniPrepairForStreamingfromService
        (JNIEnv *env, jobject obj) {
    if (initialized) {
        probeCommitControl(bmHint, camFormatIndex, camFrameIndex, camFrameInterval, fd);
        runningStream = true;
        int r = libusb_set_interface_alt_setting(uvcDeviceHandle->usb_devh, camStreamingInterfaceNum,
                                                 camStreamingAltSetting); // camStreamingAltSetting = 7;    // 7 = 3x1024 bytes packet size
        if (r != LIBUSB_SUCCESS) {
            LOGD("libusb_set_interface_alt_setting(uvcDeviceHandle->usb_devh, 1, 1) failed with error %d\n", r);
        } else {
            LOGD("Die Alternativeinstellungen wurden erfolgreich gesetzt: %d ; Altsetting = %d\n", r,
                 camStreamingAltSetting);
        }
        if (activeUrbs > 16) activeUrbs = 16;
    }
}

 */

/*
 *
void eof_cb_jni_stream_Bitmap_Service(uvc_stream_handle_t *strmh) {

ueberschreitungDerUebertragungslaenge = 0;
if (frameUeberspringen == 0) {
++totalFrame;
if (runningStream == false) stopStreaming();
if (strcmp(frameFormat, "MJPEG") == 0) {
///////////////////   MJPEG     /////////////////////////
// LOGD("MJPEG");
LOGD("calling sendFrameComplete");
//runningStream = false;
sendFrameComplete(total);
if (imageCapture) {
imageCapture = false;
LOGD("complete");
} else if (imageCapturelongClick) {
imageCapturelongClick = false;
}
} else {   ///////////////////   YUV     /////////////////////////
sendFrameComplete(total);
if (imageCapture) {
imageCapture = false;
LOGD("complete");
} else if (imageCapturelongClick) {
imageCapturelongClick = false;
}
}
} else {
LOGD("Länge des Frames (Übersprungener Frame) = %d\n", total);
frameUeberspringen = 0;
}
total = 0;
}

void cb_jni_stream_Bitmap_Service(struct libusb_transfer *the_transfer) {
    uvc_stream_handle_t *strmh = the_transfer->user_data;
    if (runningStream == false) stopStreaming();
    //LOGD("Iso Transfer Callback Function");
    unsigned char *p;
    int packetLen;
    int i;
    p = the_transfer->buffer;
    for (i = 0; i < the_transfer->num_iso_packets; i++, p += maxPacketSize) {
        if (the_transfer->iso_packet_desc[i].status == LIBUSB_TRANSFER_COMPLETED) {
            packetLen = the_transfer->iso_packet_desc[i].actual_length;
            if (packetLen == 0) {
                continue;
            }
            if (packetLen < 2) {
                continue;
            }
            // error packet
            if (p[1] & UVC_STREAM_ERR) // bmHeaderInfoh
            {
                LOGD("UVC_STREAM_ERR --> Package %d", i);
                frameUeberspringen = 1;
                continue;
            }

            if ((total != p[1] & UVC_STREAM_FID )  && total ) {
                eof_cb_jni_stream_Bitmap_Service(strmh);
            }
            total = p[1] & UVC_STREAM_FID;
            packetLen -= p[0];
            if (packetLen + total > videoFrameData->FrameBufferSize) {
                if (ueberschreitungDerUebertragungslaenge == 1) {
                    ueberschreitungDerUebertragungslaenge = 1;
                    fflush(stdout);
                }
                packetLen = videoFrameData->FrameBufferSize - total;
            }
            memcpy(videoFrameData->videoframe + total, p + p[0], packetLen);
            total += packetLen;
            if ((p[1] & UVC_STREAM_EOF) && total != 0) {
                eof_cb_jni_stream_Bitmap_Service(strmh);
            }

        }
    }
    if (runningStream) if (libusb_submit_transfer(the_transfer) != 0) {
            LOGD(stderr, "SUBMISSION FAILED. \n");
        }
}
 */


