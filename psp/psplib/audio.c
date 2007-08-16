/** PSP helper library ***************************************/
/**                                                         **/
/**                          audio.c                        **/
/**                                                         **/
/** This file contains the audio rendering library. It is   **/
/** based on the pspaudio library by Adresd and Marcus R.   **/
/** Brown, 2005.                                            **/
/**                                                         **/
/** Akop Karapetyan 2007                                    **/
/*************************************************************/
#include "audio.h"

#include <stdio.h>
#include <pspaudio.h>
#include <pspthreadman.h>
#include <string.h>
#include <malloc.h>

#define AUDIO_CHANNELS       1
#define DEFAULT_SAMPLE_COUNT 512

static int AudioReady;
static volatile int StopAudio;
static int SampleCount;

typedef struct {
  int ThreadHandle;
  int Handle;
  int LeftVolume;
  int RightVolume;
  pspAudioCallback Callback;
  void *Userdata;
} ChannelInfo;

/* TODO: move all these under a single struct */
static ChannelInfo AudioStatus[AUDIO_CHANNELS];
static short *AudioBuffer[AUDIO_CHANNELS][2];
static int AudioBufferOffset[AUDIO_CHANNELS][2];
static int AudioBufferLength[AUDIO_CHANNELS][2];

static int AudioChannelThread(int args, void *argp);
static void FreeBuffers();
static int OutputBlocking(unsigned int channel, unsigned int vol1, 
  unsigned int vol2, void *buf, int length);

int pspAudioInit(int sample_count)
{
  int i, j, failed;

  StopAudio = 0;
  AudioReady = 0;

  for (i = 0; i < AUDIO_CHANNELS; i++)
  {
    AudioStatus[i].Handle = -1;
    AudioStatus[i].ThreadHandle = -1;
    AudioStatus[i].LeftVolume = PSP_AUDIO_MAX_VOLUME;
    AudioStatus[i].RightVolume = PSP_AUDIO_MAX_VOLUME;
    AudioStatus[i].Callback = NULL;
    AudioStatus[i].Userdata = NULL;

    for (j = 0; j < 2; j++)
    {
      AudioBuffer[i][j] = NULL;
      AudioBufferOffset[i][j] = 0;
      AudioBufferLength[i][j] = 0;
    }
  }

  if ((SampleCount = sample_count) <= 0)
    SampleCount = DEFAULT_SAMPLE_COUNT;

  /* 64 = buffer zone */
  SampleCount = PSP_AUDIO_SAMPLE_ALIGN(SampleCount) + 64;

  for (i = 0; i < AUDIO_CHANNELS; i++)
  {
    for (j = 0; j < 2; j++)
    {
      if (!(AudioBuffer[i][j] = (short*)malloc(SampleCount * 2 * sizeof(short))))
      {
        FreeBuffers();
        return 0;
      }
    }
  }
  
  for (i = 0, failed = 0; i < AUDIO_CHANNELS; i++)
  {
    AudioStatus[i].Handle = sceAudioChReserve(PSP_AUDIO_NEXT_CHANNEL, 
      SampleCount, PSP_AUDIO_FORMAT_STEREO);

    if (AudioStatus[i].Handle < 0)
    { 
      failed = 1;
      break;
    }
  }

  if (failed)
  {
    for (i = 0; i < AUDIO_CHANNELS; i++)
    {
      if (AudioStatus[i].Handle != -1)
      {
        sceAudioChRelease(AudioStatus[i].Handle);
        AudioStatus[i].Handle = -1;
      }
    }

    FreeBuffers();
    return 0;
  }

  AudioReady = 1;

  char label[16];
  strcpy(label, "audiotX");

  for (i = 0; i < AUDIO_CHANNELS; i++)
  {
    label[6] = '0' + i;
    AudioStatus[i].ThreadHandle = 
      sceKernelCreateThread(label, (void*)&AudioChannelThread, 0x12, 0x10000, 
        0, NULL);

    if (AudioStatus[i].ThreadHandle < 0)
    {
      AudioStatus[i].ThreadHandle = -1;
      failed = 1;
      break;
    }

    if (sceKernelStartThread(AudioStatus[i].ThreadHandle, sizeof(i), &i) != 0)
    {
      failed = 1;
      break;
    }
  }

  if (failed)
  {
    StopAudio = 1;
    for (i = 0; i < AUDIO_CHANNELS; i++)
    {
      if (AudioStatus[i].ThreadHandle != -1)
      {
        //sceKernelWaitThreadEnd(AudioStatus[i].threadhandle,NULL);
        sceKernelDeleteThread(AudioStatus[i].ThreadHandle);
      }

      AudioStatus[i].ThreadHandle = -1;
    }

    AudioReady = 0;
    FreeBuffers();
    return 0;
  }

  return SampleCount;
}

