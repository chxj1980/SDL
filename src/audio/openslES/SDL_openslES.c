/*
  Simple DirectMedia Layer
  Copyright (C) 1997-2019 Sam Lantinga <slouken@libsdl.org>

  This software is provided 'as-is', without any express or implied
  warranty.  In no event will the authors be held liable for any damages
  arising from the use of this software.

  Permission is granted to anyone to use this software for any purpose,
  including commercial applications, and to alter it and redistribute it
  freely, subject to the following restrictions:

  1. The origin of this software must not be misrepresented; you must not
     claim that you wrote the original software. If you use this software
     in a product, an acknowledgment in the product documentation would be
     appreciated but is not required.
  2. Altered source versions must be plainly marked as such, and must not be
     misrepresented as being the original software.
  3. This notice may not be removed or altered from any source distribution.
*/
#include "../../SDL_internal.h"

#if SDL_AUDIO_DRIVER_OPENSLES

#include "SDL_audio.h"
#include "../SDL_audio_c.h"
#include "SDL_openslES.h"

/* for native audio */
#include <SLES/OpenSLES.h>
#include <SLES/OpenSLES_Android.h>

#include <android/log.h>

#define LOG_TAG "SDL_openslES"

#if 0
#define LOGI(...)  __android_log_print(ANDROID_LOG_INFO,LOG_TAG,__VA_ARGS__)
#define LOGE(...)  __android_log_print(ANDROID_LOG_ERROR,LOG_TAG,__VA_ARGS__)
// #define LOGI(...) do {} while (0)
// #define LOGE(...) do {} while (0)
#else
#define LOGI(...)
#define LOGE(...)
#endif

/* engine interfaces */
static SLObjectItf engineObject = NULL;
static SLEngineItf engineEngine;

/* output mix interfaces */
static SLObjectItf outputMixObject = NULL;
// static SLEnvironmentalReverbItf outputMixEnvironmentalReverb = NULL;

/* aux effect on the output mix, used by the buffer queue player */
/* static const SLEnvironmentalReverbSettings reverbSettings = SL_I3DL2_ENVIRONMENT_PRESET_STONECORRIDOR; */

/* buffer queue player interfaces */
static SLObjectItf                   bqPlayerObject = NULL;
static SLPlayItf                     bqPlayerItf;
static SLAndroidSimpleBufferQueueItf bqPlayerBufferQueue;
/*static SLEffectSendItf          bqPlayerEffectSend; */
static SLMuteSoloItf                 bqPlayerMuteSolo;
static SLVolumeItf                   bqPlayerVolume;

#if 0
/* recorder interfaces TODO */
static SLObjectItf                   recorderObject = NULL;
static SLRecordItf                   recorderRecord;
static SLAndroidSimpleBufferQueueItf recorderBufferQueue;
#endif

/* pointer and size of the next player buffer to enqueue, and number of remaining buffers */
#if 0
static short      *nextBuffer;
static unsigned    nextSize;
static int         nextCount;
#endif

// static SDL_AudioDevice* audioDevice = NULL;

#if 0
static const char *sldevaudiorecorderstr = "SLES Audio Recorder";
static const char *sldevaudioplayerstr   = "SLES Audio Player";

#define  SLES_DEV_AUDIO_RECORDER  sldevaudiorecorderstr
#define  SLES_DEV_AUDIO_PLAYER  sldevaudioplayerstr
static void openslES_DetectDevices( int iscapture )
{
  LOGI( "openSLES_DetectDevices()" );
    if ( iscapture )
            addfn( SLES_DEV_AUDIO_RECORDER );
  else
            addfn( SLES_DEV_AUDIO_PLAYER );
  return;
}
#endif

static void openslES_DestroyEngine();

