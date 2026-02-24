#include "AkAlsaSink.h"
#include "Logging.h"
#include <alsa/pcm.h>

#include <AK/AkWwiseSDKVersion.h>
#include <AK/Tools/Common/AkPlatformFuncs.h>
#include <algorithm>
#include <sched.h>

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#define PA_DEFAULT_CHANNEL_MASK 3

using namespace AK::CustomHardware;

static AkUInt32 g_uBuffersNeeded = 0; //Global for Main output to set only

#define ALSA_SINK_SOCKET_PATH "/tmp/akalsasink_audio.sock"
#define SOCKET_AUDIO_CHUNK_BYTES (AkAlsaSink::SinkPluginTypes::kAkAlsaSinkBufferSize * sizeof(int16_t) * AkAlsaSink::SinkPluginTypes::kAkAlsaSinkChannelCount)

AK::IAkPlugin* CreateAkAlsaSink(AK::IAkPluginMemAlloc * in_pAllocator)
{
    AKASSERT(in_pAllocator != NULL);
    AK::IAkPlugin* plugin = AK_PLUGIN_NEW(in_pAllocator, AkAlsaSink());
    if (AkAlsaSink::PostCreateAlsaFunc != nullptr)
    {
        AkAlsaSink::PostCreateAlsaFunc(static_cast<AkAlsaSink*>(plugin));
    }
    return plugin;
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

// Static function for post CreateAkAlsaSink() work
std::function<void(AkAlsaSink*)> AkAlsaSink::PostCreateAlsaFunc = nullptr;

static int alsa_error_recovery(void* in_puserdata, snd_pcm_t* in_phandle, int in_err)
{
    AkAlsaSink* pAlsaSinkDevice = (AkAlsaSink*)in_puserdata;
    int returnVal = 0;

    if (in_err == -EPIPE) //Underrun
    {
        AK_LOG_WARN("%s: Underrun, try prepare: %s", __func__, snd_strerror(in_err));
        pAlsaSinkDevice->m_uUnderflowCount++;
        returnVal = snd_pcm_prepare(in_phandle);
        if (returnVal < 0)
        {
            AK_LOG_ERROR("%s: Can't recover from underrun, prepare failed: %s", __func__, snd_strerror(returnVal));
        }
        return returnVal;
    }
    else if (in_err == -ESTRPIPE) //Suspend
    {
        AK_LOG_ERROR("%s: Stream is suspended", __func__);
        while ((returnVal = snd_pcm_resume(in_phandle)) == -EAGAIN)
        {
            sleep(1); /* wait until the suspend flag is released */
        }
        if (returnVal < 0)
        {
            returnVal = snd_pcm_prepare(in_phandle);
            if (returnVal < 0)
            {
                AK_LOG_ERROR("%s: Can't recover from suspend, prepare failed: %s", __func__, snd_strerror(returnVal));
            }
        }
        return returnVal;
    }
    else
    {
        AK_LOG_ERROR("%s: unknown error %s (%d) --> try to recover", __func__, snd_strerror(in_err), in_err);
        returnVal = snd_pcm_prepare(in_phandle);
        if (returnVal < 0)
        {
            AK_LOG_ERROR("%s: Can't recover from unknown error, prepare failed: %s", __func__, snd_strerror(returnVal));
        }
    }
    return in_err;
}

static int write_frames_to_alsa(AkAlsaSink* pSink, const int16_t* pcm, snd_pcm_uframes_t frames)
{
    const int channels = AkAlsaSink::SinkPluginTypes::kAkAlsaSinkChannelCount;
    snd_pcm_uframes_t remaining = frames;
    const int16_t* ptr = pcm;

    while (remaining > 0)
    {
        snd_pcm_sframes_t written = snd_pcm_writei(pSink->m_pPcmHandle, ptr, remaining);
        if (written < 0)
        {
            AK_LOG_WARN("AlsaSink: snd_pcm_writei error %s, recovering", snd_strerror(written));
            int err = snd_pcm_recover(pSink->m_pPcmHandle, written, 1);
            if (err < 0)
            {
                AK_LOG_ERROR("AlsaSink: snd_pcm_recover failed: %s", snd_strerror(err));
                return -1;
            }
            continue;
        }
        ptr       += written * channels;
        remaining -= (snd_pcm_uframes_t)written;
    }
    return 0;
}

static void mix_into(int16_t* dst, const int16_t* src, size_t sampleCount)
{
    for (size_t i = 0; i < sampleCount; ++i)
    {
        int32_t mixed = (int32_t)dst[i] + (int32_t)src[i];
        if      (mixed >  32767) mixed =  32767;
        else if (mixed < -32768) mixed = -32768;
        dst[i] = (int16_t)mixed;
    }
}

static void socket_server_thread_func(AkAlsaSink* pSink)
{
    int serverFd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (serverFd < 0)
    {
        AK_LOG_ERROR("AlsaSink socket: failed to create socket");
        return;
    }

    {
        int flags = fcntl(serverFd, F_GETFL, 0);
        fcntl(serverFd, F_SETFL, flags | O_NONBLOCK);
    }

    unlink(ALSA_SINK_SOCKET_PATH);

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, ALSA_SINK_SOCKET_PATH, sizeof(addr.sun_path) - 1);

    if (bind(serverFd, (struct sockaddr*)&addr, sizeof(addr)) < 0)
    {
        AK_LOG_ERROR("AlsaSink socket: bind failed");
        close(serverFd);
        return;
    }

    if (listen(serverFd, 1) < 0)
    {
        AK_LOG_ERROR("AlsaSink socket: listen failed");
        close(serverFd);
        return;
    }

    AK_LOG_INFO("AlsaSink socket: listening on %s", ALSA_SINK_SOCKET_PATH);

    const size_t recvBufSize = 8192;
    std::vector<uint8_t> recvBuf(recvBufSize);

    int clientFd = -1;

    while (pSink->m_bSocketThreadRun)
    {
        if (clientFd < 0)
        {
            clientFd = accept(serverFd, nullptr, nullptr);
            if (clientFd >= 0)
            {
                AK_LOG_INFO("AlsaSink socket: client connected");

                {
                    std::lock_guard<std::mutex> lock(pSink->_socketRingMutex);
                    pSink->_socketRingHead = 0;
                    pSink->_socketRingTail = 0;
                    pSink->_socketRingUsed = 0;
                }

                pSink->m_bSocketClientConnected = true;

                int cflags = fcntl(clientFd, F_GETFL, 0);
                fcntl(clientFd, F_SETFL, cflags & ~O_NONBLOCK);
            }
            else
            {
                if (errno != EAGAIN && errno != EWOULDBLOCK)
                    AK_LOG_ERROR("AlsaSink socket: accept error %d", errno);
                usleep(5000);
                continue;
            }
        }

        ssize_t n = recv(clientFd, recvBuf.data(), recvBufSize, 0);

        if (n > 0)
        {
            std::lock_guard<std::mutex> lock(pSink->_socketRingMutex);

            const size_t cap = pSink->_socketRing.size();

            if (cap - pSink->_socketRingUsed < (size_t)n)
            {
                size_t toDrop = (size_t)n - (cap - pSink->_socketRingUsed);
                toDrop = (toDrop + 1) & ~(size_t)1;
                AK_LOG_WARN("AlsaSink socket: ring full, dropping %zu bytes", toDrop);
                pSink->_socketRingTail = (pSink->_socketRingTail + toDrop) % cap;
                pSink->_socketRingUsed -= toDrop;
            }

            size_t part1 = std::min((size_t)n, cap - pSink->_socketRingHead);
            memcpy(pSink->_socketRing.data() + pSink->_socketRingHead, recvBuf.data(),         part1);
            memcpy(pSink->_socketRing.data(),                          recvBuf.data() + part1, (size_t)n - part1);
            pSink->_socketRingHead = (pSink->_socketRingHead + (size_t)n) % cap;
            pSink->_socketRingUsed += (size_t)n;
        }
        else if (n == 0)
        {
            AK_LOG_INFO("AlsaSink socket: client EOF");
            goto client_gone;
        }
        else
        {
            if (errno == EINTR)
                continue;
            AK_LOG_ERROR("AlsaSink socket: recv error errno=%d (%s)", errno, strerror(errno));
            goto client_gone;
        }

        continue;

client_gone:
        close(clientFd);
        clientFd = -1;
        pSink->m_bSocketClientConnected = false;
        AK_LOG_INFO("AlsaSink socket: client disconnected");
    }

    if (clientFd >= 0)
        close(clientFd);

    close(serverFd);
    unlink(ALSA_SINK_SOCKET_PATH);
}

