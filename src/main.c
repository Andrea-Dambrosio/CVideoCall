#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/videodev2.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include "app/errors.h"
#include "common/utils.h"

#define WIDTH 1920
#define HEIGHT 1080
#define YUYV_PITCH WIDTH * 2

struct buffer {
    void *start;
    size_t length;
};
int fd = -1;
struct buffer *buffers = NULL;
unsigned int bufc = 0;
extern bool VERBOSE;

SDL_Window *window = NULL;
SDL_Renderer *renderer = NULL;
SDL_Texture *texture = NULL;

void clean_up() {
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    printVerbose("Starting clean_up...\n");

    if (fd != -1) {
        printVerbose("Stopping streming...\n");
        if (-1 == xioctl(fd, VIDIOC_STREAMOFF, &type))
            fprintf(stderr, "Cannot stop camera streaming: %s\n",
                    strerror(errno));
    } else {
        printVerbose("No open device.\n");
    }

    if (buffers != NULL) {
        printVerbose("Unmupping buffers...\n");
        for (unsigned int i = 0; i < bufc; i++)
            munmap(buffers[i].start, buffers[i].length);
        free(buffers);
        buffers = NULL;
    } else {
        printVerbose("No buffers to unmap\n");
    }

    if (fd != -1) {
        printVerbose("Closing device...\n");
        if (-1 == close(fd))
            fprintf(stderr, "Cannot close device: %s", strerror(errno));
        fd = -1;
    }

    if (texture != NULL) {
        printVerbose("Destroy texture...\n");
        SDL_DestroyTexture(texture);
        texture = NULL;
    }
    if (renderer != NULL) {
        printVerbose("Destroy renderer...\n");
        SDL_DestroyRenderer(renderer);
        renderer = NULL;
    }
    if (window != NULL) {
        printVerbose("Destroy window\n");
        SDL_DestroyWindow(window);
        window = NULL;
    }
    SDL_Quit();

    printVerbose("Clean up completed.\n");
}