static int
openslES_CreateEngine()
{
    SLresult result;

    LOGI("openSLES_CreateEngine()");

    /* create engine */
    result = slCreateEngine(&engineObject, 0, NULL, 0, NULL, NULL);
    if (SL_RESULT_SUCCESS != result) {
        LOGE("slCreateEngine failed");
        goto error;
    }

    LOGI("slCreateEngine OK");

    /* realize the engine */
    result = (*engineObject)->Realize(engineObject, SL_BOOLEAN_FALSE);
    if (SL_RESULT_SUCCESS != result) {
        LOGE("RealizeEngine failed");
        goto error;
    }

    LOGI("RealizeEngine OK");

    /* get the engine interface, which is needed in order to create other objects */
    result = (*engineObject)->GetInterface(engineObject, SL_IID_ENGINE, &engineEngine);
    if (SL_RESULT_SUCCESS != result) {
        LOGE("EngineGetInterface failed");
        goto error;
    }

    LOGI("EngineGetInterface OK");

    /* create output mix, with environmental reverb specified as a non-required interface */
    /* const SLInterfaceID ids[1] = { SL_IID_ENVIRONMENTALREVERB }; */
    /* const SLboolean req[1] = { SL_BOOLEAN_FALSE }; */

    const SLInterfaceID ids[1] = { SL_IID_VOLUME };
    const SLboolean req[1] = { SL_BOOLEAN_FALSE };
    result = (*engineEngine)->CreateOutputMix(engineEngine, &outputMixObject, 1, ids, req);

    if (SL_RESULT_SUCCESS != result) {
        LOGE("CreateOutputMix failed");
        goto error;
    }
    LOGI("CreateOutputMix OK");

    /* realize the output mix */
    result = (*outputMixObject)->Realize(outputMixObject, SL_BOOLEAN_FALSE);
    if (SL_RESULT_SUCCESS != result) {
        LOGE("RealizeOutputMix failed");
        goto error;
    }
    return 1;

error:
    openslES_DestroyEngine();
    return 0;
}

static void openslES_DestroyPCMPlayer(_THIS);
static void openslES_DestroyPCMRecorder(_THIS);

static void openslES_DestroyEngine()
{
    LOGI("openslES_DestroyEngine()");

//        openslES_DestroyPCMPlayer(this);
//    openslES_DestroyPCMRecorder(this);

    /* destroy output mix object, and invalidate all associated interfaces */
    if (outputMixObject != NULL) {
        (*outputMixObject)->Destroy(outputMixObject);
        outputMixObject = NULL;
        /* outputMixEnvironmentalReverb = NULL; */
    }

    /* destroy engine object, and invalidate all associated interfaces */
    if (engineObject != NULL) {
        (*engineObject)->Destroy(engineObject);
        engineObject = NULL;
        engineEngine = NULL;
    }

    return;
}

/* this callback handler is called every time a buffer finishes playing */
static void
bqPlayerCallback(SLAndroidSimpleBufferQueueItf bq, void *context)
{
    struct SDL_PrivateAudioData *audiodata = (struct SDL_PrivateAudioData *) context;
    static int t = 0;

    /* assert(bq == bqPlayerBufferQueue); */
    /* assert(NULL == context); */

    /* for streaming playback, replace this test by logic to find and fill the next buffer */
#if 0
    if (--nextCount > 0 && NULL != nextBuffer && 0 != nextSize)
    {
        SLresult result;

        /* enqueue another buffer */
        result = (*bqPlayerBufferQueue)->Enqueue(bqPlayerBufferQueue, nextBuffer, nextSize);
        /* the most likely other result is SL_RESULT_BUFFER_INSUFFICIENT, */
        /* which for this code example would indicate a programming error */
        assert(SL_RESULT_SUCCESS == result);
        (void) result;
    }
#endif
    LOGI("SLES: Playback Callmeback %u", t++);
    SDL_SemPost(audiodata->playsem);
    return;
}

static int
openslES_CreatePCMRecorder(_THIS)
{
/*    struct SDL_PrivateAudioData *audiodata = (struct SDL_PrivateAudioData *) this->hidden; */

    LOGE("openslES_CreatePCMRecorder not implimented yet!");
    return SDL_SetError("openslES_CreatePCMRecorder not implimented yet!");
}