// ---------------------- Alsa Helper function -----------------------------------------------
// Alsa audio Thread, handling writing of the alsa stream and pushing data to the audio buffer
AK_DECLARE_THREAD_ROUTINE(AlsaSinkAudioThread)
{
    AkAlsaSink* pAlsaSinkDevice = (AkAlsaSink*)lpParameter;

    AkThreadProperties threadProp;
    threadProp.dwAffinityMask = pAlsaSinkDevice->m_uCpuMask;
    int error = AK_THREAD_INIT_CODE(threadProp);
    if (error != 0)
    {
        AK_LOG_ERROR("SetAffinityMask Error %d", error);
    }

    AK_INSTRUMENT_THREAD_START("AlsaSinkAudioThread");

    const size_t channels      = AkAlsaSink::SinkPluginTypes::kAkAlsaSinkChannelCount;
    const size_t periodFrames  = pAlsaSinkDevice->m_uFrameSize;
    const size_t periodSamples = periodFrames * channels;
    const size_t periodBytes   = periodSamples * sizeof(int16_t);

    std::vector<int16_t> wwiseBuf(periodSamples, 0);
    std::vector<int16_t> socketBuf(periodSamples, 0);
    std::vector<int16_t> mixBuf(periodSamples, 0);

    while (pAlsaSinkDevice->m_bThreadRun)
    {
        snd_pcm_sframes_t frameAvail;

        frameAvail = snd_pcm_avail_update(pAlsaSinkDevice->m_pPcmHandle);
        AK_LOG_DEBUG("frameAvail : %i", frameAvail);
        if (frameAvail < 0)
        {
            AK_LOG_INFO("%s: snd_pcm_avail_update() failed -> try to recover", __func__);
            int returnVal = alsa_error_recovery(pAlsaSinkDevice, pAlsaSinkDevice->m_pPcmHandle, frameAvail);
            if (returnVal < 0)
            {
                AK_LOG_ERROR("Alsa unable to recover from error");
                break;
            }
            frameAvail = snd_pcm_avail(pAlsaSinkDevice->m_pPcmHandle);
            if (frameAvail < 0)
            {
                AK_LOG_WARN("Alsa stream multiple underrun, problem detected");
                continue;
            }
        }

        if (frameAvail < (snd_pcm_sframes_t)periodFrames)
        {
            usleep(5000);
            continue;
        }

        {
            std::lock_guard<std::mutex> lock(pAlsaSinkDevice->_sinkPluginBufferMutex);

            if (pAlsaSinkDevice->_sinkPluginBuffer.size() > 0)
            {
                const auto& front = pAlsaSinkDevice->_sinkPluginBuffer.front();
                memcpy(wwiseBuf.data(), front, periodBytes);
                pAlsaSinkDevice->_sinkPluginBuffer.pop_front();
            }
            else
            {
                std::fill(wwiseBuf.begin(), wwiseBuf.end(), (int16_t)0);
            }
        }

        //Generate SignalThread if we've used up any frames we have
        if (pAlsaSinkDevice->_sinkPluginBuffer.size() < pAlsaSinkDevice->_sinkPluginBuffer.capacity())
        {
            pAlsaSinkDevice->m_pSinkPluginContext->SignalAudioThread();
            AK_LOG_DEBUG("Generate signalAudioThread");
        }

        {
            std::lock_guard<std::mutex> lock(pAlsaSinkDevice->_socketRingMutex);

            const size_t cap  = pAlsaSinkDevice->_socketRing.size();
            const size_t have = pAlsaSinkDevice->_socketRingUsed;

            if (have >= periodBytes)
            {
                uint8_t* dst = reinterpret_cast<uint8_t*>(socketBuf.data());
                size_t tail  = pAlsaSinkDevice->_socketRingTail;
                size_t part1 = std::min(periodBytes, cap - tail);
                memcpy(dst,         pAlsaSinkDevice->_socketRing.data() + tail, part1);
                memcpy(dst + part1, pAlsaSinkDevice->_socketRing.data(),        periodBytes - part1);
                pAlsaSinkDevice->_socketRingTail = (tail + periodBytes) % cap;
                pAlsaSinkDevice->_socketRingUsed -= periodBytes;
            }
            else if (have > 0)
            {
                std::fill(socketBuf.begin(), socketBuf.end(), (int16_t)0);
                uint8_t* dst = reinterpret_cast<uint8_t*>(socketBuf.data());
                size_t tail  = pAlsaSinkDevice->_socketRingTail;
                size_t part1 = std::min(have, cap - tail);
                memcpy(dst,         pAlsaSinkDevice->_socketRing.data() + tail, part1);
                memcpy(dst + part1, pAlsaSinkDevice->_socketRing.data(),        have - part1);
                pAlsaSinkDevice->_socketRingTail = (tail + have) % cap;
                pAlsaSinkDevice->_socketRingUsed = 0;
            }
            else
            {
                std::fill(socketBuf.begin(), socketBuf.end(), (int16_t)0);
            }
        }

        memcpy(mixBuf.data(), wwiseBuf.data(), periodBytes);
        mix_into(mixBuf.data(), socketBuf.data(), periodSamples);

        if (write_frames_to_alsa(pAlsaSinkDevice, mixBuf.data(), periodFrames) < 0)
        {
            AK_LOG_ERROR("AlsaSink: unrecoverable ALSA write error, stopping thread");
            break;
        }
    }

    pAlsaSinkDevice->m_bThreadRun = false;
    AkExitThread(AK_RETURN_THREAD_OK);
}

