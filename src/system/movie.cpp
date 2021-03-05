extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h> 
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
}

#include "movie.h"
#include "fsmgr.h"

#if defined(__APPLE__) && defined(__MACH__)
#include <OpenGL/gl.h>
#else
#include <GL/gl.h>
#endif
#include "system.h"
#include "sound.h"

namespace System
{

TMovie TMovie::Instance;
    
struct TMovie::Context
{
    AVFormatContext *FormatCtx = NULL;
    AVCodecContext  *VidCodecCtx = NULL;
    AVCodec         *VidCodec = NULL;
    AVCodecContext  *AudCodecCtx = NULL;
    AVCodec         *AudCodec = NULL;
    
    int videoStream = -1;
    int audioStream = -1;
    
    std::list<AVFrame *> vidFrames;
    std::list<AVFrame *> audFrames;
    
    AVPacket *packet = NULL;
    AVFrame *vidFrame = NULL;
    AVFrame *audFrame = NULL;
    
    bool playing = false;
    
    struct SwsContext *sws_ctx = NULL;
    GLuint screenTex = 0;
    
    uint8_t *dst_data[4];
    int dst_linesize[4];    
    
    bool AudioSetted = false;
    struct SwrContext *swr_ctx = NULL;
    uint8_t **adst_data = NULL;
    int adst_linesize = 0;
    int adst_nb_samples = 0;
};

TMovie::TMovie()
{
    _ctx = new Context();
}    

TMovie::~TMovie()
{
    delete _ctx;
}

void TMovie::Init()
{
    //av_register_all();
}

int TMovie::EventsWatcher(void *, SDL_Event *event)
{
    switch(event->type)
    {
        case SDL_WINDOWEVENT:
        {
            switch(event->window.event)
            {
            case SDL_WINDOWEVENT_RESIZED:
            case SDL_WINDOWEVENT_SIZE_CHANGED:
                {
                    Common::Point scrSz = System::GetResolution();
                    glViewport(0, 0, scrSz.x, scrSz.y);
                }
                break;
            default:
                break;
            }

        }
        break;
        
        case SDL_KEYDOWN:
            Instance._ctx->playing = false;
            return 0;

        case SDL_KEYUP:
        case SDL_TEXTINPUT:
        case SDL_MOUSEBUTTONDOWN:
        case SDL_MOUSEBUTTONUP:
        case SDL_MOUSEMOTION:
            return 0;
    }
    
    return 1; // This event can be passed to another event watcher
}


bool TMovie::OpenFile(const std::string &fname)
{
    FSMgr::iNode *file = FSMgr::iDir::findNode(fname);
    if (!file)
        return false;
    
    // Trying to open video file
    if (avformat_open_input(&_ctx->FormatCtx, file->getPath().c_str(), NULL, NULL) != 0)
        return false;
    
    if (avformat_find_stream_info(_ctx->FormatCtx, NULL) < 0)
      return false;
    
    _ctx->videoStream = av_find_best_stream(_ctx->FormatCtx, AVMEDIA_TYPE_VIDEO, -1, -1, &_ctx->VidCodec, 0);
    
    if (_ctx->videoStream == -1)
        return false;
    
    _ctx->VidCodecCtx = avcodec_alloc_context3(_ctx->VidCodec);
    avcodec_parameters_to_context(_ctx->VidCodecCtx, _ctx->FormatCtx->streams[_ctx->videoStream]->codecpar);
    
    if (avcodec_open2(_ctx->VidCodecCtx, _ctx->VidCodec, NULL) < 0)
    {
        avcodec_free_context(&_ctx->VidCodecCtx);
        avformat_close_input(&_ctx->FormatCtx);
        return false;
    }
    
    _ctx->packet = av_packet_alloc();
    _ctx->vidFrame = av_frame_alloc();

    _ctx->audioStream = av_find_best_stream(_ctx->FormatCtx, AVMEDIA_TYPE_AUDIO, -1, _ctx->videoStream, &_ctx->AudCodec, 0);
    _ctx->AudioSetted = false;
    
    _ctx->swr_ctx = NULL;
    _ctx->adst_data = NULL;
    _ctx->adst_linesize = 0;
    _ctx->adst_nb_samples = 0;
    
    if (_ctx->audioStream >= 0)
    {
        _ctx->AudCodecCtx = avcodec_alloc_context3(_ctx->AudCodec);
        avcodec_parameters_to_context(_ctx->AudCodecCtx, _ctx->FormatCtx->streams[_ctx->audioStream]->codecpar);
        
        int ret = avcodec_open2(_ctx->AudCodecCtx, _ctx->AudCodec, NULL);
        if (ret < 0)
        {
            avcodec_free_context(&_ctx->AudCodecCtx);
            _ctx->audioStream = -1;
        }
        
        _ctx->audFrame = av_frame_alloc();
    }
    
    return true;
}

void TMovie::Close()
{
    _ctx->playing = false;
    
    av_packet_free(&_ctx->packet);
    av_frame_free(&_ctx->vidFrame);
    
    if (_ctx->audFrame)
        av_frame_free(&_ctx->audFrame);
    
    if (_ctx->sws_ctx)
    {
        sws_freeContext(_ctx->sws_ctx);
        _ctx->sws_ctx = NULL;
        
        av_freep(&_ctx->dst_data[0]);
    }
    
    while(!_ctx->audFrames.empty())
    {
        av_frame_free(&_ctx->audFrames.front());
        _ctx->audFrames.pop_front();
    }
    
    while(!_ctx->vidFrames.empty())
    {
        av_frame_free(&_ctx->vidFrames.front());
        _ctx->vidFrames.pop_front();
    }
    
    avcodec_free_context(&_ctx->AudCodecCtx);
    avcodec_free_context(&_ctx->VidCodecCtx);
    avformat_close_input(&_ctx->FormatCtx);
    
    _ctx->audioStream = -1;
    _ctx->videoStream = -1;
    
    if (_ctx->screenTex != 0)
        glDeleteTextures(1, &_ctx->screenTex);
    
    if (_ctx->swr_ctx)
        swr_free(&_ctx->swr_ctx);
    
    if (_ctx->adst_data)
        av_freep(&_ctx->adst_data[0]);
    av_freep(_ctx->adst_data);
    
    _ctx->screenTex = 0;
}

void TMovie::ReadFrames()
{
    if ( (_ctx->vidFrames.empty() || _ctx->audFrames.empty()) ) 
    {
        int ret = av_read_frame(_ctx->FormatCtx, _ctx->packet);
        if ( ret >= 0 )
        {
            if (_ctx->packet->stream_index == _ctx->videoStream) 
            {
                ret = avcodec_send_packet(_ctx->VidCodecCtx, _ctx->packet);
                if (ret < 0)
                {
                    _ctx->playing = false;
                }
                else
                {
                    ret = 0;

                    while (ret >= 0 ) 
                    {
                        ret = avcodec_receive_frame(_ctx->VidCodecCtx, _ctx->vidFrame);

                        if (ret < 0)
                            break;

                        AVFrame *tmp = av_frame_alloc();

                        av_frame_move_ref(tmp, _ctx->vidFrame);
                        av_frame_unref(_ctx->vidFrame);

                        _ctx->vidFrames.push_back(tmp);
                    }                    
                }
            }
            else if (_ctx->audioStream >= 0 && _ctx->packet->stream_index == _ctx->audioStream) 
            {
                ret = avcodec_send_packet(_ctx->AudCodecCtx, _ctx->packet);
                if (ret < 0)
                {
                    _ctx->playing = false;
                }
                else
                {
                    ret = 0;

                    while (ret >= 0 ) 
                    {
                        ret = avcodec_receive_frame(_ctx->AudCodecCtx, _ctx->audFrame);

                        if (ret < 0)
                            break;

                        AVFrame *tmp = av_frame_alloc();

                        av_frame_move_ref(tmp, _ctx->audFrame);
                        av_frame_unref(_ctx->audFrame);

                        _ctx->audFrames.push_back(tmp);
                    }                    
                }
            }
            else
            {
                av_packet_unref(_ctx->packet);
            }
        }
        else
            _ctx->playing = false;
    }
}

uint32_t TMovie::ProcessFrame()
{
    if (_ctx->vidFrames.empty())
        return 0;
    
    AVFrame *frm = _ctx->vidFrames.front();
    _ctx->vidFrames.pop_front();

    AVStream * vids = _ctx->FormatCtx->streams[_ctx->videoStream];
    uint32_t nextPV = 1000 * (frm->pts + frm->pkt_duration) * vids->time_base.num / vids->time_base.den;

    if (!_ctx->sws_ctx)
    {
        _ctx->sws_ctx = sws_getContext(frm->width,
                        frm->height,
                        _ctx->VidCodecCtx->pix_fmt,
                        frm->width,
                        frm->height,
                        AV_PIX_FMT_RGB24,
                        SWS_BILINEAR,
                        NULL,
                        NULL,
                        NULL
                        );

        av_image_alloc(_ctx->dst_data, _ctx->dst_linesize,
                  frm->width, frm->height, AV_PIX_FMT_RGB24, 1);


    }

    sws_scale(_ctx->sws_ctx, (uint8_t const * const *)frm->data,
            frm->linesize, 0, frm->height,
            _ctx->dst_data, _ctx->dst_linesize);


    Common::Point scrSz = System::GetResolution();
    glViewport(0, 0, scrSz.x, scrSz.y);

    glPushAttrib(GL_LIGHTING | GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT | GL_CURRENT_BIT | GL_TRANSFORM_BIT | GL_TEXTURE_BIT | GL_TEXTURE_2D);

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    glDisable(GL_LIGHTING);

    glDepthMask(GL_FALSE);
    glDisable(GL_DEPTH_TEST);

    glDisable(GL_BLEND);
    
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f );
    glClear(GL_COLOR_BUFFER_BIT);