static void
openslES_DestroyPCMRecorder(_THIS)
{
/*    struct SDL_PrivateAudioData *audiodata = (struct SDL_PrivateAudioData *) this->hidden; */

    return;
}

static int
openslES_CreatePCMPlayer(_THIS)
{
    struct SDL_PrivateAudioData *audiodata = (struct SDL_PrivateAudioData *) this->hidden;

    SLDataFormat_PCM format_pcm;

    SDL_AudioFormat test_format = 0;
    SLresult result;
    int i;

#if 0

      test_format = SDL_FirstAudioFormat( this->spec.format );

      while (test_format != 0) {

          if (SDL_AUDIO_ISSIGNED(test_format) && SDL_AUDIO_ISINT(test_format)) {
              break;
          }
          test_format = SDL_NextAudioFormat();
      }

      if ( test_format == 0 ) {
          /* Didn't find a compatible format : */
          LOGI( "No compatible audio format!" );
          return SDL_SetError("No compatible audio format!");
      }

      this->spec.format = test_format;
#endif

    /* Update the fragment size as size in bytes */
    SDL_CalculateAudioSpec(&this->spec);

    LOGI("Try to open %u hz %u bit chan %u %s samples %u",
          this->spec.freq, SDL_AUDIO_BITSIZE(this->spec.format),
          this->spec.channels, (test_format & 0x1000) ? "BE" : "LE", this->spec.samples);

    /* configure audio source */
    SLDataLocator_AndroidSimpleBufferQueue loc_bufq = { SL_DATALOCATOR_ANDROIDSIMPLEBUFFERQUEUE, 2 };
    /* SLDataLocator_AndroidSimpleBufferQueue loc_bufq = { SL_DATALOCATOR_ANDROIDSIMPLEBUFFERQUEUE, OPENSLES_BUFFERS }; */

    format_pcm.formatType    = SL_DATAFORMAT_PCM;
    format_pcm.numChannels   = this->spec.channels;
    format_pcm.samplesPerSec = this->spec.freq * 1000;  /* / kilo Hz to milli Hz */
    format_pcm.bitsPerSample = SDL_AUDIO_BITSIZE(this->spec.format);
    format_pcm.containerSize = SDL_AUDIO_BITSIZE(this->spec.format);

    if (SDL_AUDIO_ISBIGENDIAN(this->spec.format)) {
        format_pcm.endianness = SL_BYTEORDER_BIGENDIAN;
    } else {
        format_pcm.endianness = SL_BYTEORDER_LITTLEENDIAN;
    }

/*
#define SL_SPEAKER_FRONT_LEFT            ((SLuint32) 0x00000001)
#define SL_SPEAKER_FRONT_RIGHT           ((SLuint32) 0x00000002)
#define SL_SPEAKER_FRONT_CENTER          ((SLuint32) 0x00000004)
#define SL_SPEAKER_LOW_FREQUENCY         ((SLuint32) 0x00000008)
#define SL_SPEAKER_BACK_LEFT             ((SLuint32) 0x00000010)
#define SL_SPEAKER_BACK_RIGHT            ((SLuint32) 0x00000020)
#define SL_SPEAKER_FRONT_LEFT_OF_CENTER  ((SLuint32) 0x00000040)
#define SL_SPEAKER_FRONT_RIGHT_OF_CENTER ((SLuint32) 0x00000080)
#define SL_SPEAKER_BACK_CENTER           ((SLuint32) 0x00000100)
#define SL_SPEAKER_SIDE_LEFT             ((SLuint32) 0x00000200)
#define SL_SPEAKER_SIDE_RIGHT            ((SLuint32) 0x00000400)
#define SL_SPEAKER_TOP_CENTER            ((SLuint32) 0x00000800)
#define SL_SPEAKER_TOP_FRONT_LEFT        ((SLuint32) 0x00001000)
#define SL_SPEAKER_TOP_FRONT_CENTER      ((SLuint32) 0x00002000)
#define SL_SPEAKER_TOP_FRONT_RIGHT       ((SLuint32) 0x00004000)
#define SL_SPEAKER_TOP_BACK_LEFT         ((SLuint32) 0x00008000)
#define SL_SPEAKER_TOP_BACK_CENTER       ((SLuint32) 0x00010000)
#define SL_SPEAKER_TOP_BACK_RIGHT        ((SLuint32) 0x00020000)
*/

    if (this->spec.channels == 1) {
        format_pcm.channelMask = SL_SPEAKER_FRONT_CENTER;
    } else if (this->spec.channels == 2) {
        format_pcm.channelMask = SL_SPEAKER_FRONT_LEFT | SL_SPEAKER_FRONT_RIGHT;
    } else if (this->spec.channels == 3) {
        format_pcm.channelMask = SL_SPEAKER_FRONT_LEFT | SL_SPEAKER_FRONT_RIGHT | SL_SPEAKER_FRONT_CENTER;
    } else if (this->spec.channels == 4) {
        format_pcm.channelMask = SL_SPEAKER_FRONT_LEFT | SL_SPEAKER_FRONT_RIGHT |
              SL_SPEAKER_BACK_LEFT | SL_SPEAKER_BACK_RIGHT;
    } else {
        format_pcm.channelMask = SL_SPEAKER_FRONT_LEFT | SL_SPEAKER_FRONT_RIGHT |
              SL_SPEAKER_BACK_LEFT | SL_SPEAKER_BACK_RIGHT | SL_SPEAKER_FRONT_CENTER;
    }

    SLDataSource audioSrc = { &loc_bufq, &format_pcm };

    /* configure audio sink */
    SLDataLocator_OutputMix loc_outmix = { SL_DATALOCATOR_OUTPUTMIX, outputMixObject };
    SLDataSink audioSnk = { &loc_outmix, NULL };

    /* create audio player */
    const SLInterfaceID ids[2] = {
        SL_IID_ANDROIDSIMPLEBUFFERQUEUE,
        SL_IID_VOLUME
    };

    const SLboolean req[2] = {
        SL_BOOLEAN_TRUE,
        SL_BOOLEAN_FALSE,
    };

    result = (*engineEngine)->CreateAudioPlayer(engineEngine, &bqPlayerObject, &audioSrc, &audioSnk, 2, ids, req);
    if (SL_RESULT_SUCCESS != result) {
        LOGE("CreateAudioPlayer failed");
        goto failed;
    }

    /* realize the player */
    result = (*bqPlayerObject)->Realize(bqPlayerObject, SL_BOOLEAN_FALSE);
    if (SL_RESULT_SUCCESS != result) {
        LOGE("RealizeAudioPlayer failed");
        goto failed;
    }

    /* get the play interface */
    result = (*bqPlayerObject)->GetInterface(bqPlayerObject, SL_IID_PLAY, &bqPlayerItf);
    if (SL_RESULT_SUCCESS != result) {
        LOGE("SL_IID_PLAY interface get failed");
        goto failed;
    }

    /* get the buffer queue interface */
    result = (*bqPlayerObject)->GetInterface(bqPlayerObject, SL_IID_ANDROIDSIMPLEBUFFERQUEUE, &bqPlayerBufferQueue);
    if (SL_RESULT_SUCCESS != result) {
        LOGE("SL_IID_BUFFERQUEUE interface get failed");
        goto failed;
    }

    /* register callback on the buffer queue */
    /* context is '(SDL_PrivateAudioData *)this->hidden' */
    result = (*bqPlayerBufferQueue)->RegisterCallback(bqPlayerBufferQueue, bqPlayerCallback, this->hidden);
    if (SL_RESULT_SUCCESS != result) {
        LOGE("RegisterCallback failed");
        goto failed;
    }

#if 0
    /* get the effect send interface */
    result = (*bqPlayerObject)->GetInterface(bqPlayerObject, SL_IID_EFFECTSEND, &bqPlayerEffectSend);
    if (SL_RESULT_SUCCESS != result)
    {

        LOGE("SL_IID_EFFECTSEND interface get failed");
        goto failed;
    }
#endif

#if 0   /* mute/solo is not supported for sources that are known to be mono, as this is */
    /* get the mute/solo interface */
    result = (*bqPlayerObject)->GetInterface(bqPlayerObject, SL_IID_MUTESOLO, &bqPlayerMuteSolo);
    assert(SL_RESULT_SUCCESS == result);
    (void) result;
#endif

    /* get the volume interface */
    result = (*bqPlayerObject)->GetInterface(bqPlayerObject, SL_IID_VOLUME, &bqPlayerVolume);
    if (SL_RESULT_SUCCESS != result) {
        LOGE("SL_IID_VOLUME interface get failed");
        /* goto failed; */
    }

    /* Create the audio buffer semaphore */
    audiodata->playsem = SDL_CreateSemaphore(NUM_BUFFERS - 1);
    if (!audiodata->playsem) {
        LOGE("cannot create Semaphore!");
        goto failed;
    }

    /* Create the sound buffers */
    audiodata->mixbuff = (Uint8 *) SDL_malloc(NUM_BUFFERS * this->spec.size);
    if (audiodata->mixbuff == NULL) {
        LOGE("mixbuffer allocate - out of memory");
        goto failed;
    }

    for (i = 0; i < NUM_BUFFERS; i++) {
        audiodata->pmixbuff[i] = audiodata->mixbuff + i * this->spec.size;
    }

    /* set the player's state to playing */
    result = (*bqPlayerItf)->SetPlayState(bqPlayerItf, SL_PLAYSTATE_PLAYING);
    if (SL_RESULT_SUCCESS != result) {
        LOGE("Play set state failed");
        goto failed;
    }

    return 0;

failed:

    openslES_DestroyPCMPlayer(this);

    return SDL_SetError("Open device failed!");
}