void open_device(char *dev_name) {
    struct stat st;
    printVerbose("Opening device: %s\n", dev_name);
    // adding NON_BLOCKING modify the behavior of ioctl VIDIOC_DBUF
    fd = open(dev_name, O_RDWR);

    if (-1 == fd)
        fatal_errno("Cannot open device");

    if (-1 == fstat(fd, &st))
        fatal_errno("Cannot identify device");

    if (!S_ISCHR(st.st_mode))
        msg_exit("This is not a device");
}
void setup_device() {
    struct v4l2_capability cap;
    enum v4l2_priority priority = V4L2_PRIORITY_RECORD;
    struct v4l2_format fmt;
    printVerbose("Setting up device\n.");
    if (-1 == xioctl(fd, VIDIOC_QUERYCAP, &cap)) {
        if (EINVAL == errno)
            fatal_errno("This is not a V4L2 device");
        else
            fatal_errno("Cannot get device capabilites");
    }
    printVerbose("Opened device name: %s\n", cap.card);
    printVerbose("Device location: %s\n", cap.bus_info);
    printVerbose("Global device capabilities: 0x%08X\n", cap.capabilities);
    printVerbose("Local device capabilites: 0x%08X\n", cap.device_caps);

    printVerbose("Check if the device have the correct capabilities for video "
                 "capture..\n");

    if (!(cap.device_caps & V4L2_CAP_VIDEO_CAPTURE))
        fatal_errno(
            "Video Capture interface is not available on this device.\n");

    if (!(cap.device_caps & V4L2_CAP_STREAMING))
        fatal_errno("This device does not support streaming i/o\n");

    if (-1 == xioctl(fd, VIDIOC_G_PRIORITY, &priority))
        fatal_errno("Cannot get maximum priority on device");

    CLEAR(fmt);
    printVerbose("Negotiating format...\n");
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (-1 == xioctl(fd, VIDIOC_G_FMT, &fmt))
        fatal_errno("Cannot get previous video format");

    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_MJPEG;
    fmt.fmt.pix.height = HEIGHT;
    fmt.fmt.pix.width = WIDTH;
    fmt.fmt.pix.field = V4L2_FIELD_ANY;

    if (-1 == xioctl(fd, VIDIOC_S_FMT, &fmt))
        fatal_errno("Cannot negotiate video format");
    printVerbose("Setup completed\n");
    if (fmt.fmt.pix.pixelformat != V4L2_PIX_FMT_MJPEG)
        msg_exit("Cannot negotiate MJPEG format");
    // if(-1 == xioctl(fd, VIDIOC_G_PARM)) to set the frame-rate
}
void setup_mmap() {
    struct v4l2_requestbuffers reqbuf;
    CLEAR(reqbuf);
    printVerbose("Allocate and map buffers...\n");

    reqbuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    reqbuf.memory = V4L2_MEMORY_MMAP;
    reqbuf.count = 4;

    // allocazione di 4 buffers in memoria fisica
    if (-1 == xioctl(fd, VIDIOC_REQBUFS, &reqbuf))
        fatal_errno("Cannot allocate buffers for streaming");

    if (reqbuf.count < 4)
        msg_exit("Not enough buffer memory\n");
    printVerbose("Allocated %d buffers.\n", reqbuf.count);

    buffers = calloc(reqbuf.count, sizeof(*buffers));

    if (buffers == NULL)
        msg_exit("Out of memory\n");
    printVerbose("Start buffer mapping\n");
    // Mappatura dei buffers nello spazio di indirizzamento del programma
    for (bufc = 0; bufc < reqbuf.count; bufc++) {
        struct v4l2_buffer buf;
        CLEAR(buf);

        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = bufc;

        if (-1 == xioctl(fd, VIDIOC_QUERYBUF, &buf))
            fatal_errno("Cannot query buffer");

        buffers[bufc].length = buf.length;
        buffers[bufc].start = mmap(NULL, buf.length, PROT_READ | PROT_WRITE,
                                   MAP_SHARED, fd, buf.m.offset);

        if (MAP_FAILED == buffers[bufc].start)
            fatal_errno("mmap failed");
    }
    printVerbose("Buf mapping completed\n");
}
void start_streaming() {
    // fill the queque
    struct v4l2_buffer buf;
    enum v4l2_buf_type type;
    printVerbose("Filling buffer queque...\n");
    for (unsigned int i = 0; i < bufc; i++) {
        CLEAR(buf);
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;

        if (-1 == xioctl(fd, VIDIOC_QBUF, &buf))
            fatal_errno("Cannot enqueque buffer");
    }
    // start the streaming from the camera
    type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    printVerbose("Starting streaming...\n");
    if (-1 == xioctl(fd, VIDIOC_STREAMON, &type))
        fatal_errno("Cannot start video streaming");
}
void render_frame(void *buff) {
    void *pixels;
    int locked_pitch = 0;

    if (false == SDL_LockTexture(texture, NULL, &pixels, &locked_pitch))
        SDL_fatal("Cannot write buffer into GPU texture");

    if (locked_pitch == YUYV_PITCH) {
        memcpy(pixels, buff, YUYV_PITCH * HEIGHT); // or buff.length
    } else {
        for (int i = 0; i < HEIGHT; i++) // GPU require a different padding
            memcpy(pixels + i * locked_pitch, buff + i * YUYV_PITCH,
                   YUYV_PITCH);
    }

    SDL_UnlockTexture(texture);
    // SDL_RenderClear(renderer);
    SDL_RenderTexture(renderer, texture, NULL, NULL);
}
void main_loop() {
    struct v4l2_buffer buf;
    SDL_Event event;
    int run = 1;
    printVerbose("Iniziating main thread loop\n");
    // in futuro è importante implementare l'attesa tramite select
    // utilizzando delle aperture non bloccanti. In questo modo con select
    // il programma può mettersi in attesa per più eventi (video/audio/etc)
    while (run) {

        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT) {
                run = false;
                printVerbose("Quit event called\n");
            }
        }

        CLEAR(buf);
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        // this is blocking (as set into open), to minimize resource usage
        // in futre i should use ecall or select to multiplex the inputs
        if (-1 == xioctl(fd, VIDIOC_DQBUF, &buf))
            fatal_errno("Cannot dequeque buffer");

        if (buf.flags & V4L2_BUF_FLAG_ERROR)
            fprintf(stderr, "A buffer may be corrupted for a single frame.\n");

        assert(buf.index < bufc);

        render_frame(buffers[buf.index].start);

        if (-1 == xioctl(fd, VIDIOC_QBUF, &buf))
            fatal_errno("Cannot re-enqueque buffer");
    }
    printVerbose("Exited main main_loop\n");
}
int SDL_setup() {
    if (!SDL_Init(SDL_INIT_VIDEO))
        SDL_fatal("Errore SDL_init");

    if (!SDL_CreateWindowAndRenderer("Webcam", WIDTH, HEIGHT, 0, &window,
                                     &renderer))
        SDL_fatal("Errore creazione window/renderer");

    texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_YUY2,
                                SDL_TEXTUREACCESS_STREAMING, WIDTH, HEIGHT);
    if (!texture)
        SDL_fatal("Errore creazione texture");
    return 0;
}

int main(int argc, char **argv) {
    setlocale(LC_ALL, "");
    VERBOSE = 1;
    char *dev_name;
    if (argc != 2)
        msg_exit("Usage: test [DEVICE NODE]\n");

    if (-1 == SDL_setup())
        return -1;
    printf("HEY");

    dev_name = argv[1];
    open_device(dev_name);
    setup_device();
    setup_mmap();

    start_streaming();
    main_loop();

    clean_up();
}