void pspAudioShutdown()
{
  int i;
  AudioReady = 0;
  StopAudio = 1;

  for (i = 0; i < AUDIO_CHANNELS; i++)
  {
    if (AudioStatus[i].ThreadHandle != -1)
    {
      //sceKernelWaitThreadEnd(AudioStatus[i].threadhandle,NULL);
      sceKernelDeleteThread(AudioStatus[i].ThreadHandle);
    }

    AudioStatus[i].ThreadHandle = -1;
  }

  for (i = 0; i < AUDIO_CHANNELS; i++)
  {
    if (AudioStatus[i].Handle != -1)
    {
      sceAudioChRelease(AudioStatus[i].Handle);
      AudioStatus[i].Handle = -1;
    }
  }

  FreeBuffers();
}

void pspAudioSetVolume(int channel, int left, int right)
{
  AudioStatus[channel].LeftVolume = left;
  AudioStatus[channel].RightVolume = right;
}

/* Playback thread for each channel */
static int AudioChannelThread(int args, void *argp)
{
  volatile int buf_idx = 0;
  int channel = *(int*)argp;
  int i, buf_len;
  unsigned int length;
  int *buf_ptr, *usr_buf_ptr, *src_ptr, *ptr;
  pspAudioCallback callback;
  /* TODO: */
  //void *channel_buffer = AudioBuffer[channel];

  /* Clear buffer */
  for (i = 0; i < 2; i++)
    memset(AudioBuffer[channel][buf_idx], 0, sizeof(short) * SampleCount * 2);

  while (!StopAudio)
  {
    callback = AudioStatus[channel].Callback;
    buf_ptr = usr_buf_ptr = (int*)AudioBuffer[channel][buf_idx];
    length = SampleCount;

    if (callback)
    {
      buf_len = 0;

      if (AudioBufferOffset[channel][buf_idx] != 0)
      {
        src_ptr = buf_ptr + AudioBufferOffset[channel][buf_idx];
        buf_len = AudioBufferLength[channel][buf_idx];

        /* Some remaining data from last audio render */
        memcpy(buf_ptr, src_ptr, buf_len << 2); // in bytes
        usr_buf_ptr += buf_len;
        length -= buf_len;
      }

      callback(usr_buf_ptr, &length, AudioStatus[channel].Userdata);
      length += buf_len;

      if ((buf_len = length & 63))  // length % 64
      {
        /* Length not divisible by 64 */
        AudioBufferLength[channel][buf_idx] = buf_len;
        length &= ~63; // -= length % 64
        AudioBufferOffset[channel][buf_idx] = length;
      }
    }
    /* TODO: this looks like it may corrupt sound when pausing/continuing */
    else for (i = 0, ptr = buf_ptr; i < SampleCount; i++) 
      *(ptr++) = 0;

	  OutputBlocking(channel, AudioStatus[channel].LeftVolume, 
      AudioStatus[channel].RightVolume, buf_ptr, length);

    buf_idx = (buf_idx ? 0 : 1);
  }

  sceKernelExitThread(0);
  return 0;
}

int pspAudioOutputBlocking(unsigned int channel, void *buf)
{
  return OutputBlocking(channel, AudioStatus[channel].LeftVolume,
    AudioStatus[channel].RightVolume, buf, SampleCount);
}

static int OutputBlocking(unsigned int channel,
  unsigned int vol1, unsigned int vol2, void *buf, int length)
{
  if (!AudioReady || channel >= AUDIO_CHANNELS) return -1;
  if (vol1 > PSP_AUDIO_MAX_VOLUME) vol1 = PSP_AUDIO_MAX_VOLUME;
  if (vol2 > PSP_AUDIO_MAX_VOLUME) vol2 = PSP_AUDIO_MAX_VOLUME;

  sceAudioSetChannelDataLen(AudioStatus[channel].Handle, length);
  return sceAudioOutputPannedBlocking(AudioStatus[channel].Handle,
    vol1, vol2, buf);
}

static void FreeBuffers()
{
  int i, j;

  for (i = 0; i < AUDIO_CHANNELS; i++)
  {
    for (j = 0; j < 2; j++)
    {
      if (AudioBuffer[i][j])
      {
        free(AudioBuffer[i][j]);
        AudioBuffer[i][j] = NULL;
      }
    }
  }
}

void pspAudioSetChannelCallback(int channel, pspAudioCallback callback, void *userdata)
{
  volatile ChannelInfo *pci = &AudioStatus[channel];
  pci->Callback = NULL;
  pci->Userdata = userdata;
  pci->Callback = callback;
}
int pspAudioGetSampleCount()
{
  return SampleCount;
}

