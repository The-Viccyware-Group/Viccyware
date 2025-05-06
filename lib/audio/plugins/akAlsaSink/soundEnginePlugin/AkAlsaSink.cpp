#include <AK/AkWwiseSDKVersion.h>
#include "AkAlsaSink.h"
#include "Logging.h"
#include <alsa/pcm.h>
#include <sched.h>

#define PA_DEFAULT_CHANNEL_MASK 3

using namespace AK::CustomHardware;


static AkUInt32 g_uBuffersNeeded = 0; //Global for Main output to set only

AK::IAkPlugin* CreateAkAlsaSink(AK::IAkPluginMemAlloc * in_pAllocator)
{
	AKASSERT( in_pAllocator != NULL );
	return AK_PLUGIN_NEW( in_pAllocator, AkAlsaSink() );
}

AK::IAkPluginParam* CreateAkAlsaSinkParams(AK::IAkPluginMemAlloc * in_pAllocator)
{
	AKASSERT(in_pAllocator != NULL);
	return AK_PLUGIN_NEW(in_pAllocator, AkAlsaSinkPluginParams());
}

//Static initializer object to register automatically the plugin into the sound engine
AK::PluginRegistration AkAlsaSinkRegistration(AkPluginTypeSink, AKCOMPANYID_AUDIOKINETIC, AKEFFECTID_ALSASINK, CreateAkAlsaSink, CreateAkAlsaSinkParams);

AkOutputGetLatencyCallbackFunc AkAlsaSink::m_pfnGetLatencyCallback = NULL;
AkUInt32 AkAlsaSink::m_uLatencyReportThresholdMsec = AKALSASINK_DEFAULT_LATENCY_THRESHOLD;

static int alsa_error_recovery(void *in_puserdata, snd_pcm_t *in_phandle, int in_err)
{
	AkAlsaSink *  pAlsaSinkDevice = (AkAlsaSink *)in_puserdata;
	int returnVal=0;

	if (in_err == -EPIPE) //Underrun
	{
		LOG_ERROR("Underrun, try prepare: %s", snd_strerror(in_err));

		pAlsaSinkDevice->m_uUnderflowCount++;
		returnVal = snd_pcm_prepare(in_phandle);
		if (returnVal < 0)
		{
			LOG_ERROR("Can't recovery from underrun, prepare failed: %s", snd_strerror(returnVal));
		}

		return returnVal;
	}
	else if (in_err == -ESTRPIPE) //Suspend
	{
		LOG_ERROR("Stream is suspended");
		while ((returnVal = snd_pcm_resume(in_phandle)) == -EAGAIN)
		{
			sleep(1);       /* wait until the suspend flag is released */
		}

		if (returnVal < 0)
		{
			returnVal = snd_pcm_prepare(in_phandle);
			if (returnVal < 0)
			{
				LOG_ERROR("Can't recovery from suspend, prepare failed: %s", snd_strerror(returnVal));
			}

		}

		return returnVal;
	}

	return in_err;
}

// ---------------------- Alsa Helper function -----------------------------------------------
// Alsa audio Thread, handling writing of the alsa stream and pushing data to the audio buffer
AK_DECLARE_THREAD_ROUTINE(AlsaSinkAudioThread)
{

	AkAlsaSink *  pAlsaSinkDevice = (AkAlsaSink*)lpParameter;
	AK_INSTRUMENT_THREAD_START( "AlsaSinkAudioThread" );

	unsigned int SampleRate = pAlsaSinkDevice->m_uSampleRate;
	unsigned int WaitSleepTimeUs = ((float)pAlsaSinkDevice->m_uFrameSize/(float)SampleRate)*1000000/3;
	unsigned int CompSleepTimeUs = 0;

	while(pAlsaSinkDevice->m_bThreadRun)
	{
		snd_pcm_sframes_t frameAvail;
		int returnVal;

		//Check how much buffer space we have
		frameAvail = snd_pcm_avail(pAlsaSinkDevice->m_pPcmHandle);
		LOG_DEBUG("frameAvail : %i",frameAvail);
		if(frameAvail< 0)
		{
			returnVal = alsa_error_recovery(pAlsaSinkDevice, pAlsaSinkDevice->m_pPcmHandle, frameAvail);
			if(returnVal < 0)
			{
				LOG_ERROR("Alsa unable to recover from error");
				frameAvail = 0;
				pAlsaSinkDevice->m_bThreadRun = false;
				break;
			}
			else
			{

				//Check again how much space is available
				frameAvail = snd_pcm_avail_update(pAlsaSinkDevice->m_pPcmHandle);
				if(frameAvail<0)
				{
					//After prepare not supposed to underrun again
					LOG_WARN("Alsa stream multiple underrun, problem detected");
					frameAvail = 0;
				}
			}
		}

		if (frameAvail >= pAlsaSinkDevice->m_uFrameSize)
		{
			unsigned int nbFrames = frameAvail/pAlsaSinkDevice->m_uFrameSize;
			//Generate SignalThread
			if((pAlsaSinkDevice->m_bIsPrimary == true) && (pAlsaSinkDevice->m_bMasterMode == true))
			{
				pAlsaSinkDevice->m_pSinkPluginContext->SignalAudioThread();
				LOG_DEBUG("Generate signalAudioThread");
			}
			CompSleepTimeUs = (unsigned int)((((float)(nbFrames*pAlsaSinkDevice->m_uFrameSize)/(float)SampleRate)*1000000)) - WaitSleepTimeUs;
			LOG_DEBUG("RequestSleep %i us", CompSleepTimeUs);
			usleep(CompSleepTimeUs);
		}
		else
		{
			// Wait for next blocksize
			LOG_DEBUG("WaitSleep %i us", WaitSleepTimeUs);
			usleep(WaitSleepTimeUs);
		}
	}
	AkExitThread(AK_RETURN_THREAD_OK);
}

