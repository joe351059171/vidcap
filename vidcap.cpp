#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <syslog.h>
#include "turbojpeg.h"
#include <capture.h>
#include <rapp/rapp.h>

#ifdef DEBUG
#define D(x)    x
#else
#define D(x)
#endif

#define LOGINFO(fmt, args...)    { syslog(LOG_INFO, fmt, ## args); printf(fmt, ## args); }
#define LOGERR(fmt, args...)     { syslog(LOG_CRIT, fmt, ## args); fprintf(stderr, fmt, ## args); }

void cropYUV420(char* source, int width, int height, 
                char* dest, int cropX, int cropY, int cropWidth, int cropHeight)
{
  int size = width * height;
  int cx = cropX - cropX % 2;
  int cy = cropY - cropY % 2;
  int cw = cropWidth + cropWidth % 2;
  int ch = cropHeight + cropHeight % 2;
  char* sc = source + cy * width + cx;
  for(int i = 0; i < ch; i++) {
    memcpy(dest+i*cw, sc+i*width, cw);
  }

  int cux = cx / 2;
  int cuy = cy / 2;
  int cuw = cw / 2;
  int cuh = ch / 2;
  char* su = source + width*height + cuy*width/2 + cux;
  char* du = dest + cw*ch;
  for(int i = 0; i < cuh; i++) {
    memcpy(du+i*cuw, su+i*width/2, cuw);
  }
  char* sv = source + width*height*5/4 + cuy*width/2 + cux;
  du = dest + cw*ch*5/4;
  for(int i = 0; i < cuh; i++) {
    memcpy(du+i*cuw, sv+i*width/2, cuw);
  }
}

int
main(int argc, char** argv)
{
  media_stream *     stream;
  media_frame *      frame;
  struct timeval     tv_start, tv_end;
  int                msec;
  int                i          = 1;
  int                numframes  = 100;
  unsigned long long totalbytes = 0;
  FILE *file;
  int cropX = 0, cropY = 0;
  int cropWidth, cropHeight;
  unsigned char *rapp_buffer = NULL;

  if (argc < 3) {
#if defined __i386
    /* The host version */
    fprintf(stderr,
            "Usage: %s media_type media_props\n", argv[0]);
    fprintf(stderr,
            "Example: %s \"%s\"      \"resolution=2CIF&fps=15\"\n",
            argv[0], IMAGE_JPEG);
    fprintf(stderr,
            "Example: %s \"%s\" \"resolution=352x288&sdk_format=Y800&fps=15\"\n",
            argv[0], IMAGE_UNCOMPRESSED);
    fprintf(stderr,
            "Example: %s \"%s\" \"capture-cameraIP=192.168.0.90&capture-userpass=user:pass&sdk_format=Y800&resolution=2CIF&fps=15\"\n",
            argv[0], IMAGE_UNCOMPRESSED);
#else
    fprintf(stderr, "Usage: %s media_type media_props [numframes]\n",
            argv[0]);
    fprintf(stderr, "Example: %s %s \"resolution=352x288&fps=15\" 100\n",
            argv[0], IMAGE_JPEG);
    fprintf(stderr,
            "Example: %s %s \"resolution=160x120&sdk_format=Y800&fps=15\" 100\n",
            argv[0], IMAGE_UNCOMPRESSED);
#endif

    return 1;
  }
  /* is numframes specified ? */
  if (argc >= 4) {
    numframes = atoi(argv[3]);

    /* Need at least 2 frames for the achived fps calculation */
    if (numframes < 2) {
      numframes = 2;
    }
  }

  openlog("vidcap", LOG_PID | LOG_CONS, LOG_USER);

  stream = capture_open_stream(argv[1], argv[2]);
  if (stream == NULL) {
    LOGERR("Failed to open stream\n");
    closelog();
    return EXIT_FAILURE;
  }

  gettimeofday(&tv_start, NULL);

  /* print intital information */
  frame = capture_get_frame(stream);
  sleep(1);
  size_t width = capture_frame_width(frame);
  size_t height = capture_frame_height(frame);
  size_t size = capture_frame_size(frame);
  char* data = capture_frame_data(frame);

  cropWidth = width;
  cropHeight = height;
  if(argc >= 9) {
    cropX = atoi(argv[5]);
    cropY = atoi(argv[6]);
    cropWidth = atoi(argv[7]);
    cropHeight = atoi(argv[8]);
  }
  LOGINFO("etting %d frames. resolution: %dx%d framesize: %d\n",
          numframes,
          width,
          height,
          size);

  file = fopen(argv[4], "wb");
  if(file == NULL) {
    printf("file open failed!\n");
    return -1;
  }
  int cropSize = cropWidth*cropHeight*3/2;
  char* dest = malloc(cropSize);
  rapp_initialize();
  rapp_buffer = rapp_malloc(cropWidth * cropHeight, 0);

  //cropYUV420(rapp_buffer, width, height, dest, cropX, cropY, cropWidth, cropHeight);
  tjhandle tjh = tjInitDecompress();
  if(tjh == NULL) {
    printf("tjInitCompress error '%s'\n", tjGetErrorStr());
    return -1;
  }

  unsigned char* jpegBuf = NULL;
  unsigned long jpegSize = 0;
  printf("Decompress begin!\n");
  if(tjDecompress2(tjh, data, size, dest, cropWidth, 0, cropHeight, TJPF_GRAY, 0)) {
    printf("tjCompressFromYUV error '%s' \n", tjGetErrorStr());
    return -1;
  }
  printf("Decompress successed! %d\n", rapp_alignment);
  int ret = rapp_thresh_gt_u8(rapp_buffer, cropHeight, dest, height, 640, 480, 100);
  if(ret < 0) {
    printf("thresh error %s\n", rapp_error(ret));
  }
  fwrite(dest, 1, cropHeight*cropWidth, file);
  fclose(file);
  free(dest);
  rapp_free(rapp_buffer);
  tjFree(jpegBuf);
  capture_frame_free(frame);

  while (i < numframes) {
    frame = capture_get_frame(stream);
    if (!frame) {
      /* nothing to read, this is serious  */
      fprintf(stderr, "Failed to read frame!");
      syslog(LOG_CRIT, "Failed to read frame!\n");

      return 1;
    } else {
      D(capture_time ts = capture_frame_timestamp(frame));
      D(LOGINFO("timestamp: %" CAPTURE_TIME_FORMAT "\n", ts));

      totalbytes += capture_frame_size(frame);
      capture_frame_free(frame);
      i++;
    }
  }
  gettimeofday(&tv_end, NULL);

  /* calculate fps */
  msec  = tv_end.tv_sec * 1000 + tv_end.tv_usec / 1000;
  msec -= tv_start.tv_sec * 1000 + tv_start.tv_usec / 1000;

  LOGINFO("Fetched %d images in %d milliseconds, fps:%0.3f MBytes/sec:%0.3f\n",
          i,
          msec,
          (float)(i / (msec / 1000.0f)),
          (float)(totalbytes / (msec * 1000.0f)));

  capture_close_stream(stream);
  closelog();

  return EXIT_SUCCESS;
}