// Initializes the ALSA
static AKRESULT alsa_stream_connect(void* in_puserdata)
{
    AkAlsaSink* pAlsaSinkDevice = (AkAlsaSink*)in_puserdata;
    int retValue;

    // Handle for the PCM device
    snd_pcm_t* pPcmHandle;

    //
    // Name of the PCM device, like plughw:0,0, hw:0,0, default, pulse, etc
    //
    // The first number is the number of the soundcard,
    // the second number is the number of the device.
    char* pDeviceName;
    pDeviceName = pAlsaSinkDevice->m_pDeviceName;

    AK_LOG_INFO("%s: Opening PCM device '%s'", __func__, pDeviceName);

    // Open PCM. The last parameter of this function is the mode.
    // If this is set to 0, the standard mode is used. Possible
    // other values are SND_PCM_NONBLOCK and SND_PCM_ASYNC.
    // If SND_PCM_NONBLOCK is used, read / write access to the
    // PCM device will return immediately. If SND_PCM_ASYNC is
    // specified, SIGIO will be emitted whenever a period has
    // been completely processed by the soundcard.
    retValue = snd_pcm_open(&pPcmHandle, pDeviceName, SND_PCM_STREAM_PLAYBACK, SND_PCM_NONBLOCK);
    if (retValue < 0)
    {
        AK_LOG_ERROR("%s: Error opening PCM device '%s': %s (%d)", __func__, pDeviceName, snd_strerror(retValue), retValue);
        return AK_Fail;
    }

    // Init hwparams with full configuration space
    retValue = snd_pcm_hw_params_any(pPcmHandle, pAlsaSinkDevice->m_phwparams);
    if (retValue < 0)
    {
        AK_LOG_ERROR("%s: Error configuring PCM device: %s (%d)", __func__, snd_strerror(retValue), retValue);
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
        AK_LOG_ERROR("%s: Error setting access: %s (%d)", __func__, snd_strerror(retValue), retValue);
        return AK_Fail;
    }

    // Set sample format
    retValue = snd_pcm_hw_params_set_format(pPcmHandle, pAlsaSinkDevice->m_phwparams, SND_PCM_FORMAT_S16_LE);
    if (retValue < 0)
    {
        AK_LOG_ERROR("int16 not support by hardware");
        return AK_Fail;
    }

    // Set sample rate. If the exact rate is not supported
    // by the hardware, use nearest possible rate.
    // Use near to be able to see hardware closest supported rate even if we only accept the exact rate

    AK_LOG_INFO("%s: device Sample Rates: %d", __func__, uRate);

    uSampleRate = uRate;
    retValue = snd_pcm_hw_params_set_rate_near(pPcmHandle, pAlsaSinkDevice->m_phwparams, &uSampleRate, 0);
    if (retValue < 0)
    {
        AK_LOG_ERROR("Error setting rate");
        return AK_Fail;
    }

    if (uRate != uSampleRate)
    {
        AK_LOG_ERROR("The rate %d Hz is not supported by your hardware.\n ==> Please use %d Hz instead.", uRate, uSampleRate);
        return AK_Fail;
    }
    AK_LOG_INFO("%s: sample rate: %d", __func__, uSampleRate);

    snd_pcm_chmap_query_t** ppChMaps;
    snd_pcm_chmap_t* FoundChMap;

    ppChMaps = snd_pcm_query_chmaps(pPcmHandle);
    if (ppChMaps == NULL)
    {
        AK_LOG_WARN("Unable to detect number of audio channels supported by the Device. Mono mode is selected by default");
        //Channel mapping is not set because no api to set channel mapping available.
        //Assuming channel mapping in mono mode will match Wwise mono channel mapping
        pAlsaSinkDevice->m_uOutNumChannels = AKALSASINK_DEFAULT_NBCHAN;
        pAlsaSinkDevice->m_uChannelMask = AK::ChannelMaskFromNumChannels(pAlsaSinkDevice->m_uOutNumChannels);
    }
    else
    {
        int i = 0;

        FoundChMap = &ppChMaps[i]->map;

        //Auto-detect mode
        if (pAlsaSinkDevice->m_uOutNumChannels == 0)
        {
            //In Auto-detect mode we select the first channel mapping available.
            pAlsaSinkDevice->m_uOutNumChannels = FoundChMap->channels;
            pAlsaSinkDevice->m_uChannelMask = AK::ChannelMaskFromNumChannels(FoundChMap->channels);
        }
        else //Custom Mode
        {
            //Search for request nb of channels and report error if no config match with number of channel.
            bool bChannelMapConfigFound = false;

            snd_pcm_chmap_query_t* pChMap = ppChMaps[i];

            //ppChMaps is always terminated with NULL
            while (pChMap != NULL)
            {
                if (pChMap->map.channels == pAlsaSinkDevice->m_uOutNumChannels)
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

            if (bChannelMapConfigFound == false)
            {
                AK_LOG_ERROR("Audio device does not support %i audio channels", pAlsaSinkDevice->m_uOutNumChannels);
                return AK_Fail;
            }
        }

        if (pAlsaSinkDevice->m_uOutNumChannels > AKALSASINK_MAXCHANNEL)
        {
            AK_LOG_ERROR("Alsa Sink supported a maximum off 16 channels, requested %i channels", pAlsaSinkDevice->m_uOutNumChannels);
            return AK_Fail;
        }
    }

    // Set number of channels
    retValue = snd_pcm_hw_params_set_channels(pPcmHandle, pAlsaSinkDevice->m_phwparams, pAlsaSinkDevice->m_uOutNumChannels);
    if (retValue < 0)
    {
        AK_LOG_ERROR("Unable to set request number of channels :%i", pAlsaSinkDevice->m_uOutNumChannels);
        return AK_Fail;
    }

    unsigned int period_min;
    unsigned int period_max;
    snd_pcm_uframes_t period_size_min;
    snd_pcm_uframes_t period_size_max;
    snd_pcm_uframes_t buffer_size_min;
    snd_pcm_uframes_t buffer_size_max;

    //Get usefull HW parameters
    retValue = snd_pcm_hw_params_get_periods_min(pAlsaSinkDevice->m_phwparams, &period_min, 0);
    if (retValue < 0)
    {
        AK_LOG_WARN("Unable to get the minimum number of periods");
        period_min = 0;
    }
    AK_LOG_INFO("ALSA: minimum number of periods: %d", period_min);

    retValue = snd_pcm_hw_params_get_periods_max(pAlsaSinkDevice->m_phwparams, &period_max, 0);
    if (retValue < 0)
    {
        AK_LOG_WARN("Error getting the maximum number of periods");
        period_max = 0;
    }
    AK_LOG_INFO("ALSA: maximum number of periods: %d", period_max);

    retValue = snd_pcm_hw_params_get_period_size_min(pAlsaSinkDevice->m_phwparams, &period_size_min, 0);
    if (retValue < 0)
    {
        AK_LOG_WARN("Error getting the minimum periods size");
        period_size_min = 0;
    }
    AK_LOG_INFO("ALSA: minimum period size: %d", period_size_min);

    retValue = snd_pcm_hw_params_get_period_size_max(pAlsaSinkDevice->m_phwparams, &period_size_max, 0);
    if (retValue < 0)
    {
        AK_LOG_WARN("Error getting the maximum periods size");
        period_size_max = 0;
    }
    AK_LOG_INFO("ALSA: maximum period size: %d", period_size_max);

    retValue = snd_pcm_hw_params_get_buffer_size_min(pAlsaSinkDevice->m_phwparams, &buffer_size_min);
    if (retValue < 0)
    {
        AK_LOG_WARN("Error getting the minimum buffer size");
        buffer_size_min = 0;
    }
    AK_LOG_INFO("ALSA: minimum buffer size: %d", buffer_size_min);

    retValue = snd_pcm_hw_params_get_buffer_size_max(pAlsaSinkDevice->m_phwparams, &buffer_size_max);
    if (retValue < 0)
    {
        AK_LOG_WARN("Error getting the maximum buffer size");
        buffer_size_max = 0;
    }
    AK_LOG_INFO("ALSA: maximum buffer size: %d", buffer_size_max);

    //buffer_size = periodsize * periods
    //Latency important parameters
    //The latency is calculated as follow:
    //latency = periodsize*period / (rate * bytes_per_frame)
    //Typical example is as follow:
    //period=2; periodsize=8192 bytes; rate=48000Hz; 16 bits stereo data (bytes_per_frame=4bytes)
    //latency = 8192*2/(48000*4) = 85.3 ms of latency
    // TODO: Update var names - Wwise & ALSA terms are different
    snd_pcm_uframes_t buffer_size = pAlsaSinkDevice->m_uFrameSize * pAlsaSinkDevice->m_uNumBuffers;

    //Set the buffer size
    AK_LOG_INFO("ALSA: trying to set buffer size: %d", buffer_size);
    retValue = snd_pcm_hw_params_set_buffer_size_near(pPcmHandle, pAlsaSinkDevice->m_phwparams, &buffer_size);
    if (retValue < 0)
    {
        AK_LOG_ERROR("Error setting buffer size");
        return AK_Fail;
    }
    AK_LOG_INFO("ALSA: set to effective buffer size: %d", buffer_size);

    unsigned int period = buffer_size / pAlsaSinkDevice->m_uFrameSize;

    //Periods
    retValue = snd_pcm_hw_params_test_periods(pPcmHandle, pAlsaSinkDevice->m_phwparams, period, 0);
    if (retValue < 0)
    {
        AK_LOG_WARN("Request %i period not supported by hardware, will set to the nearest possible value, the latency will be affected", period);
    }

    retValue = snd_pcm_hw_params_set_periods_near(pPcmHandle, pAlsaSinkDevice->m_phwparams, &period, 0);
    if (retValue < 0)
    {
        AK_LOG_ERROR("Error setting period size");
        return AK_Fail;
    }

    AK_LOG_INFO("ALSA buffer size (frames) is set to : %i", buffer_size);
    AK_LOG_INFO("ALSA period is set to : %i", period);

    // Apply HW parameter and prepare device
    retValue = snd_pcm_hw_params(pPcmHandle, pAlsaSinkDevice->m_phwparams);
    if (retValue < 0)
    {
        AK_LOG_ERROR("Error setting PCM HW PARAMS");
        return AK_Fail;
    }

    retValue = snd_pcm_hw_params_get_period_size_min(pAlsaSinkDevice->m_phwparams, &period_size_min, 0);
    if (retValue < 0)
    {
        AK_LOG_WARN("Error getting the minimum periods size");
    }

    retValue = snd_pcm_hw_params_get_period_size_max(pAlsaSinkDevice->m_phwparams, &period_size_max, 0);
    if (retValue < 0)
    {
        AK_LOG_WARN("Error getting the minimum periods size");
    }

    //Assign pcmHandle
    pAlsaSinkDevice->m_pPcmHandle = pPcmHandle;

    if (ppChMaps != NULL)
    {
        //Wwise standard mapping L-R-C-LFE-RL-RR-RC-SL-SR-HL-HR-HC-HRL-HRR-HRC-T
        //In order to support up to 16 channels
        //Always open the stream with Wwise Standard channel mapping, since it does not change based on channel number
        //The we use m_pChannelMap to remap into run time mapping.
        //Since the remapping does not cost an extra memcpy this solution should keep the same performance.
        //Another solution will be to directly map into Wwise runtime mapping but this has a lot of case to handle.
        if (pAlsaSinkDevice->m_uOutNumChannels > 1) //Channel map only for stereo and up.
        {
            for (unsigned int y = 0; y < pAlsaSinkDevice->m_uOutNumChannels; y++)
            {
                switch (y)
                {
                    case 0:  FoundChMap->pos[y] = SND_CHMAP_FL;  break;
                    case 1:  FoundChMap->pos[y] = SND_CHMAP_FR;  break;
                    case 2:  FoundChMap->pos[y] = SND_CHMAP_FC;  break;
                    case 3:  FoundChMap->pos[y] = SND_CHMAP_LFE; break;
                    case 4:  FoundChMap->pos[y] = SND_CHMAP_RL;  break;
                    case 5:  FoundChMap->pos[y] = SND_CHMAP_RR;  break;
                    case 6:  FoundChMap->pos[y] = SND_CHMAP_RC;  break;
                    case 7:  FoundChMap->pos[y] = SND_CHMAP_SL;  break;
                    case 8:  FoundChMap->pos[y] = SND_CHMAP_SR;  break;
                    case 9:  FoundChMap->pos[y] = SND_CHMAP_TFL; break;
                    case 10: FoundChMap->pos[y] = SND_CHMAP_TFR; break;
                    case 11: FoundChMap->pos[y] = SND_CHMAP_TFC; break;
                    case 12: FoundChMap->pos[y] = SND_CHMAP_TRL; break;
                    case 13: FoundChMap->pos[y] = SND_CHMAP_TRR; break;
                    case 14: FoundChMap->pos[y] = SND_CHMAP_TRC; break;
                    case 15: FoundChMap->pos[y] = SND_CHMAP_TC;  break;
                }
            }

            retValue = snd_pcm_set_chmap(pPcmHandle, FoundChMap);
            if (retValue < 0)
            {
                AK_LOG_WARN("Unable to set channel mapping, your channel mapping is not set. Please check your device name.");
            }
        }

        //Free channel maps
        snd_pcm_free_chmaps(ppChMaps);
    }

    //SW params setting
    retValue = snd_pcm_sw_params_current(pPcmHandle, pAlsaSinkDevice->m_pswparams);
    if (retValue < 0)
    {
        AK_LOG_ERROR("Error gettting the current sw params");
        return AK_Fail;
    }

    //Set start value, minimum is 2 frames
    retValue = snd_pcm_sw_params_set_start_threshold(pPcmHandle, pAlsaSinkDevice->m_pswparams, pAlsaSinkDevice->m_uFrameSize * AKALSASINK_MIN_NB_OUTBUFFER);
    if (retValue < 0)
    {
        AK_LOG_ERROR("Error setting the start threashold");
        return AK_Fail;
    }

    retValue = snd_pcm_sw_params_set_avail_min(pPcmHandle, pAlsaSinkDevice->m_pswparams, pAlsaSinkDevice->m_uFrameSize);
    if (retValue < 0)
    {
        AK_LOG_ERROR("Error setting avail minimum");
        return AK_Fail;
    }

    retValue = snd_pcm_sw_params(pPcmHandle, pAlsaSinkDevice->m_pswparams);
    if (retValue < 0)
    {
        AK_LOG_ERROR("Error writing sw params");
        return AK_Fail;
    }

    //Init stream
    retValue = snd_pcm_prepare(pPcmHandle);
    if (retValue < 0)
    {
        AK_LOG_ERROR("cannot prepare audio interface for use");
        return AK_Fail;
    }

    return AK_Success;
}

AkAlsaSink::AkAlsaSink()
    : m_pSinkPluginContext(NULL)
    , m_uOutNumChannels(0)
    , m_pChannelMap(NULL)
    , m_uNumBuffers(SinkPluginTypes::kAkAlsaSinkBufferCount)
    , m_uChannelMask(0)
    , m_pDeviceName(NULL)
    , m_bDeviceFound(false)
    , m_uFrameSize(0)
    , m_uLatencyMax(0)
    , m_uLatencyErrorCnt(0)
    , m_uSampleRate(48000)
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
    , m_Alsathread(AK_NULL_THREAD)
    , m_bThreadRun(false)
    , m_bCustomThreadEnable(false)
    , m_pPcm_callback_handle(NULL)
    , m_uCpuMask(0)
    , m_bSocketThreadRun(false)
    , m_bSocketClientConnected(false)
    , _socketBufferReady(false)
    , _socketRingHead(0)
    , _socketRingTail(0)
    , _socketRingUsed(0)
{
}

AkAlsaSink::~AkAlsaSink()
{
}

AKRESULT AkAlsaSink::Init(
    AK::IAkPluginMemAlloc*      in_pAllocator,
    AK::IAkSinkPluginContext*   in_pSinkPluginContext,
    AK::IAkPluginParam*         in_pParams,
    AkAudioFormat&              io_rFormat
    )
{
    // Uncomment for debug logging
    Logging::Instance().SetLogLevel(LogLevelInfo);

    AK_LOG_TRACE(__FUNCTION__);
    AK_LOG_INFO("Alsa sink initialization");

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
        AK_LOG_ERROR("Could not retrieve output device type and ID");
        return AK_NotImplemented;
    }

    //Register MasterSink if this sink is the mastersink and used as slave
    if (m_bIsPrimary == true && (params.bMasterMode == false))
    {
    // TODO: Fix after updating to Wwise SDK 2017
        // eResult = RegisterMasterSink(m_pSinkPluginContext);
        // if(eResult != AK_Success)
        // {
        //     AK_LOG_WARN("Unable to register Master Sink to be use to synchronize with source");
        // }
    }

    m_bMasterMode = params.bMasterMode;

    AK_LOG_INFO("Alsa full device name: %s", params.szDeviceName);
    AK_LOG_INFO("Primary sink: %d Master: %d", m_bIsPrimary, m_bMasterMode);

    // Allocate device array name storage
    m_pDeviceName = (char*)AK_PLUGIN_ALLOC(in_pAllocator, sizeof(char) * AKALSASINK_MAXDEVICENAME_LENGTH);
    if (m_pDeviceName == NULL)
    {
        AK_LOG_ERROR("Could not allocate Alsa output sink device name");
        return AK_InsufficientMemory;
    }

    strcpy(m_pDeviceName, params.szDeviceName);
    if (m_pDeviceName == NULL)
    {
        AK_LOG_ERROR("Device name equal to NULL, fatal error should never happened");
        return AK_InvalidParameter;
    }

    m_bDeviceFound = false;

    /////////////////////////////////////////////////////
    // HACK
    // I'M TAKING CONTROL AWAY FROM PLUGIN UI SETTINGS AND SOUND DESIGNERS
    // m_uNumBuffers = params.uNumBuffers; // Uses init value
    /////////////////////////////////////////////////////

    if (m_uNumBuffers < AKALSASINK_MIN_NB_OUTBUFFER)
    {
        AK_LOG_ERROR("Minimum number of output buffer is 2, current number of buffer is: %i", m_uNumBuffers);
        return AK_Fail;
    }

    // Set Engine settings
    m_uSampleRate = m_pSinkPluginContext->GlobalContext()->GetSampleRate();
    m_uFrameSize  = m_pSinkPluginContext->GlobalContext()->GetMaxBufferLength();
    AK_LOG_INFO("Sound engine frame size: %d", m_uFrameSize);
    AKASSERT(m_uFrameSize == kExpectedPeriodSize_frames);

    m_pChannelMap = (unsigned int*)AK_PLUGIN_ALLOC(in_pAllocator, sizeof(unsigned int) * AKALSASINK_MAXCHANNEL);
    if (m_pChannelMap == NULL)
    {
        AK_LOG_ERROR("Could not allocate channelMap array");
        return AK_InsufficientMemory;
    }

    // Detect/force output configuration for the master bus
    switch (params.eChannelMode)
    {
        default:
        case CHANNELMODE_AUTODETECT: // Auto
            m_uOutNumChannels = 0;
            m_uChannelMask    = 0;
            break;
        case CHANNELMODE_ANONYMOUS: // Channels have no precise meaning
            m_uChannelMask    = 0;
            m_uOutNumChannels = params.uChannelInfo;  // Channel information parameter represents desired number of channels in anonymous mode
            if (m_uOutNumChannels <= 2)
            {
                m_uChannelMask = AK::ChannelMaskFromNumChannels(m_uOutNumChannels);
            }
            break;
        case CHANNELMODE_CUSTOM: // Use explicitly specified channel mask
            m_uChannelMask    = params.uChannelInfo; // Channel mask explicitely provided by channel info parameter
            m_uOutNumChannels = AK::GetNumNonZeroBits(m_uChannelMask);
            break;
        case CHANNELMODE_MONO:
            m_uChannelMask    = AK_SPEAKER_SETUP_MONO;
            m_uOutNumChannels = AK::GetNumNonZeroBits(m_uChannelMask);
            break;
        case CHANNELMODE_STEREO:
            m_uChannelMask    = AK_SPEAKER_SETUP_STEREO;
            m_uOutNumChannels = AK::GetNumNonZeroBits(m_uChannelMask);
            break;
        case CHANNELMODE_5_1:
            m_uChannelMask    = AK_SPEAKER_SETUP_5POINT1;
            m_uOutNumChannels = AK::GetNumNonZeroBits(m_uChannelMask);
            break;
        case CHANNELMODE_7_1:
            m_uChannelMask    = AK_SPEAKER_SETUP_7POINT1;
            m_uOutNumChannels = AK::GetNumNonZeroBits(m_uChannelMask);
            break;
    }

    //Allocate necessary structure for alsa
    //Can't use Ak malloc because snd_pcm_hw_params_t is opaque
    int returnVal = 0;
    returnVal = snd_pcm_hw_params_malloc(&m_phwparams);
    if (returnVal < 0)
    {
        AK_LOG_ERROR("Could not allocate ALSA HW parameters");
        return AK_InsufficientMemory;
    }

    returnVal = snd_pcm_sw_params_malloc(&m_pswparams);
    if (returnVal < 0)
    {
        AK_LOG_ERROR("Could not allocate ALSA SW parameters");
        return AK_InsufficientMemory;
    }

    eResult = alsa_stream_connect(this);
    if (eResult != AK_Success)
    {
        AK_LOG_ERROR("Could not init and connect to Alsa stream");
        return AK_Fail;
    }

    if (m_uOutNumChannels == 0)
    {
        AK_LOG_ERROR("User specified channel settings for sink have no output channel. A sink cannot be created with this configuration");
        return AK_Fail;
    }

    io_rFormat.channelConfig.SetStandardOrAnonymous(m_uOutNumChannels, m_uChannelMask);
    io_rFormat.uBlockAlign  = m_uOutNumChannels * sizeof(AkReal32);
    io_rFormat.uSampleRate  = m_uSampleRate;
    AK_LOG_INFO("Alsa sink output channel mask: %d for %d channels", m_uChannelMask, m_uOutNumChannels);

    //assign Channel Mapping.
    for (unsigned int i = 0; i < m_uOutNumChannels; i++)
    {
        m_pChannelMap[i] = AK::StdChannelIndexToDisplayIndex(AK::ChannelOrdering_Standard, m_uChannelMask, i);
    }

    // Compute the max speaker 'latency'. Divide sample rate (which is in Hz) by 1000
    // since we want a value in milliseconds (and want to avoid integer rounding errors)
    _speakerLatency_ms = m_uNumBuffers * m_uFrameSize / (m_uSampleRate / 1000);
    AK_LOG_INFO("Speaker latency computed to be %u milliseconds", (uint32_t)_speakerLatency_ms);

    const size_t channels    = SinkPluginTypes::kAkAlsaSinkChannelCount;
    const size_t periodBytes = (size_t)m_uFrameSize * channels * sizeof(int16_t);
    const size_t ringCapacity = periodBytes * 64;
    _socketRing.assign(ringCapacity, 0);
    _socketRingHead = _socketRingTail = _socketRingUsed = 0;

    _socketAudioBuffer.resize(SOCKET_AUDIO_CHUNK_BYTES, 0);
    m_bSocketThreadRun = true;
    m_socketThread = std::thread(socket_server_thread_func, this);
    AK_LOG_INFO("AlsaSink socket server thread started");

    AK_LOG_INFO("Successful Alsa sink initialization");
    return AK_Success;
}