    glEnable(GL_TEXTURE_2D);
    
    if (_ctx->screenTex == 0)
    {
        glGenTextures(1, &_ctx->screenTex);
        
        glBindTexture(GL_TEXTURE_2D, _ctx->screenTex);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, frm->width, frm->height, 0, GL_RGB, GL_UNSIGNED_BYTE, NULL);
    }
    else
        glBindTexture(GL_TEXTURE_2D, _ctx->screenTex);
    
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, frm->width, frm->height, GL_RGB, GL_UNSIGNED_BYTE, _ctx->dst_data[0]);
    
    double scrAspect = (double)scrSz.x / (double)scrSz.y;
    double frmAspect = (double)frm->width / (double)frm->height;
    double qW = 1.0;
    double qH = 1.0;
    if (scrAspect > frmAspect)
        qW = frmAspect / scrAspect;
    else if (scrAspect < frmAspect)
        qH = scrAspect / frmAspect;

    glBegin(GL_QUADS);
    {
        glTexCoord2f(0, 0);
        glVertex3f(-1.0 * qW, 1.0 * qH, 0);
        glTexCoord2f(0, 1);
        glVertex3f(-1.0 * qW, -1.0 * qH, 0);
        glTexCoord2f(1, 1);
        glVertex3f(1.0 * qW, -1.0 * qH, 0);
        glTexCoord2f(1, 0);
        glVertex3f(1.0 * qW, 1.0 * qH, 0);
    }
    glEnd();

    glPopAttrib();

    System::Flip();

    av_frame_free(&frm);
    
    return nextPV;
}