static void
openslES_DestroyPCMPlayer(_THIS)
{
    struct SDL_PrivateAudioData *audiodata = (struct SDL_PrivateAudioData *) this->hidden;
    SLresult result;

    /* set the player's state to 'stopped' */
    result = (*bqPlayerItf)->SetPlayState(bqPlayerItf, SL_PLAYSTATE_STOPPED);
    if (SL_RESULT_SUCCESS != result) {
        SDL_SetError("Stopped set state failed");
    }

    /* destroy buffer queue audio player object, and invalidate all associated interfaces */
    if (bqPlayerObject != NULL) {

        (*bqPlayerObject)->Destroy(bqPlayerObject);

        bqPlayerObject = NULL;
        bqPlayerItf = NULL;
        bqPlayerBufferQueue = NULL;
        /* bqPlayerEffectSend = NULL; */
        bqPlayerMuteSolo = NULL;
        bqPlayerVolume = NULL;
    }

    if (audiodata->playsem) {
        SDL_DestroySemaphore(audiodata->playsem);
        audiodata->playsem = NULL;
    }

    if (audiodata->mixbuff) {
        SDL_free(audiodata->mixbuff);
    }

    return;
}

static int
openslES_OpenDevice(_THIS, void *handle, const char *devname, int iscapture)
{
    this->hidden = (struct SDL_PrivateAudioData *) SDL_calloc(1, (sizeof *this->hidden));
    if (this->hidden == NULL) {
        return SDL_OutOfMemory();
    }

    if (iscapture) {
        LOGI("openslES_OpenDevice( ) %s for capture", devname);
        return openslES_CreatePCMRecorder(this);
    } else {
        LOGI("openslES_OpenDevice( ) %s for playing", devname);
        return openslES_CreatePCMPlayer(this);
    }
}