//Callback to check audio avaibility and wake up Wwise audio thread
void asyncAudioCb(snd_async_handler_t *pPcmCallback)
{

	if(pPcmCallback!=NULL)
	{
		AkAlsaSink *pAlsaSinkDevice = (AkAlsaSink*)snd_async_handler_get_callback_private(pPcmCallback);

		//Need to be protect, Alsa is not thread safe.
		pAlsaSinkDevice->m_threadlock.Lock();
		snd_pcm_t *pPcmHandle = snd_async_handler_get_pcm(pPcmCallback);
		snd_pcm_sframes_t frameAvail;
		int returnVal;

		//Check how much buffer space we have
		frameAvail = snd_pcm_avail_update(pPcmHandle);
		if(frameAvail< 0)
		{
			returnVal = alsa_error_recovery(pAlsaSinkDevice, pPcmHandle, frameAvail);
			if(returnVal < 0)
			{
				LOG_WARN("Alsa unable to recover from error: %s", snd_strerror(returnVal));
				frameAvail = 0;
			}
			else
			{
				//Check again how much space is available
				frameAvail = snd_pcm_avail_update(pPcmHandle);
				if(frameAvail<0)
				{
					//After prepare not supposed to underrun again
					LOG_WARN("Alsa stream multiple underrun, problem detected: %s", snd_strerror(returnVal));
					frameAvail = 0;
				}
			}
		}

		if (frameAvail >= pAlsaSinkDevice->m_uFrameSize)
		{
			if((pAlsaSinkDevice->m_bIsPrimary == true) && (pAlsaSinkDevice->m_bMasterMode == true))
			{
				pAlsaSinkDevice->m_pSinkPluginContext->SignalAudioThread();
				LOG_DEBUG("Generate signalAudioThread");
			}
		}

		LOG_DEBUG("frameAvail : %i",frameAvail);
		pAlsaSinkDevice->m_threadlock.Unlock();
	}
}


