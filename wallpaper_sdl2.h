#pragma once
extern "C" {
#include <libavcodec\avcodec.h>
#include <libavformat\avformat.h>
#include <libswscale\swscale.h>
#include <libavutil\imgutils.h>
}
#define SDL_MAIN_HANDLED
#include <SDL2/SDL.h>
#include <SDL2/SDL_thread.h>
#include <SDL2/SDL_syswm.h>
#include <memory>
#include <Windows.h>

BOOL CALLBACK enum_func_cb(_In_ HWND hwnd, _In_ LPARAM lparam) {
    HWND hDefView = FindWindowExW(hwnd, 0, L"SHELLDLL_DefView", 0);
    if (hDefView) {
        HWND hWorkerW = FindWindowExW(0, hwnd, L"WorkerW", 0);
        ShowWindow(hWorkerW, SW_HIDE);
        return FALSE;
    }
    return TRUE;
}

#pragma warning(push)
#pragma warning(disable: 26812)
bool wallpaper_set_video(const char* url) {

    /* ���� DPI ��֪ */
    SetProcessDPIAware();

    /* ��ʼ�� SDL �� */
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER)) return false;

    /* ���ļ�����ȡ�ļ�ͷ */
    AVFormatContext* pFmtCtx = NULL;
    if (avformat_open_input(&pFmtCtx, url, NULL, NULL) != 0) return false;
    std::shared_ptr<AVFormatContext*> fmtCtxCloser(&pFmtCtx, avformat_close_input);

    /* ��ȡ����Ϣ */
    if (avformat_find_stream_info(pFmtCtx, NULL) < 0) return false;

    /* ���ҵ�һ����Ƶ�� */
    int videoStream = -1;
    for (unsigned int i = 0; i < pFmtCtx->nb_streams; ++i)
        if (pFmtCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            videoStream = i;
            break;
        }
    if (videoStream == -1) return false;

    /* ��ȡ��Ƶ����Ӧ�Ľ����� */
    AVCodecParameters* pCodecParams = pFmtCtx->streams[videoStream]->codecpar;
    AVCodec* pCodec = avcodec_find_decoder(pCodecParams->codec_id);
    if (pCodec == NULL) return false;

    /* ��ʼ�������������� */
    AVCodecContext* pCodecCtx = avcodec_alloc_context3(NULL);
    if (avcodec_parameters_to_context(pCodecCtx, pCodecParams) < 0) return false;
    std::shared_ptr<AVCodecContext> codecCtxCloser(pCodecCtx, avcodec_close);

    /* �򿪽����� */
    if (avcodec_open2(pCodecCtx, pCodec, NULL) < 0) return false;

    /* ����֡ */
    AVFrame* pFrame = av_frame_alloc();
    std::shared_ptr<AVFrame*> frameDeleter(&pFrame, av_frame_free);

    /* ����ת��ɫ����֡ */
    AVFrame* pFrameYUV = av_frame_alloc();
    std::shared_ptr<AVFrame*> frameBGRDeleter(&pFrameYUV, av_frame_free);

    /* Ŀ��������ɫɫ�� */
    const AVPixelFormat destPixFormat = AV_PIX_FMT_BGR24;  // AV_PIX_FMT_YUV420P

    /* �������ݻ����� */
    int numBytes = av_image_get_buffer_size(destPixFormat, pCodecCtx->width,
        pCodecCtx->height, 1);
    uint8_t* buffer = (uint8_t*)av_malloc(numBytes * sizeof(uint8_t));
    std::shared_ptr<uint8_t> bufferDeleter(buffer, av_free);

    /* ���֡��Ϣ */
    av_image_fill_arrays(pFrameYUV->data, pFrameYUV->linesize, buffer, destPixFormat,
        pCodecCtx->width, pCodecCtx->height, 1);

    /* ��ʼ��ͼ���������� */
    SwsContext* pSwsCtx = sws_getContext(pCodecCtx->width, pCodecCtx->height,
        pCodecCtx->pix_fmt, pCodecCtx->width, pCodecCtx->height, destPixFormat,
        SWS_BICUBIC, NULL, NULL, NULL);
    std::shared_ptr<SwsContext> swsCtxDeleter(pSwsCtx, sws_freeContext);
    
    /* ���� SDL ���� */
    SDL_Window* screen = SDL_CreateWindow("SDL_app", 0, 0,
        pCodecCtx->width, pCodecCtx->height,
        SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_BORDERLESS | SDL_WINDOW_OPENGL);
    if (!screen) return false;
    std::shared_ptr<SDL_Window> screenDestoryer(screen, SDL_DestroyWindow);

    /* ��ȡ������ SDL ������ */
    SDL_SysWMinfo wmInfo{};
    SDL_VERSION(&wmInfo.version);
    SDL_GetWindowWMInfo(screen, &wmInfo);
    HWND hwnd = wmInfo.info.win.window;

    /* �� Program Manager ��������Ϊ SDL ����ĸ����� */
    HWND hProgman = FindWindowW(L"Progman", 0);
    SendMessageTimeout(hProgman, 0x52C, 0, 0, 0, 100, 0);
    SetParent(hwnd, hProgman);
    EnumWindows(enum_func_cb, 0);

    /* ���� SDL ��Ⱦ�� */
    SDL_Renderer* renderer = SDL_CreateRenderer(screen, -1, SDL_RENDERER_TARGETTEXTURE);
    std::shared_ptr<SDL_Renderer> rendererDestroyer(renderer, SDL_DestroyRenderer);

    /* ���� SDL ���� */
    Uint32 sdlPixFormat = SDL_PIXELFORMAT_BGR24;  // SDL_PIXELFORMAT_YV12
    SDL_Texture* texture = SDL_CreateTexture(renderer, sdlPixFormat,
        SDL_TEXTUREACCESS_STREAMING, pCodecCtx->width, pCodecCtx->height);
    std::shared_ptr<SDL_Texture> textureDestroyer(texture, SDL_DestroyTexture);

    /* ������Ƶ */
    AVPacket packet;
    int frameCount = 0;
    const int saveFrameIndex = 1;
    SDL_Event event;
    while (1) {
        int ret = av_read_frame(pFmtCtx, &packet);

        /* ѭ������ */
        if (ret < 0) {
            if (ret == AVERROR_EOF) {
                av_seek_frame(pFmtCtx, -1, pFmtCtx->start_time, AVSEEK_FLAG_BACKWARD);
                continue;
            }
        }
        std::shared_ptr<AVPacket> packetDeleter(&packet, av_packet_unref);

        /*  �������������Ƶ�� */
        if (packet.stream_index == videoStream) {

            /* �� pCodecCtx ���� packet */
            avcodec_send_packet(pCodecCtx, &packet);

            /* ��ȡ pCodecCtx ���������� */
            if (avcodec_receive_frame(pCodecCtx, pFrame) == 0) {

                /* ͼ��ת�� */
                sws_scale(pSwsCtx, pFrame->data,
                    pFrame->linesize, 0, pCodecCtx->height,
                    pFrameYUV->data, pFrameYUV->linesize);

                /* ��Ⱦͼ�� */
                SDL_Rect rc = { 0, 0, pCodecCtx->width, pCodecCtx->height };
                SDL_UpdateTexture(texture, &rc, pFrameYUV->data[0], pFrameYUV->linesize[0]);
                SDL_RenderClear(renderer);
                SDL_RenderCopy(renderer, texture, &rc, &rc);
                SDL_RenderPresent(renderer);
                SDL_Delay(1000 / 60);
            }
        }

        /* ���� SDL ������Ϣ */
        SDL_PollEvent(&event);
        switch (event.type)
        {
        case SDL_QUIT:
            SDL_Quit();
            return 0;
        }
    }
    SDL_Quit();
    return 0;
}
#pragma warning(pop)
