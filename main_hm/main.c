//
//  main.c
//  libavHEVC
//
//  Created by Mickaël Raulet on 11/10/12.
//
//
#include "openHevcWrapper.h"
#include "getopt.h"
#include <libavformat/avformat.h>


//#define TIME2

#ifdef TIME2
#ifdef WIN32
#include <Windows.h>
#else
#include <sys/time.h>
//#include <ctime>
#endif


/* Returns the amount of milliseconds elapsed since the UNIX epoch. Works on both
 * windows and linux. */

static unsigned long int GetTimeMs64()
{
#ifdef WIN32
    /* Windows */
    FILETIME ft;
    LARGE_INTEGER li;
    
    /* Get the amount of 100 nano seconds intervals elapsed since January 1, 1601 (UTC) and copy it
     * to a LARGE_INTEGER structure. */
    GetSystemTimeAsFileTime(&ft);
    li.LowPart = ft.dwLowDateTime;
    li.HighPart = ft.dwHighDateTime;
    
    uint64_t ret = li.QuadPart;
    ret -= 116444736000000000LL; /* Convert from file time to UNIX epoch time. */
    ret /= 10000; /* From 100 nano seconds (10^-7) to 1 millisecond (10^-3) intervals */
    
    return ret;
#else
    /* Linux */
    struct timeval tv;
    
    gettimeofday(&tv, NULL);
    
    unsigned long int ret = tv.tv_usec;
    /* Convert from micro seconds (10^-6) to milliseconds (10^-3) */
    //ret /= 1000;
    
    /* Adds the seconds (10^0) after converting them to milliseconds (10^-3) */
    ret += (tv.tv_sec * 1000000);
    
    return ret;
#endif
}
#endif

typedef struct OpenHevcWrapperContext {
    AVCodec *codec;
    AVCodecContext *c;
    AVFrame *picture;
    AVPacket avpkt;
    AVCodecParserContext *parser;
} OpenHevcWrapperContext;


typedef struct Info {
    int NbFrame;
    int Poc;
    int Tid;
    int Qid;
    int type;
    int size;
} Info;