// Initializes the ALSA
static AKRESULT alsa_stream_connect(void *in_puserdata)
{
	AkAlsaSink *  pAlsaSinkDevice = (AkAlsaSink *)in_puserdata;
	int retValue;

	// Handle for the PCM device
	snd_pcm_t *pPcmHandle;

	//
	// Name of the PCM device, like plughw:0,0, hw:0,0, default, pulse, etc
	//
	// The first number is the number of the soundcard,
	// the second number is the number of the device.
	char *pDeviceName;
	pDeviceName = pAlsaSinkDevice->m_pDeviceName;

	// Open PCM. The last parameter of this function is the mode.
	// If this is set to 0, the standard mode is used. Possible
	// other values are SND_PCM_NONBLOCK and SND_PCM_ASYNC.
	// If SND_PCM_NONBLOCK is used, read / write access to the
	// PCM device will return immediately. If SND_PCM_ASYNC is
	// specified, SIGIO will be emitted whenever a period has
	// been completely processed by the soundcard.
	retValue = snd_pcm_open(&pPcmHandle, pDeviceName, SND_PCM_STREAM_PLAYBACK, SND_PCM_NONBLOCK);
	if (retValue  < 0)
	{
	  LOG_ERROR("Error opening PCM device %s", pDeviceName);
	  return AK_Fail;
	}

    // Init hwparams with full configuration space
	retValue = snd_pcm_hw_params_any(pPcmHandle, pAlsaSinkDevice->m_phwparams);
    if (retValue < 0)
    {
      LOG_ERROR("Can not configure this PCM device");
      return AK_Fail;
    }

    //Get Wwise sample rate
    unsigned int uRate = pAlsaSinkDevice->m_uSampleRate;

    //Variable to stored exact rate return by ALSA
    unsigned int uSampleRate;

    //Set access type
    //Possible value are SND_PCM_ACCESS_RW_INTERLEAVED or SND_PCM_ACCESS_RW_NONINTERLEAVED
    retValue = snd_pcm_hw_params_set_access(pPcmHandle, pAlsaSinkDevice->m_phwparams, SND_PCM_ACCESS_RW_INTERLEAVED);
	if (retValue < 0)
	{
	  LOG_ERROR("Error setting access");
	  return AK_Fail;
	}

	// Set sample format
	retValue = snd_pcm_hw_params_set_format(pPcmHandle, pAlsaSinkDevice->m_phwparams, SND_PCM_FORMAT_S16_LE);
	if(retValue < 0)
	{
	  LOG_ERROR("int16 not support by hardware");
	  return AK_Fail;
	}

	// Set sample rate. If the exact rate is not supported
	// by the hardware, use nearest possible rate.
	// Use near to be able to see hardware closest supported rate even if we only accept the exact rate
	uSampleRate = uRate;
	retValue = snd_pcm_hw_params_set_rate_near(pPcmHandle, pAlsaSinkDevice->m_phwparams, &uSampleRate, 0);
	if (retValue  < 0)
	{
	  LOG_ERROR("Error setting rate");
	  return AK_Fail;
	}

	if (uRate != uSampleRate)
	{
		LOG_ERROR("The rate %d Hz is not supported by your hardware.\n ==> Please use %d Hz instead.", uRate, uSampleRate);
		return AK_Fail;
	}

	snd_pcm_chmap_query_t **ppChMaps;
	snd_pcm_chmap_t *FoundChMap;

	ppChMaps = snd_pcm_query_chmaps(pPcmHandle);
	if (ppChMaps == NULL)
	{
		LOG_WARN("Unable to detect number of audio channels supported by the Device. Stereo mode is selected by default");
		//Channel mapping is not set because no api to set channel mapping available.
		//Assuming channel mapping in stereo mode will match Wwise stereo channel mapping
		pAlsaSinkDevice->m_uOutNumChannels = AKALSASINK_DEFAULT_NBCHAN;
		pAlsaSinkDevice->m_uChannelMask = AK::ChannelMaskFromNumChannels(pAlsaSinkDevice->m_uOutNumChannels);
	}
	else
	{
		int i=0;

		FoundChMap = &ppChMaps[i]->map;

		//Auto-detect mode
		if(pAlsaSinkDevice->m_uOutNumChannels ==0)
		{
			//In Auto-detect mode we select the first channel mapping available.
			pAlsaSinkDevice->m_uOutNumChannels = FoundChMap->channels;
			pAlsaSinkDevice->m_uChannelMask = AK::ChannelMaskFromNumChannels(FoundChMap->channels);

		}
		else //Custom Mode
		{
			//Search for request nb of channels and report error if no config match with number of channel.
			bool bChannelMapConfigFound = false;

			snd_pcm_chmap_query_t *pChMap = ppChMaps[i];

			//ppChMaps is always terminated with NULL
			while(pChMap!=NULL)
			{
				if(pChMap->map.channels == pAlsaSinkDevice->m_uOutNumChannels)
				{
					bChannelMapConfigFound = true;
					FoundChMap = &ppChMaps[i]->map;
					pChMap = NULL;
				}
				else
				{
					i++;
					pChMap = ppChMaps[i];
				}
			}

			if(bChannelMapConfigFound == false)
			{
			  LOG_ERROR("Audio device does not support %i audio channels", pAlsaSinkDevice->m_uOutNumChannels);
			  return AK_Fail;
			}
		}

		if(pAlsaSinkDevice->m_uOutNumChannels > AKALSASINK_MAXCHANNEL )
		{
			LOG_ERROR("Alsa Sink supported a maximum off 16 channels, requested %i channels", pAlsaSinkDevice->m_uOutNumChannels);
			return AK_Fail;
		}
	}


	// Set number of channels
	retValue = snd_pcm_hw_params_set_channels(pPcmHandle,pAlsaSinkDevice->m_phwparams, pAlsaSinkDevice->m_uOutNumChannels);
	if (retValue < 0)
	{
	  LOG_ERROR("Unable to set request number of channels :%i", pAlsaSinkDevice->m_uOutNumChannels);
	  return AK_Fail;
	}

	unsigned int period_min;
	unsigned int period_max;
	snd_pcm_uframes_t period_size_min;
	snd_pcm_uframes_t period_size_max;

	snd_pcm_uframes_t buffer_size_min;
	snd_pcm_uframes_t buffer_size_max;

	//Get usefull HW parameters
	retValue = snd_pcm_hw_params_get_periods_min(pAlsaSinkDevice->m_phwparams, &period_min,0);
	if (retValue  < 0)
	{
	  LOG_WARN("Unable to get the minimum periods size");
	  period_min = 0;
	}

	retValue = snd_pcm_hw_params_get_periods_max(pAlsaSinkDevice->m_phwparams, &period_max,0);
	if (retValue < 0)
	{
	  LOG_WARN("Error getting the minimum periods size");
	  period_max = 0;
	}

	retValue = snd_pcm_hw_params_get_period_size_min(pAlsaSinkDevice->m_phwparams, &period_size_min,0);
	if (retValue < 0)
	{
	  LOG_WARN("Error getting the minimum periods size");
	  period_size_min = 0;
	}

	retValue = snd_pcm_hw_params_get_period_size_max(pAlsaSinkDevice->m_phwparams, &period_size_max,0);
	if (retValue < 0)
	{
	  LOG_WARN("Error getting the minimum periods size");
	  period_size_max = 0;
	}

	retValue = snd_pcm_hw_params_get_buffer_size_min(pAlsaSinkDevice->m_phwparams, &buffer_size_min);
	if (retValue < 0)
	{
	  LOG_WARN("Error getting the minimum periods size");
	  buffer_size_min = 0;
	}

	retValue = snd_pcm_hw_params_get_buffer_size_max(pAlsaSinkDevice->m_phwparams, &buffer_size_max);
	if (retValue < 0) {
	  LOG_WARN("Error getting the minimum periods size");
	  buffer_size_max = 0;
	}


	//buffer_size = periodsize * periods
	//Latency important parameters
	//The latency is calculated as follow:
	//latency = periodsize*period / (rate * bytes_per_frame)
	//Typical example is as follow:
	//period=2; periodsize=8192 bytes; rate=48000Hz; 16 bits stereo data (bytes_per_frame=4bytes)
	//latency = 8192*2/(48000*4) = 85.3 ms of latency
	snd_pcm_uframes_t buffer_size = pAlsaSinkDevice->m_uFrameSize*pAlsaSinkDevice->m_uNumBuffers;

	//Set the buffer size
	retValue = snd_pcm_hw_params_set_buffer_size_near (pPcmHandle, pAlsaSinkDevice->m_phwparams, &buffer_size);
	if (retValue  < 0) {
	  LOG_ERROR("Error setting buffer size");
	  return AK_Fail;
	}

	unsigned int period = buffer_size/pAlsaSinkDevice->m_uFrameSize;

	//Periods
	retValue = snd_pcm_hw_params_test_periods(pPcmHandle, pAlsaSinkDevice->m_phwparams, period, 0);
	if(retValue < 0)
	{
		LOG_WARN("Request %i period not supported by hardware, will set to the nearest possible value, the latency will be affected",period);

	}

	retValue = snd_pcm_hw_params_set_periods_near(pPcmHandle, pAlsaSinkDevice->m_phwparams, &period, 0);
	if (retValue < 0)
	{
		LOG_ERROR("Error setting period size");
		return AK_Fail;
	}


	LOG_INFO("ALSA buffer size (frames) is set to : %i", buffer_size);
	LOG_INFO("ALSA period is set to : %i", period);

	// Apply HW parameter and prepare device
	retValue = snd_pcm_hw_params(pPcmHandle, pAlsaSinkDevice->m_phwparams);
    if (retValue < 0)
    {
    	LOG_ERROR("Error setting PCM HW PARAMS");
    	return AK_Fail;
    }

    retValue = snd_pcm_hw_params_get_period_size_min(pAlsaSinkDevice->m_phwparams, &period_size_min,0);
	if (retValue < 0)
	{
	  LOG_WARN("Error getting the minimum periods size");
	}

	retValue = snd_pcm_hw_params_get_period_size_max(pAlsaSinkDevice->m_phwparams, &period_size_max,0);
	if (retValue < 0)
	{
	  LOG_WARN("Error getting the minimum periods size");
	}



    //Assign pcmHandle
    pAlsaSinkDevice->m_pPcmHandle = pPcmHandle;

    if(ppChMaps!= NULL)
    {
    	//Wwise standard mapping L-R-C-LFE-RL-RR-RC-SL-SR-HL-HR-HC-HRL-HRR-HRC-T
		//In order to support up to 16 channels
		//Always open the stream with Wwise Standard channel mapping, since it does not change based on channel number
		//The we use m_pChannelMap to remap into run time mapping.
		//Since the remapping does not cost an extra memcpy this solution should keep the same performance.
		//Another solution will be to directly map into Wwise runtime mapping but this has a lot of case to handle.
		if(pAlsaSinkDevice->m_uOutNumChannels > 1) //Channel map only for stereo and up.
		{

			for(unsigned int y=0; y<pAlsaSinkDevice->m_uOutNumChannels; y++)
			{
				switch(y)
				{
					case 0:
							FoundChMap->pos[y] = SND_CHMAP_FL;
							break;
					case 1:
							FoundChMap->pos[y]  = SND_CHMAP_FR;
							break;
					case 2:
							FoundChMap->pos[y]  = SND_CHMAP_FC;
							break;
					case 3:
							FoundChMap->pos[y]  = SND_CHMAP_LFE;
							break;
					case 4:
							FoundChMap->pos[y]  = SND_CHMAP_RL;
							break;
					case 5:
							FoundChMap->pos[y]  = SND_CHMAP_RR;
							break;
					case 6:
							FoundChMap->pos[y]  = SND_CHMAP_RC;
							break;
					case 7:
							FoundChMap->pos[y]  = SND_CHMAP_SL;
							break;
					case 8:
							FoundChMap->pos[y]  = SND_CHMAP_SR;
							break;
					case 9:
							FoundChMap->pos[y]  = SND_CHMAP_TFL;
							break;
					case 10:
							FoundChMap->pos[y]  = SND_CHMAP_TFR;
							break;
					case 11:
							FoundChMap->pos[y]  = SND_CHMAP_TFC;
							break;
					case 12:
							FoundChMap->pos[y]  = SND_CHMAP_TRL;
							break;
					case 13:
							FoundChMap->pos[y]  = SND_CHMAP_TRR;
							break;
					case 14:
							FoundChMap->pos[y]  = SND_CHMAP_TRC;
							break;
					case 15:
							FoundChMap->pos[y]  = SND_CHMAP_TC;
							break;
				}
			}

			retValue =  snd_pcm_set_chmap(pPcmHandle, FoundChMap);
			if(retValue  <0)
			{
				LOG_WARN("Unable to set channel mapping, your channel mapping is not set. Please check your device name.");
			}

		}

		//Free channel maps
		snd_pcm_free_chmaps	(ppChMaps);
    }

    //SW params setting
    retValue = snd_pcm_sw_params_current (pPcmHandle, pAlsaSinkDevice->m_pswparams);
	if(retValue < 0)
	{
		LOG_ERROR("Error gettting the current sw params");
		return AK_Fail;
	}

	//Set start value, minimum is 2 frames
	retValue = snd_pcm_sw_params_set_start_threshold(pPcmHandle, pAlsaSinkDevice->m_pswparams, pAlsaSinkDevice->m_uFrameSize*AKALSASINK_MIN_NB_OUTBUFFER);
	if( retValue < 0)
	{
		LOG_ERROR("Error setting the start threashold");
		return AK_Fail;
	}

	retValue = snd_pcm_sw_params_set_avail_min(pPcmHandle, pAlsaSinkDevice->m_pswparams, pAlsaSinkDevice->m_uFrameSize);
	if(retValue < 0)
	{
		LOG_ERROR("Error setting avail minimum");
		return AK_Fail;
	}

	retValue = snd_pcm_sw_params(pPcmHandle, pAlsaSinkDevice->m_pswparams);
	if(retValue < 0)
	{
		LOG_ERROR("Error writing sw params");
		return AK_Fail;
	}

	//Only create Async Handler or custom thread for Primary
	snd_async_handler_t *pcm_callback;
	if(pAlsaSinkDevice->m_bIsPrimary && pAlsaSinkDevice->m_bMasterMode)
	{
		retValue = snd_async_add_pcm_handler(&pcm_callback, pPcmHandle, asyncAudioCb, pAlsaSinkDevice);
		if(retValue < 0)
		{
			if(pAlsaSinkDevice->m_bIsPrimary)
			{
				pAlsaSinkDevice->m_bCustomThreadEnable = true;
			}
			LOG_WARN("ALSA Async Callback API not supported, custom thread will be created");
		}
		else
		{
			pAlsaSinkDevice->m_pPcm_callback_handle = pcm_callback;
		}
	}


	//Init stream
	retValue = snd_pcm_prepare (pPcmHandle);
	if (retValue  < 0)
	{
		LOG_ERROR("cannot prepare audio interface for use");
		return AK_Fail;
	}


    return AK_Success;

}