AKRESULT AkAlsaSink::Term(AK::IAkPluginMemAlloc* in_pAllocator)
{
    AK_LOG_TRACE(__FUNCTION__);
    AK_LOG_INFO("Alsa sink termination");

    //FixMe need to never access globalcontext in Term function.
    if (m_uLatencyErrorCnt < AKALSASINK_MAX_LATENCY_ERRORS)
    {
        AK_LOG_INFO("OutputID: %i Alsa MAX Latency: %f s", m_uOutputID, (float)m_uLatencyMax / m_uSampleRate);
    }
    else
    {
        AK_LOG_WARN("Unable to calculate Latency due to unavailable Latency API on device");
    }

    AK_LOG_INFO("Number of underflow: %i ", m_uUnderflowCount);

    //Stop the thread
    if (AKPLATFORM::AkIsValidThread(&m_Alsathread))
    {
        AK_LOG_INFO("Stopping AlsaSinkAudioThread");
        m_bThreadRun = false;
        AKPLATFORM::AkWaitForSingleThread(&m_Alsathread);
        AKPLATFORM::AkCloseThread(&m_Alsathread);
    }

    if (m_bIsPrimary == true && (m_bMasterMode == false))
    {
    // TODO: Fix after updating to Wwise SDK 2017
        // AKRESULT eResult = UnRegisterMasterSink();
        // if(eResult != AK_Success)
        // {
        //     AK_LOG_WARN("Unable to register Master Sink to be use to synchronize with source");
        // }
    }

    // Stop socket thread
    m_bSocketThreadRun = false;
    m_bSocketClientConnected = false;
    if (m_socketThread.joinable())
        m_socketThread.join();

    if (m_phwparams)
    {
        snd_pcm_hw_params_free(m_phwparams);
        m_phwparams = NULL;
    }

    if (m_pswparams)
    {
        snd_pcm_sw_params_free(m_pswparams);
        m_pswparams = NULL;
    }

    //Drop PCM Stream
    if (m_pPcmHandle)
    {
        AK_LOG_INFO("Stopping Alsa Stream, drop all remaining Audio data");
        snd_pcm_drop(m_pPcmHandle);
    }

    if (m_pPcm_callback_handle)
    {
        //Only assign to NULL don't need to free the pointer because it will be done by snd_pcm_close below.
        m_pPcm_callback_handle = NULL;
    }

    if (m_pPcmHandle != NULL)
    {
        snd_pcm_close(m_pPcmHandle);
    }
    m_pPcmHandle = NULL;

    if (m_pDeviceName)
    {
        AK_PLUGIN_FREE(in_pAllocator, m_pDeviceName);
        m_pDeviceName = NULL;
    }

    if (m_pChannelMap)
    {
        AK_PLUGIN_FREE(in_pAllocator, m_pChannelMap);
        m_pChannelMap = NULL;
    }

    m_pfnGetLatencyCallback       = NULL;
    m_uLatencyReportThresholdMsec = AKALSASINK_DEFAULT_LATENCY_THRESHOLD;
    AK_PLUGIN_DELETE(in_pAllocator, this);
    return AK_Success;
}