static void
openslES_CloseDevice(_THIS)
{
    /* struct SDL_PrivateAudioData *audiodata = (struct SDL_PrivateAudioData *) this->hidden; */

    if (this->iscapture) {
        LOGI("openslES_CloseDevice( ) for capture");
        openslES_DestroyPCMRecorder(this);
    } else {
        LOGI("openslES_CloseDevice( ) for playing");
        openslES_DestroyPCMPlayer(this);
    }

    SDL_free(this->hidden);

    return;
}

static void
openslES_WaitDevice(_THIS)
{
    struct SDL_PrivateAudioData *audiodata = (struct SDL_PrivateAudioData *) this->hidden;

    LOGI("openslES_WaitDevice( )");

    /* Wait for an audio chunk to finish */
    /* WaitForSingleObject(this->hidden->audio_sem, INFINITE); */
    SDL_SemWait(audiodata->playsem);

    return;
}

/*/           n   playn sem */
/* getbuf     0   -     1 */
/* fill buff  0   -     1 */
/* play       0 - 0     1 */
/* wait       1   0     0 */
/* getbuf     1   0     0 */
/* fill buff  1   0     0 */
/* play       0   0     0 */
/* wait */
/* */
/* okay.. */

static Uint8 *
openslES_GetDeviceBuf(_THIS)
{
    struct SDL_PrivateAudioData *audiodata = (struct SDL_PrivateAudioData *) this->hidden;

    LOGI("openslES_GetDeviceBuf( )");
    return audiodata->pmixbuff[audiodata->next_buffer];
}