AkAlsaSink::AkAlsaSink()
	: m_pSinkPluginContext( NULL )
	, m_uOutNumChannels(0)
	, m_pChannelMap(NULL)
	, m_uNumBuffers(0)
	, m_uChannelMask(0)
	, m_pDeviceName(NULL)
	, m_bDeviceFound(false)
	, m_uFrameSize(0)
	, m_uLatencyMax(0)
	, m_uLatencyErrorCnt(0)
	, m_uSampleRate(48000)
	, m_pfSinkOutBuffer(NULL)
	, m_uUnderflowCount(0)
	, m_uOverflowCount(0)
	, m_bDataReady(false)
	, m_bIsPrimary(false)
	, m_pSharedParams(NULL)
	, m_pPcmHandle(NULL)
	, m_phwparams(NULL)
	, m_pswparams(NULL)
	, m_bMasterMode(false)
	, m_uOutputID(0)
	, m_LatencyReport(0)
	, m_bIsRunning(false)
	, m_Alsathread(AK_NULL_THREAD)
	, m_bThreadRun(false)
	, m_bCustomThreadEnable(false)
	, m_pPcm_callback_handle(NULL)
{
}

AkAlsaSink::~AkAlsaSink()
{
}

AKRESULT AkAlsaSink::Init(
	AK::IAkPluginMemAlloc *		in_pAllocator,			// Interface to memory allocator to be used by the effect.
	AK::IAkSinkPluginContext *	in_pSinkPluginContext,	// Interface to sink plug-in's context.
    AK::IAkPluginParam *        in_pParams,             // Interface to parameter node
	AkAudioFormat &				io_rFormat				// Audio data format of the input signal. 
	)
{

	// Uncomment for debug logging
	Logging::Instance().SetLogLevel(LogLevelDebug);

	LOG_TRACE(__FUNCTION__);
	LOG_INFO("Alsa sink initialization");

	//init latency error counter
	m_uLatencyErrorCnt = 0;

	m_pSinkPluginContext = in_pSinkPluginContext;
	m_pSharedParams = static_cast<AkAlsaSinkPluginParams*>(in_pParams);
	AkAlsaSinkParamStruct params = m_pSharedParams->GetParams();

	m_bIsPrimary = m_pSinkPluginContext->IsPrimary();
	AkUInt32 uDeviceType = 0;
	AKRESULT eResult = m_pSinkPluginContext->GetOutputID(m_uOutputID, uDeviceType);
	if (eResult != AK_Success)
	{
		LOG_ERROR("Could not retrieve output device type and ID");
		return AK_NotImplemented;
	}

	//Register MasterSink if this sink is the mastersink and used as slave
	if(m_bIsPrimary == true && (params.bMasterMode == false))
	{
    // TODO: Fix after updating to Wwise SDK 2017
		// eResult = RegisterMasterSink(m_pSinkPluginContext);
		// if(eResult != AK_Success)
		// {
		// 	LOG_WARN("Unable to register Master Sink to be use to synchronize with source");
		// }
	}

	m_bMasterMode = params.bMasterMode;

	LOG_INFO("Alsa full device name: %s", params.szDeviceName);
	LOG_INFO("Primary sink: %d Master: %d", m_bIsPrimary,m_bMasterMode);

	// Allocate device array name storage
	m_pDeviceName = (char*) AK_PLUGIN_ALLOC(in_pAllocator, sizeof(char) *AKALSASINK_MAXDEVICENAME_LENGTH);
	if (m_pDeviceName == NULL)
	{
		LOG_ERROR("Could not allocate Alsa output sink device name");
		return AK_InsufficientMemory;
	}

	strcpy(m_pDeviceName,params.szDeviceName);
	if(m_pDeviceName == NULL)
	{
		LOG_ERROR("Device name equal to NULL, fatal error should never happened");
		return AK_InvalidParameter;
	}

	m_bDeviceFound = false;

	m_uNumBuffers = params.uNumBuffers;
	if(m_uNumBuffers < AKALSASINK_MIN_NB_OUTBUFFER)
	{
		LOG_ERROR("Minimum number of output buffer is 2, current number of buffer is: %i", m_uNumBuffers);
		return AK_Fail;
	}

	m_uFrameSize = m_pSinkPluginContext->GlobalContext()->GetMaxBufferLength();
	LOG_INFO("Sound engine frame size: %d", m_uFrameSize);

	m_pChannelMap = (unsigned int *)AK_PLUGIN_ALLOC(in_pAllocator, sizeof(unsigned int)*AKALSASINK_MAXCHANNEL);
	if(m_pChannelMap == NULL)
	{
		LOG_ERROR("Could not allocate channelMap array");
		return AK_InsufficientMemory;
	}


	// Detect/force output configuration for the master bus
	switch (params.eChannelMode)
	{
		default:
		case CHANNELMODE_AUTODETECT: // Auto
			m_uOutNumChannels = 0;
			m_uChannelMask = 0;
			break;
		case CHANNELMODE_ANONYMOUS: // Channels have no precise meaning
			m_uChannelMask = 0;
			m_uOutNumChannels = params.uChannelInfo;  // Channel information parameter represents desired number of channels in anonymous mode
			if (m_uOutNumChannels <= 2)
			{
				m_uChannelMask = AK::ChannelMaskFromNumChannels(m_uOutNumChannels);
			}
			break;
		case CHANNELMODE_CUSTOM: // Use explicitly specified channel mask
			m_uChannelMask = params.uChannelInfo; // Channel mask explicitely provided by channel info parameter
			m_uOutNumChannels = AK::GetNumNonZeroBits(m_uChannelMask);
			break;
		case CHANNELMODE_MONO:
			m_uChannelMask = AK_SPEAKER_SETUP_MONO;
			m_uOutNumChannels = AK::GetNumNonZeroBits(m_uChannelMask);
			break;
		case CHANNELMODE_STEREO:
			m_uChannelMask = AK_SPEAKER_SETUP_STEREO;
			m_uOutNumChannels = AK::GetNumNonZeroBits(m_uChannelMask);
			break;
		case CHANNELMODE_5_1:
			m_uChannelMask = AK_SPEAKER_SETUP_5POINT1;
			m_uOutNumChannels = AK::GetNumNonZeroBits(m_uChannelMask);
			break;
		case CHANNELMODE_7_1:
			m_uChannelMask = AK_SPEAKER_SETUP_7POINT1;
			m_uOutNumChannels = AK::GetNumNonZeroBits(m_uChannelMask);
			break;
	}


	//Allocate necessary structure for alsa
	//Can't use Ak malloc because snd_pcm_hw_params_t is opaque
	int returnVal=0;
	returnVal = snd_pcm_hw_params_malloc(&m_phwparams);
	if (returnVal < 0)
	{
		LOG_ERROR("Could not allocate ALSA HW parameters");
		return AK_InsufficientMemory;
	}

	returnVal = snd_pcm_sw_params_malloc(&m_pswparams);
	if (returnVal < 0)
	{
		LOG_ERROR("Could not allocate ALSA SW parameters");
		return AK_InsufficientMemory;
	}


	eResult = alsa_stream_connect(this);
	if (eResult != AK_Success)
	{
		LOG_ERROR("Could not init and connect to Alsa stream");
		return AK_Fail;
	}

	if (m_uOutNumChannels == 0)
	{
		LOG_ERROR("User specified channel settings for sink have no output channel. A sink cannot be created with this configuration");
		return AK_Fail;
	}

	io_rFormat.channelConfig.SetStandardOrAnonymous(m_uOutNumChannels, m_uChannelMask);
	io_rFormat.uBlockAlign = m_uOutNumChannels*sizeof(AkReal32);
	m_uSampleRate = m_pSinkPluginContext->GlobalContext()->GetSampleRate();
	io_rFormat.uSampleRate = m_uSampleRate;
	LOG_INFO("Alsa sink output channel mask: %d for %d channels", m_uChannelMask, m_uOutNumChannels);

	//assign Channel Mapping.
	for (unsigned int i = 0; i < m_uOutNumChannels; i++)
	{
		m_pChannelMap[i] = AK::StdChannelIndexToDisplayIndex(AK::ChannelOrdering_Standard, m_uChannelMask, i);
	}


	m_pfSinkOutBuffer = (int16_t *)AK_PLUGIN_ALLOC(in_pAllocator, m_uFrameSize*sizeof(int16_t)*m_uOutNumChannels);
	if (m_pfSinkOutBuffer == NULL)
	{
		LOG_ERROR("Could not allocate sink output audio buffer");
		return AK_InsufficientMemory;
	}
	AKPLATFORM::AkMemSet(m_pfSinkOutBuffer, 0, m_uFrameSize*sizeof(int16_t)*m_uOutNumChannels);

	LOG_INFO("Successful Alsa sink initialization");
	return AK_Success;
}