AKRESULT AkAlsaSink::Reset()
{
    AK_LOG_TRACE(__FUNCTION__);

    if (m_pPcmHandle)
    {
        AK_LOG_INFO("Starting ALSA sink");
        if (m_bThreadRun == false)
        {
            m_bThreadRun = true;
            AkThreadProperties AlsaThreadProp;
            AKPLATFORM::AkGetDefaultThreadProperties(AlsaThreadProp);
            AlsaThreadProp.nPriority = AK_THREAD_PRIORITY_ABOVE_NORMAL;
            AKPLATFORM::AkCreateThread(AlsaSinkAudioThread, (void*)this, AlsaThreadProp, &m_Alsathread, "AlsaSinkThread");
            AK_LOG_INFO("start main sink thread");
        }
    }
    else
    {
        AK_LOG_WARN("Starting ALSA device with invalid PCM Handle, device is not start");
    }

    return AK_Success;
}

bool AkAlsaSink::IsStarved()
{
    AK_LOG_TRACE(__FUNCTION__);
    AK_LOG_DEBUG("m_uUnderflowCount: %i, devicename: %s", m_uUnderflowCount, m_pDeviceName);
    //Only report underflow when it is primary output
    if (m_bIsPrimary == true)
    {
        return m_uUnderflowCount > 0;
    }
    return false;
}