uint32_t TMovie::ProcessAudio()
{
    AVFrame *frm = _ctx->audFrames.front();
    _ctx->audFrames.pop_front();

    if (!_ctx->AudioSetted)
    {
        switch(_ctx->AudCodecCtx->sample_fmt)
        {
            case AV_SAMPLE_FMT_U8P:
                _ctx->swr_ctx = swr_alloc_set_opts(NULL, (frm->channels > 1 ? AV_CH_LAYOUT_STEREO : AV_CH_LAYOUT_MONO), AV_SAMPLE_FMT_U8, _ctx->AudCodecCtx->sample_rate, 
                                                         _ctx->AudCodecCtx->channel_layout, _ctx->AudCodecCtx->sample_fmt, _ctx->AudCodecCtx->sample_rate, 
                                                   0, NULL);
                swr_init(_ctx->swr_ctx);
                av_samples_alloc_array_and_samples(&_ctx->adst_data, &_ctx->adst_linesize, Common::MIN(2, frm->channels), frm->nb_samples, AV_SAMPLE_FMT_U8, 0);
                _ctx->adst_nb_samples = frm->nb_samples;
            case AV_SAMPLE_FMT_U8:
                if (_ctx->AudCodecCtx->channels > 1)
                    SFXEngine::SFXe.AudioStream->SetFormat(AL_FORMAT_STEREO8, _ctx->AudCodecCtx->sample_rate);
                else
                    SFXEngine::SFXe.AudioStream->SetFormat(AL_FORMAT_MONO8, _ctx->AudCodecCtx->sample_rate);
                break;
            
            
            case AV_SAMPLE_FMT_S16P:
            default:
                _ctx->swr_ctx = swr_alloc_set_opts(NULL, (frm->channels > 1 ? AV_CH_LAYOUT_STEREO : AV_CH_LAYOUT_MONO), AV_SAMPLE_FMT_S16, _ctx->AudCodecCtx->sample_rate, 
                                                         _ctx->AudCodecCtx->channel_layout, _ctx->AudCodecCtx->sample_fmt, _ctx->AudCodecCtx->sample_rate, 
                                                   0, NULL);
                swr_init(_ctx->swr_ctx);
                av_samples_alloc_array_and_samples(&_ctx->adst_data, &_ctx->adst_linesize, Common::MIN(2, frm->channels), frm->nb_samples, AV_SAMPLE_FMT_S16, 0);
                _ctx->adst_nb_samples = frm->nb_samples;
            case AV_SAMPLE_FMT_S16:
                if (_ctx->AudCodecCtx->channels > 1)
                    SFXEngine::SFXe.AudioStream->SetFormat(AL_FORMAT_STEREO16, _ctx->AudCodecCtx->sample_rate);
                else
                    SFXEngine::SFXe.AudioStream->SetFormat(AL_FORMAT_MONO16, _ctx->AudCodecCtx->sample_rate);
                break;
        }
        
        _ctx->AudioSetted = true;
    }

    AVStream * s = _ctx->FormatCtx->streams[_ctx->audioStream];
    uint32_t nextPA = 1000 * (frm->pts + frm->pkt_duration / 2) * s->time_base.num / s->time_base.den;

    switch(_ctx->AudCodecCtx->sample_fmt)
    {
        case AV_SAMPLE_FMT_U8:
            SFXEngine::SFXe.AudioStream->Feed(frm->data[0], frm->nb_samples * frm->channels);
            break;
            
        case AV_SAMPLE_FMT_S16:
            SFXEngine::SFXe.AudioStream->Feed(frm->data[0], frm->nb_samples * frm->channels * 2);
            break;
        
        
        case AV_SAMPLE_FMT_U8P:
            if (frm->nb_samples > _ctx->adst_nb_samples)
            {
                av_freep(&_ctx->adst_data[0]);
                av_samples_alloc(_ctx->adst_data, &_ctx->adst_linesize, Common::MIN(2, frm->channels), frm->nb_samples, AV_SAMPLE_FMT_U8, 0);
                _ctx->adst_nb_samples = frm->nb_samples;
            }
            swr_convert(_ctx->swr_ctx, _ctx->adst_data, frm->nb_samples, (const uint8_t **)frm->data, frm->nb_samples);
            
            SFXEngine::SFXe.AudioStream->Feed(_ctx->adst_data[0], frm->nb_samples * Common::MIN(2, frm->channels));
            break;

        case AV_SAMPLE_FMT_S16P:
        default:
            if (frm->nb_samples > _ctx->adst_nb_samples)
            {
                av_freep(&_ctx->adst_data[0]);
                av_samples_alloc(_ctx->adst_data, &_ctx->adst_linesize, Common::MIN(2, frm->channels), frm->nb_samples, AV_SAMPLE_FMT_S16, 0);
                _ctx->adst_nb_samples = frm->nb_samples;
            }
            swr_convert(_ctx->swr_ctx, _ctx->adst_data, frm->nb_samples, (const uint8_t **)frm->data, frm->nb_samples);
            
            SFXEngine::SFXe.AudioStream->Feed(_ctx->adst_data[0], frm->nb_samples * 2 * Common::MIN(2, frm->channels));
            break;
    }

    av_frame_free(&frm);
    
    return nextPA;
}