AKRESULT AkAlsaSink::Term( AK::IAkPluginMemAlloc * in_pAllocator )
{
	LOG_TRACE(__FUNCTION__);
	LOG_INFO("Alsa sink termination");


	//FixMe need to never access globalcontext in Term function.
	if(m_uLatencyErrorCnt < AKALSASINK_MAX_LATENCY_ERRORS)
	{
		LOG_INFO("OutputID: %i Alsa MAX Latency: %f s", m_uOutputID, (float)m_uLatencyMax/m_uSampleRate);
	}
	else
	{
		LOG_WARN("Unable to calculate Latency due to unavailable Latency API on device");
	}

	LOG_INFO("Number of underflow: %i ", m_uUnderflowCount);

	//Stop the thread
	if ( AKPLATFORM::AkIsValidThread( &m_Alsathread ) )
	{
		LOG_INFO("Stopping AlsaSinkAudioThread");
		m_bThreadRun = false;
		AKPLATFORM::AkWaitForSingleThread( &m_Alsathread );
		AKPLATFORM::AkCloseThread( &m_Alsathread );
	}

	if(m_bIsPrimary == true && (m_bMasterMode == false))
	{
    // TODO: Fix after updating to Wwise SDK 2017
		// AKRESULT eResult = UnRegisterMasterSink();
		// if(eResult != AK_Success)
		// {
		// 	LOG_WARN("Unable to register Master Sink to be use to synchronize with source");
		// }
	}

	if(m_phwparams)
	{
		snd_pcm_hw_params_free(m_phwparams);
		m_phwparams = NULL;
	}

	if(m_pswparams)
	{
		snd_pcm_sw_params_free(m_pswparams);
		m_pswparams = NULL;
	}


	//Drop PCM Stream
	if (m_pPcmHandle)
	{
		LOG_INFO("Stopping Alsa Stream, drop all remaining Audio data");
		snd_pcm_drop(m_pPcmHandle);
	}

	if(m_pPcm_callback_handle)
	{
		//Only assign to NULL don't need to free the pointer because it will be done by snd_pcm_close below.
		m_pPcm_callback_handle = NULL;
	}

	if(m_pPcmHandle != NULL)
	{
		snd_pcm_close (m_pPcmHandle);
	}
	m_pPcmHandle = NULL;

	if(m_pDeviceName)
	{
		AK_PLUGIN_FREE(in_pAllocator, m_pDeviceName);
		m_pDeviceName = NULL;
	}

	if (m_pfSinkOutBuffer)
	{
		AK_PLUGIN_FREE(in_pAllocator, m_pfSinkOutBuffer);
		m_pfSinkOutBuffer = NULL;
	}

	if (m_pChannelMap)
	{
		AK_PLUGIN_FREE(in_pAllocator, m_pChannelMap);
		m_pChannelMap = NULL;
	}

	m_bIsRunning = false;
	m_pfnGetLatencyCallback = NULL;
	m_uLatencyReportThresholdMsec = AKALSASINK_DEFAULT_LATENCY_THRESHOLD;
	AK_PLUGIN_DELETE( in_pAllocator, this );
	return AK_Success;
}