void AkAlsaSink::ResetStarved()
{
    AK_LOG_TRACE(__FUNCTION__);
    m_uUnderflowCount = 0;
}

AKRESULT AkAlsaSink::GetPluginInfo(AkPluginInfo& out_rPluginInfo)
{
    AK_LOG_TRACE(__FUNCTION__);
    out_rPluginInfo.eType         = AkPluginTypeSink;
    out_rPluginInfo.bIsInPlace    = false;
    out_rPluginInfo.uBuildVersion = AK_WWISESDK_VERSION_COMBINED;
    return AK_Success;
}

void AkAlsaSink::Consume(
    AkAudioBuffer*  in_pInputBuffer,    ///< Input audio buffer data structure. Plugins should avoid processing data in-place.
    AkRamp          in_gain             ///< Volume gain to apply to this input (prev corresponds to the beginning, next corresponds to the end of the buffer).
    )
{
    AK_LOG_TRACE(__FUNCTION__);
    if (in_pInputBuffer->uValidFrames > 0)
    {
        // Interleave channels and apply volume
        const unsigned int uNumFrames  = in_pInputBuffer->uValidFrames;
        const unsigned int uOutStride  = m_uOutNumChannels;
        const AkReal32     fGainStart  = in_gain.fPrev;
        const AkReal32     fDelta      = (in_gain.fNext - in_gain.fPrev) / (AkReal32)uNumFrames;

        std::lock_guard<std::mutex> lock(_sinkPluginBufferMutex);
        auto& latestAudioDest = _sinkPluginBuffer.push_back();
        // Channels are in AK pipeline order (i.e. LFE at the end)
        for (unsigned int i = 0; i < m_uOutNumChannels; i++)
        {
            float* AK_RESTRICT    pfSource = in_pInputBuffer->GetChannel(m_pChannelMap[i]);
            int16_t* AK_RESTRICT  pfDest   = latestAudioDest + i; // Channel offset
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
                    *pfDest = (int16_t)(f * 32767.0f);

                pfDest += uOutStride;
                fGain  += fDelta;
            }
        }

        m_bDataReady = true;
    }
}