static void video_decode_example(const char *filename, const char *enh_filename)
{
    AVFormatContext *pFormatCtx[2];
    AVPacket        packet[2];
    FILE *fout  = NULL;
    int width   = -1;
    int height  = -1;
    int nbFrame = 0;
    int stop    = 0;
    int stop_dec= 0;
    int i= 0;
    int got_picture;
    float time  = 0.0;
#ifdef TIME2
    long unsigned int time_us = 0;
#endif
    int video_stream_idx;
    char output_file2[256];

    OpenHevc_Frame     openHevcFrame;
    OpenHevc_Frame_cpy openHevcFrameCpy;
    OpenHevc_Handle    openHevcHandle;

    if (filename == NULL) {
        printf("No input file specified.\nSpecify it with: -i <filename>\n");
        exit(1);
    }

    if (h264_flags)
        openHevcHandle = libOpenH264Init(nb_pthreads, thread_type/*, pFormatCtx*/);
    else if (shvc_flags){

    	printf("file name : %s\n", enhance_file);
    	openHevcHandle = libOpenShvcInit(nb_pthreads, thread_type/*, pFormatCtx*/);
    }
    else
        openHevcHandle = libOpenHevcInit(nb_pthreads, thread_type/*, pFormatCtx*/);


    libOpenHevcSetCheckMD5(openHevcHandle, check_md5_flags);

    if (!openHevcHandle) {
        fprintf(stderr, "could not open OpenHevc\n");
        exit(1);
    }
    av_register_all();
    pFormatCtx[0] = avformat_alloc_context();
    pFormatCtx[1] = avformat_alloc_context();

    if(avformat_open_input(&pFormatCtx[0], filename, NULL, NULL)!=0) {
        printf("%s",filename);
        exit(1); // Couldn't open file
    }
    if(shvc_flags && avformat_open_input(&pFormatCtx[1], enh_filename, NULL, NULL)!=0) {
            printf("%s",enh_filename);
            exit(1); // Couldn't open file
    }
    for(i=0; i<2 ; i++){
		if ( (video_stream_idx = av_find_best_stream(pFormatCtx[i], AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0)) < 0) {
			fprintf(stderr, "Could not find video stream in input file\n");
			exit(1);
		}

		av_dump_format(pFormatCtx[i], 0, filename, 0);

		const size_t extra_size_alloc = pFormatCtx[i]->streams[video_stream_idx]->codec->extradata_size > 0 ?
		(pFormatCtx[i]->streams[video_stream_idx]->codec->extradata_size + FF_INPUT_BUFFER_PADDING_SIZE) : 0;
		if (extra_size_alloc)
		{
			libOpenHevcCopyExtraData(openHevcHandle, pFormatCtx[i]->streams[video_stream_idx]->codec->extradata, extra_size_alloc);
		}
		if(!shvc_flags){
			break;
		}

    }
    libOpenHevcSetDebugMode(openHevcHandle, 0);
    libOpenHevcStartDecoder(openHevcHandle);
    openHevcFrameCpy.pvY = NULL;
    openHevcFrameCpy.pvU = NULL;
    openHevcFrameCpy.pvV = NULL;
#if USE_SDL
    Init_Time();
    if (frame_rate > 0) {
        initFramerate_SDL();
        setFramerate_SDL(frame_rate);
    }
#endif
#ifdef TIME2
    time_us = GetTimeMs64();
#endif
   
    libOpenHevcSetTemporalLayer_id(openHevcHandle, temporal_layer_id);
    libOpenHevcSetActiveDecoders(openHevcHandle, quality_layer_id);
    libOpenHevcSetViewLayers(openHevcHandle, quality_layer_id);

    while(!stop) {
    	if(shvc_flags)
    		if (stop_dec == 0 && av_read_frame(pFormatCtx[1], &packet[1])<0) stop_dec = 1;
        if (stop_dec == 0 && av_read_frame(pFormatCtx[0], &packet[0])<0) stop_dec = 1;
        //printf("Stop dec : %d", stop_dec);
        if (packet[0].stream_index == video_stream_idx && packet[1].stream_index == video_stream_idx || stop_dec == 1) {
        	if(shvc_flags){
        		got_picture = libOpenShvcDecode(openHevcHandle, packet, stop_dec);

        	}
        	else
        		got_picture = libOpenHevcDecode(openHevcHandle, packet[0].data, !stop_dec ? packet[0].size : 0, packet[0].pts);
        	if (got_picture > 0) {
                fflush(stdout);
                libOpenHevcGetPictureInfo(openHevcHandle, &openHevcFrame.frameInfo);
                if ((width != openHevcFrame.frameInfo.nWidth) || (height != openHevcFrame.frameInfo.nHeight)) {
                    width  = openHevcFrame.frameInfo.nWidth;
                    height = openHevcFrame.frameInfo.nHeight;
                    if (fout)
                       fclose(fout);
                    if (output_file) {
                        sprintf(output_file2, "%s_%dx%d.yuv", output_file, width, height);
                        fout = fopen(output_file2, "wb");
                    }
#if USE_SDL
                    if (display_flags == ENABLE) {
                        Init_SDL((openHevcFrame.frameInfo.nYPitch - openHevcFrame.frameInfo.nWidth)/2, openHevcFrame.frameInfo.nWidth, openHevcFrame.frameInfo.nHeight);
                    }
#endif
                    if (fout) {
                        int format = libOpenHevcGetChromaformat();
                        libOpenHevcGetPictureInfo(openHevcHandle, &openHevcFrameCpy.frameInfo);
                        if(openHevcFrameCpy.pvY) {
                            free(openHevcFrameCpy.pvY);
                            free(openHevcFrameCpy.pvU);
                            free(openHevcFrameCpy.pvV);
                        }
                        openHevcFrameCpy.pvY = calloc (openHevcFrameCpy.frameInfo.nYPitch * openHevcFrameCpy.frameInfo.nHeight, sizeof(unsigned char));
                        openHevcFrameCpy.pvU = calloc (openHevcFrameCpy.frameInfo.nUPitch * openHevcFrameCpy.frameInfo.nHeight >> format, sizeof(unsigned char));
                        openHevcFrameCpy.pvV = calloc (openHevcFrameCpy.frameInfo.nVPitch * openHevcFrameCpy.frameInfo.nHeight >> format, sizeof(unsigned char));
                    }
                }
#if USE_SDL
                if (frame_rate > 0) {
                    framerateDelay_SDL();
                }                
                if (display_flags == ENABLE) {
                    libOpenHevcGetOutput(openHevcHandle, 1, &openHevcFrame);
                    libOpenHevcGetPictureInfo(openHevcHandle, &openHevcFrame.frameInfo);
                    SDL_Display((openHevcFrame.frameInfo.nYPitch - openHevcFrame.frameInfo.nWidth)/2, openHevcFrame.frameInfo.nWidth, openHevcFrame.frameInfo.nHeight,
                            openHevcFrame.pvY, openHevcFrame.pvU, openHevcFrame.pvV);
                }
#endif
                if (fout) {
                    int format = libOpenHevcGetChromaformat();
                    libOpenHevcGetOutputCpy(openHevcHandle, 1, &openHevcFrameCpy);
                    fwrite( openHevcFrameCpy.pvY , sizeof(uint8_t) , openHevcFrameCpy.frameInfo.nYPitch * openHevcFrameCpy.frameInfo.nHeight, fout);
                    fwrite( openHevcFrameCpy.pvU , sizeof(uint8_t) , openHevcFrameCpy.frameInfo.nUPitch * openHevcFrameCpy.frameInfo.nHeight >> format, fout);
                    fwrite( openHevcFrameCpy.pvV , sizeof(uint8_t) , openHevcFrameCpy.frameInfo.nVPitch * openHevcFrameCpy.frameInfo.nHeight >> format, fout);
                }
                nbFrame++;
                if (nbFrame == num_frames)
                    stop = 1;
            } else {
                if (stop_dec==1 && nbFrame){
                stop = 1;}
            }
        }
       av_free_packet(&packet[0]);
        if(shvc_flags){
        	av_free_packet(&packet[1]);
        }

    }
#if USE_SDL
    time = SDL_GetTime()/1000.0;
#ifdef TIME2
    time_us = GetTimeMs64() - time_us;
#endif
    CloseSDLDisplay();
#endif
    if (fout) {printf("Close fout\n");
        fclose(fout);
        if(openHevcFrameCpy.pvY) {
            free(openHevcFrameCpy.pvY);
            free(openHevcFrameCpy.pvU);
            free(openHevcFrameCpy.pvV);
        }
    }printf("Close context AVC\n");
    avformat_close_input(&pFormatCtx[0]);

    if(shvc_flags){
    	printf("Close context HEVC\n");
    	avformat_close_input(&pFormatCtx[1]);
    }
    libOpenHevcClose(openHevcHandle);
#if USE_SDL
#ifdef TIME2
    printf("frame= %d fps= %.0f time= %ld video_size= %dx%d\n", nbFrame, nbFrame/time, time_us, openHevcFrame.frameInfo.nWidth, openHevcFrame.frameInfo.nHeight);
#else
    printf("frame= %d fps= %.0f time= %.2f video_size= %dx%d\n", nbFrame, nbFrame/time, time, openHevcFrame.frameInfo.nWidth, openHevcFrame.frameInfo.nHeight);
#endif
#endif
}

int main(int argc, char *argv[]) {
    init_main(argc, argv);
    video_decode_example(input_file, enhance_file);
    return 0;
}