AKRESULT AkAlsaSink::Reset()
{
	LOG_TRACE(__FUNCTION__);

    if(m_pPcmHandle)
    {
        //Start stream
    	if(m_bIsRunning == false )
    	{
    		LOG_INFO("Starting ALSA sink");
    		if(m_bCustomThreadEnable)
    		{
    			if(m_bThreadRun == false && m_bIsPrimary == true)
    			{
					m_bThreadRun = true;
					AkThreadProperties AlsaThreadProp;

					AKPLATFORM::AkGetDefaultThreadProperties(AlsaThreadProp);
					AlsaThreadProp.nPriority = AK_THREAD_PRIORITY_ABOVE_NORMAL;
					AKPLATFORM::AkCreateThread(AlsaSinkAudioThread, (void*)this, AlsaThreadProp, &m_Alsathread, "AlsaSinkThread");
                    LOG_INFO("start main sink");
                }
    		}
    		else
    		{
    			if(m_bIsPrimary == true)
    			{
    				LOG_INFO("start Main sink");
    			}
    			else
    			{
    				LOG_INFO("start secondary sink");
    			}

            }

            m_bIsRunning = true;
        }
    }
    else
    {
      LOG_WARN("Starting ALSA device with invalid PCM Handle, device is not start");  
    }

	return AK_Success;
}