void AkAlsaSink::OnFrameEnd()
{
    AK_LOG_TRACE(__FUNCTION__);

    // We are using the speaker if we have data ready at this point.
    _usingSpeaker = m_bDataReady;

    {
        // Lock while using _sinkPluginBuffer's data
        std::lock_guard<std::mutex> lock(_sinkPluginBufferMutex);
        // When no data has been produced (consume is not being called since SE is idle), produce silence to the RB
        if (!m_bDataReady)
        {
            // Write zeros into buffer
            auto& latestAudioDest = _sinkPluginBuffer.push_back();
            AKPLATFORM::AkMemSet(latestAudioDest, 0, m_uFrameSize * sizeof(int16_t) * m_uOutNumChannels);
        }

        if (m_writeBufferToAlsaCallback)
        {
            const auto& latestAudioDest = _sinkPluginBuffer.back();
            m_writeBufferToAlsaCallback(latestAudioDest, m_bDataReady);
        }
    }

    m_bDataReady = false;
}

AKRESULT AkAlsaSink::IsDataNeeded(AkUInt32& out_uBuffersNeeded)
{
    AK_LOG_TRACE(__FUNCTION__);
    out_uBuffersNeeded = 0;
    int returnVal = 0;

    size_t emptySpace = 0;
    {
        std::lock_guard<std::mutex> lock(_sinkPluginBufferMutex);
        emptySpace = _sinkPluginBuffer.capacity() - _sinkPluginBuffer.size();
    }

    g_uBuffersNeeded = (AkUInt32)emptySpace;
    AK_LOG_DEBUG("Set bufferNeeded: %d, devicename: %s", g_uBuffersNeeded, m_pDeviceName);
    out_uBuffersNeeded = g_uBuffersNeeded;

    return AK_Success;
}

void AkAlsaSink::SetAlsaSinkCallbacks(AkOutputGetLatencyCallbackFunc in_pfnGetLatencyCallback, AkUInt32 in_uLatencyReportThresholdMsec)
{
    if (in_pfnGetLatencyCallback)
    {
        m_pfnGetLatencyCallback = in_pfnGetLatencyCallback;
        //Threshold value should be relative with blocksize
        if (in_uLatencyReportThresholdMsec == 0)
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
    AkAlsaSink::SetAlsaSinkCallbacks(in_pfnGetLatencyCallback, in_uLatencyReportThresholdMsec);
}