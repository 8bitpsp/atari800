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
  short *Stream;
  int Offset;
  int Length;
} ChannelBuffer;

typedef struct {
  int ThreadHandle;
  int Handle;
  int LeftVolume;
  int RightVolume;
  pspAudioCallback Callback;
  void *Userdata;
	ChannelBuffer Buffers[2];
} ChannelInfo;

static ChannelInfo AudioStatus[AUDIO_CHANNELS];

static int AudioChannelThread(int args, void *argp);
static void FreeBuffers();
static int OutputBlocking(unsigned int channel, void *buf, int length);

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
      AudioStatus[i].Buffers[j].Stream = NULL;
      AudioStatus[i].Buffers[j].Offset = 0;
      AudioStatus[i].Buffers[j].Length = 0;
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
      if (!(AudioStatus[i].Buffers[j].Stream = 
        (short*)malloc(SampleCount * 2 * sizeof(short))))
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
  if (left > PSP_AUDIO_MAX_VOLUME) left = PSP_AUDIO_MAX_VOLUME;
  if (right > PSP_AUDIO_MAX_VOLUME) right = PSP_AUDIO_MAX_VOLUME;

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
  ChannelInfo *channel_status = &AudioStatus[channel];
  ChannelBuffer *cur_buf, *inv_buf;

  /* Clear buffer */
  for (i = 0; i < 2; i++)
    memset(channel_status->Buffers[i].Stream, 0, 
      sizeof(short) * SampleCount << 1);

  /* Audio loop */
  while (!StopAudio)
  {
    cur_buf = &channel_status->Buffers[buf_idx];
    inv_buf = &channel_status->Buffers[~buf_idx & 1];
    buf_ptr = usr_buf_ptr = (int*)cur_buf->Stream;

    length = SampleCount;
    buf_len = 0;

    if (inv_buf->Offset != 0)
    {
      src_ptr = (int*)inv_buf->Stream + inv_buf->Offset;
      buf_len = inv_buf->Length;

      /* Some remaining data from last audio render */
      memcpy(buf_ptr, src_ptr, buf_len << 2); // in bytes
      usr_buf_ptr += buf_len;
      length -= buf_len;
    }

    cur_buf->Offset = 0;

    if (channel_status->Callback)
    {
      channel_status->Callback(usr_buf_ptr, &length, channel_status->Userdata);
      length += buf_len;
    }
    else
    {
      for (i = 0, ptr = usr_buf_ptr; i < length; i++) 
        *(ptr++) = 0;
    }

    if ((buf_len = length & 63))  // length % 64 != 0
    {
      length &= ~63; // length -= (length % 64)
      cur_buf->Offset = length;
      cur_buf->Length = buf_len;
    }

	  OutputBlocking(channel, buf_ptr, length);

    buf_idx = ~buf_idx & 1;
  }

  sceKernelExitThread(0);
  return 0;
}

int pspAudioOutputBlocking(unsigned int channel, void *buf)
{
  return OutputBlocking(channel, buf, SampleCount);
}

static int OutputBlocking(unsigned int channel, void *buf, int length)
{
  if (!AudioReady || channel >= AUDIO_CHANNELS) return -1;
  
  ChannelInfo *channel_status = &AudioStatus[channel];

  sceAudioSetChannelDataLen(channel_status->Handle, length);
  return sceAudioOutputPannedBlocking(channel_status->Handle,
    channel_status->LeftVolume, channel_status->RightVolume, buf);
}

static void FreeBuffers()
{
  int i, j;

  for (i = 0; i < AUDIO_CHANNELS; i++)
  {
    for (j = 0; j < 2; j++)
    {
      if (AudioStatus[i].Buffers[j].Stream)
      {
        free(AudioStatus[i].Buffers[j].Stream);
        AudioStatus[i].Buffers[j].Stream = NULL;
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