void TMovie::PlayMovie(const std::string &fname)
{
    if (!OpenFile(fname))
        return;    

    uint32_t stime = SDL_GetTicks();
    uint32_t nextSync = 0;
    uint32_t nextPA = 0;
    uint32_t nextPV = 0;
    
    _ctx->playing = true;
    
    System::EventsAddHandler(TMovie::EventsWatcher);
    
    for(size_t i = 0; i < 10; i++)
        ReadFrames();
    
    while( _ctx->playing )
    {
        uint32_t curPts = SDL_GetTicks() - stime;
        
        ReadFrames();
        
        if (!_ctx->vidFrames.empty() && nextPV <= curPts )
            nextPV = ProcessFrame();
          
        if (_ctx->audioStream >= 0)
        {
            size_t minLeft = SFXEngine::SFXe.AudioStream->BuffersCapacity() * 10;

            //if (!audFrames.empty() && (SFXEngine::SFXe.AudioStream->DataLeft() < minLeft || nextPA <= curPts) )
            if (!_ctx->audFrames.empty() && (SFXEngine::SFXe.AudioStream->DataLeft() < minLeft || nextPA <= curPts) )
            {
                nextPA = ProcessAudio();
            }
            
            if (curPts / 1000 >= nextSync)
            {
                nextSync = (curPts / 1000) + 1;
                stime = ((SDL_GetTicks() - SFXEngine::SFXe.AudioStream->GetPlayTime()) + stime) / 2;
            }
        }
        
        System::ProcessEvents();
        SDL_Delay(1);
    }
    
    System::EventsDeleteHandler(TMovie::EventsWatcher);
    SFXEngine::SFXe.AudioStream->stop();
    
    Close();
}


}