static void
openslES_PlayDevice(_THIS)
{
    struct SDL_PrivateAudioData *audiodata = (struct SDL_PrivateAudioData *) this->hidden;
    SLresult result;

    LOGI("======openslES_PlayDevice( )======");
    /* Queue it up */

    result = (*bqPlayerBufferQueue)->Enqueue(bqPlayerBufferQueue, audiodata->pmixbuff[audiodata->next_buffer], this->spec.size);
    if (SL_RESULT_SUCCESS != result) {
        /* just puk here */
        /* next ! */
    }

    audiodata->next_buffer++;
    if (audiodata->next_buffer >= NUM_BUFFERS) {
        audiodata->next_buffer = 0;
    }

    return;
}

static int
openslES_Init(SDL_AudioDriverImpl * impl)
{
    LOGI("openslES_Init() called");

    if (!openslES_CreateEngine()) {
        return 0;
    }

    LOGI("openslES_Init() - set pointers");

    /* Set the function pointers */
    /* impl->DetectDevices = openslES_DetectDevices; */
    impl->OpenDevice    = openslES_OpenDevice;
    impl->CloseDevice   = openslES_CloseDevice;
    impl->PlayDevice    = openslES_PlayDevice;
    impl->GetDeviceBuf  = openslES_GetDeviceBuf;
    impl->Deinitialize  = openslES_DestroyEngine;
    impl->WaitDevice    = openslES_WaitDevice;

    /* and the capabilities */
    impl->HasCaptureSupport             = 0;        /* TODO */
    impl->OnlyHasDefaultOutputDevice    = 1;
    /* impl->OnlyHasDefaultInputDevice  = 1; */

    LOGI("openslES_Init() - succes");

    /* this audio target is available. */
    return 1;
}

AudioBootStrap openslES_bootstrap = {
    "openslES", "opensl ES audio driver", openslES_Init, 0
};

void openslES_ResumeDevices()
{
    SLresult result;

    /* set the player's state to 'playing' */
    result = (*bqPlayerItf)->SetPlayState(bqPlayerItf, SL_PLAYSTATE_PLAYING);
    if (SL_RESULT_SUCCESS != result) {
        SDL_SetError("Play set state failed");
    }
}

void openslES_PauseDevices()
{
    SLresult result;

    /* set the player's state to 'paused' */
    result = (*bqPlayerItf)->SetPlayState(bqPlayerItf, SL_PLAYSTATE_PAUSED);
    if (SL_RESULT_SUCCESS != result) {
        SDL_SetError("Playe set state failed");
    }
}

#endif /* SDL_AUDIO_DRIVER_OPENSLES */

/* vi: set ts=4 sw=4 expandtab: */