bool AkAlsaSink::IsStarved()
{
	LOG_TRACE(__FUNCTION__);
	LOG_DEBUG("m_uUnderflowCount: %i, devicename: %s", m_uUnderflowCount, m_pDeviceName);
	//Only report underflow when it is primary output	
	if(m_bIsPrimary == true)
	{
		return m_uUnderflowCount > 0;
	}

	return false;
}

void AkAlsaSink::ResetStarved()
{
	LOG_TRACE(__FUNCTION__);
	m_uUnderflowCount = 0;
}

AKRESULT AkAlsaSink::GetPluginInfo(
	AkPluginInfo & out_rPluginInfo	///< Reference to the plug-in information structure to be retrieved
	)
{
	LOG_TRACE(__FUNCTION__);
	out_rPluginInfo.eType			= AkPluginTypeSink;
	out_rPluginInfo.bIsInPlace		= false;
	out_rPluginInfo.uBuildVersion = AK_WWISESDK_VERSION_COMBINED;
	return AK_Success;
}

void AkAlsaSink::Consume(
	AkAudioBuffer *			in_pInputBuffer,		///< Input audio buffer data structure. Plugins should avoid processing data in-place.
	AkRamp					in_gain					///< Volume gain to apply to this input (prev corresponds to the beginning, next corresponds to the end of the buffer).
	)
{
	LOG_TRACE(__FUNCTION__);
	if ( in_pInputBuffer->uValidFrames > 0 )
	{
		// Interleave channels and apply volume
		const unsigned int uNumFrames = in_pInputBuffer->uValidFrames;
		const unsigned int uOutStride = m_uOutNumChannels;
		const AkReal32 fGainStart = in_gain.fPrev;
		const AkReal32 fDelta = (in_gain.fNext - in_gain.fPrev) / (AkReal32) uNumFrames;

		// Channels are in AK pipeline order (i.e. LFE at the end)
		for (unsigned int i = 0; i < m_uOutNumChannels; i++)
		{
			float * AK_RESTRICT pfSource = in_pInputBuffer->GetChannel(m_pChannelMap[i]);		
			int16_t * AK_RESTRICT pfDest = m_pfSinkOutBuffer + i; // Channel offset
			AkReal32 fGain = fGainStart;	
			for (unsigned int j = 0; j < uNumFrames; j++)
			{
				const float f = pfSource[j] * fGain;

				//Can be simd instruction to improve performance
				//To be optimized
				if (f >= 1.0f)
					*pfDest = 32767;
				else if (f <= -1.0f)
					*pfDest = -32768;
				else
					*pfDest = (int16_t) (f * 32767.0f);


				pfDest += uOutStride;
				fGain += fDelta;
			}
		}


		m_bDataReady = true;
	}
}

void AkAlsaSink::OnFrameEnd()
{
	LOG_TRACE(__FUNCTION__);
	
	// When no data has been produced (consume is not being called since SE is idle), produce silence to the RB
	if (!m_bDataReady)
	{
		AKPLATFORM::AkMemSet(m_pfSinkOutBuffer, 0, m_uFrameSize*sizeof(int16_t)*m_uOutNumChannels);
	}

	snd_pcm_sframes_t numberFrameAvailable;
	numberFrameAvailable = snd_pcm_avail(m_pPcmHandle);
	if(numberFrameAvailable < 0)
	{
		LOG_WARN("Error getting available frame.");
		numberFrameAvailable = 0;
	}

	if(numberFrameAvailable >= m_uFrameSize)
	{
		int error=0;
		// An error on this function is not critical
		// It could mean that the device does not support Latency reporting
		// Stop getting latency when the max latency errors count is reach
		if(m_uLatencyErrorCnt < AKALSASINK_MAX_LATENCY_ERRORS)
		{

			snd_pcm_sframes_t LatencyInFrame=0.0;

			error = snd_pcm_delay(m_pPcmHandle, &LatencyInFrame);
			if(error ==0)
			{
				LOG_DEBUG("Latency in number of frame is: %i", LatencyInFrame);

				//Callback to report Latency
				if(m_pfnGetLatencyCallback)
				{
					AkUInt64 currentLatencyUsec = (AkUInt64)(((float)LatencyInFrame/m_uSampleRate)*1000000);
					AkUInt64 currentLatencymsec = currentLatencyUsec/1000;
          AkUInt64 latency_diff = abs(static_cast<AkInt64>(m_LatencyReport-currentLatencymsec));
					if(latency_diff > m_uLatencyReportThresholdMsec)
					{
						m_pfnGetLatencyCallback( m_uOutputID, m_bIsPrimary, currentLatencyUsec);
						m_LatencyReport=currentLatencymsec;
					}
				}

				//Calculate max  latency
				if((AkUInt64)LatencyInFrame>m_uLatencyMax)
				{
					m_uLatencyMax = LatencyInFrame;
				}
			}
			else
			{

				m_uLatencyErrorCnt ++;
			}
		}

		int numberByteWritten=0;

		//this function accept number of frame, not number of bytes
		numberByteWritten = snd_pcm_writei(m_pPcmHandle,m_pfSinkOutBuffer, m_uFrameSize);
		if(numberByteWritten < 0)
		{
			LOG_ERROR("snd_pcm_writei error: %s", snd_strerror(numberByteWritten));
			error = alsa_error_recovery(this, m_pPcmHandle, numberByteWritten);
			if(error<0)
			{
				LOG_ERROR("Cannot recover from error: %s", snd_strerror(error));
			}
		}
		else
		{
			LOG_DEBUG("numberByteWritten: %d, devicename: %s", numberByteWritten, m_pDeviceName);
		}
	}
	else
	{
		LOG_WARN("not enought space in buffer, numberFrameAvailable: %d", numberFrameAvailable);
	}

	m_bDataReady = false;
}

AKRESULT AkAlsaSink::IsDataNeeded( AkUInt32 & out_uBuffersNeeded )
{
	// This will only get called on the "main" sink

	LOG_TRACE(__FUNCTION__);
	out_uBuffersNeeded = 0;
	int returnVal=0;

	// Query ALsa audio if it can accept more Wwise audio frames
	snd_pcm_sframes_t numberFrameAvailable;
	numberFrameAvailable = snd_pcm_avail_update(m_pPcmHandle);
	if(numberFrameAvailable < 0)
	{
		returnVal = alsa_error_recovery(this, m_pPcmHandle, numberFrameAvailable);
		if(returnVal<0)
		{
			LOG_ERROR("Cannot recover from error: %s", snd_strerror(returnVal));
			numberFrameAvailable = 0;
		}

		numberFrameAvailable = snd_pcm_avail_update(m_pPcmHandle);
		if(numberFrameAvailable<0)
		{
			LOG_ERROR("Multiple errors while get number of available frame");
			numberFrameAvailable = 0;
		}
	}

	if(m_bIsPrimary == true)
	{
		g_uBuffersNeeded = numberFrameAvailable / (m_uFrameSize);
		LOG_DEBUG("Set bufferNeeded: %d, devicename: %s", g_uBuffersNeeded, m_pDeviceName);
	}

	out_uBuffersNeeded = g_uBuffersNeeded;

	LOG_DEBUG("IsDataNeeded request numberFrameAvailable: %d, bufferNeeded: %d, devicename: %s", numberFrameAvailable, out_uBuffersNeeded, m_pDeviceName);
	return AK_Success; 
}

void AkAlsaSink::SetAlsaSinkCallbacks(AkOutputGetLatencyCallbackFunc in_pfnGetLatencyCallback, AkUInt32 in_uLatencyReportThresholdMsec)
{
	if(in_pfnGetLatencyCallback)
	{
		m_pfnGetLatencyCallback = in_pfnGetLatencyCallback;
		//Threshold value should be relative with blocksize
		if(in_uLatencyReportThresholdMsec == 0)
		{
			m_uLatencyReportThresholdMsec = AKALSASINK_DEFAULT_LATENCY_THRESHOLD;
		}
		else
		{
			m_uLatencyReportThresholdMsec = in_uLatencyReportThresholdMsec;
		}

	}
}

void SetAlsaSinkCallbacks(AkOutputGetLatencyCallbackFunc in_pfnGetLatencyCallback, AkUInt32 in_uLatencyReportThresholdMsec)
{
	AkAlsaSink::SetAlsaSinkCallbacks( in_pfnGetLatencyCallback,in_uLatencyReportThresholdMsec);
}
