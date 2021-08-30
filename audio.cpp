#include "mmdeviceapi.h"
#include "audioclient.h"
#include "winuser.h"

const IID IID_IAudioClient  = __uuidof(IAudioClient);
const IID IID_IAudioRenderClient = __uuidof(IAudioRenderClient);
const CLSID CLSID_MMDeviceEnumerator = __uuidof(MMDeviceEnumerator);
const IID IID_IMMDeviceEnumerator = __uuidof(IMMDeviceEnumerator);

REFERENCE_TIME sound_latency_fps = 60;
REFERENCE_TIME requested_sound_duration = 2*100;

u64 next_power_of_two(u64 in)
{
    in--;
    in |= in >> 1;
    in |= in >> 2;
    in |= in >> 4;
    in |= in >> 8;
    in |= in >> 16;
    in |= in >> 32;
    in++;
    return in;
}
u64 read_pos = 0;
u64 write_pos = 0;
u64 xruns;
u64 reftimes_to_samples(const REFERENCE_TIME t, const float sample_rate)
{
    return u64(sample_rate * float(t) * 0.0000001 + 0.5f);
}

REFERENCE_TIME samples_to_reftime(u64 num_samples, float sample_rate)
{
    return REFERENCE_TIME(num_samples * 10000.0f * 1000.0f / sample_rate + 0.5f);
}

#define if_failed(hr, message) if(FAILED(hr)) { MessageBox(0, message, 0, MB_OK); return false;}

struct WasapiContext{
    HANDLE audio_sample_ready_event;
    HANDLE shutdown_event;
    
    IMMDeviceEnumerator *enumerator;
    IMMDevice *output_device;
    IAudioClient *audio_client;
    IAudioRenderClient *render_client;
    
    UINT32 system_buffer_size;
    real32 sample_rate;
    HANDLE audio_thread;
    
    u64 user_buffer_size;
    real32 *user_buffer;
};

#define M_2PI (3.1415926535897932384626433 * 2.0)

void deinterleave(real32* in, real32* out, u64 channel_count, u64 sample_count)
{
    for(u64 sample = 0; sample < sample_count; sample++)
    {
        for(u64 channel = 0; channel < channel_count; channel++)
        {
            out[sample_count * channel + sample] = in[sample * channel_count + channel];
        }
    }
}

void interleave(real32* in, real32* out, 
                u64 channel_count, u64 input_offset, u64 sample_count, u64 total_buffer_size)
{
    for(int sample = 0; sample < sample_count; sample++)
    {
        for(int channel = 0; channel < channel_count; channel++)
        {
            out[sample * channel_count + channel] = in[(total_buffer_size) * channel + sample + input_offset];
        }
    }
}


double sampleIncrement;
double theta;

void audio_callback(float* output_buffer, int buffer_size)
{
    for(int sample = 0; sample < buffer_size; sample++)
    {
        output_buffer[sample] = output_buffer[sample + buffer_size] = 0.8f * sin(theta);
        //output_buffer[sample ] = 0.3f;// -1;//(float)sample/(float)buffer_size;
        //output_buffer[sample+ buffer_size] = 0;
        theta += sampleIncrement;
    }
    
}


DWORD audio_thread_fn(LPVOID Context)
{
    WasapiContext* audio_context = (WasapiContext*)Context;
    
    sampleIncrement = (440.0 * (M_2PI)) / (double)audio_context->sample_rate;
    theta = 0.00;
    
    bool still_playing = true;
    HANDLE wait_array[2] = {audio_context->shutdown_event, audio_context->audio_sample_ready_event};
    
    CoInitializeEx(0, COINIT_MULTITHREADED);
    
    
    u64 samples_used_from_user_buffer = 0;
    
    while(still_playing)
    {
        DWORD wait_result = WaitForMultipleObjects(2, wait_array, FALSE, INFINITE);
        switch(wait_result){
            case WAIT_OBJECT_0 + 0:
            {
                still_playing = false;
            }break;
            case WAIT_OBJECT_0 + 1:
            {
                UINT32 padding, buffer_size;
                audio_context->audio_client->GetCurrentPadding(&padding);
                audio_context->audio_client->GetBufferSize(&buffer_size);
                
                assert(audio_context->system_buffer_size == buffer_size);
                
                UINT32 frames_to_ask = buffer_size - padding;
                assert(audio_context->user_buffer_size >= frames_to_ask);
                
                real32 *system_buffer;
                HRESULT hr = audio_context->render_client->GetBuffer(frames_to_ask,(BYTE**) &system_buffer);
                assert(hr != AUDCLNT_E_BUFFER_TOO_LARGE);
                
                if(hr == S_OK)
                {
                    
                    //printf("samples used : %llu\n", samples_used_from_user_buffer);
                    auto samples_to_write = frames_to_ask ;
                    auto user_buffer_size = audio_context->user_buffer_size;
                    
                    if(samples_used_from_user_buffer == 0)
                    {
                        audio_callback(audio_context->user_buffer, user_buffer_size);
                        interleave(audio_context->user_buffer, system_buffer, 
                                   2, 0, samples_to_write,user_buffer_size);
                        samples_used_from_user_buffer = samples_to_write;
                    }
                    else
                    {
                        u64 samples_left_in_user_buffer = user_buffer_size - samples_used_from_user_buffer;
                        if(samples_left_in_user_buffer > samples_to_write)
                        {
                            interleave(audio_context->user_buffer, system_buffer,
                                       2, samples_used_from_user_buffer, samples_to_write,user_buffer_size);
                            samples_used_from_user_buffer += samples_to_write;
                        }
                        else if(samples_left_in_user_buffer == samples_to_write)
                        {
                            interleave(audio_context->user_buffer, system_buffer,
                                       2, samples_used_from_user_buffer, samples_to_write,user_buffer_size);
                            samples_used_from_user_buffer = 0;
                            
                        }
                        else
                        {
                            interleave(audio_context->user_buffer, system_buffer, 
                                       2, samples_used_from_user_buffer, samples_left_in_user_buffer,user_buffer_size);
                            
                            u64 samples_written_to_system_buffer = samples_left_in_user_buffer;
                            u64 samples_left_to_write_in_system_buffer = samples_to_write - samples_written_to_system_buffer;
                            
                            u64 position_to_write = (u64) system_buffer + (samples_written_to_system_buffer * sizeof(float) * 2) ;
                            
                            audio_callback(audio_context->user_buffer, user_buffer_size);
                            interleave(audio_context->user_buffer, (float*) position_to_write, 
                                       2, 0, samples_left_to_write_in_system_buffer,user_buffer_size);
                            
                            samples_used_from_user_buffer = samples_left_to_write_in_system_buffer; 
                        }
                    }
                    
                    audio_context->render_client->ReleaseBuffer(frames_to_ask, 0);
                }
                else{
                    printf("failed to get buffer\n");
                }
                
                
            }break;
        }
    }
    
    CoUninitialize();
    return S_OK;
}


bool audio_initialize(WasapiContext *ctx)
{
    *ctx = WasapiContext{};
    
    ctx->audio_sample_ready_event = CreateEventEx(0,0,0, EVENT_MODIFY_STATE | SYNCHRONIZE);
    ctx->shutdown_event = CreateEventEx(0,0,0, EVENT_MODIFY_STATE | SYNCHRONIZE);
    
    
    auto hr = CoCreateInstance(CLSID_MMDeviceEnumerator, NULL, CLSCTX_ALL, IID_IMMDeviceEnumerator,
                               (void**)&ctx->enumerator);
    
    hr = ctx->enumerator->GetDefaultAudioEndpoint(eRender, eMultimedia, &ctx->output_device);
    if_failed(hr ,"failed to get default device\n");
    
    hr = ctx->output_device->Activate(IID_IAudioClient, 
                                      CLSCTX_ALL, 
                                      0, 
                                      (void**) &ctx->audio_client);
    if_failed(hr ,"failed to active audio client\n");
    
    WAVEFORMATEX new_wave_format{
        . wFormatTag = WAVE_FORMAT_IEEE_FLOAT ,
        . nChannels = 2,
        . nSamplesPerSec = 44100,
        . nAvgBytesPerSec = 2 * sizeof(real32) * 44100,
        . nBlockAlign = 2 * sizeof (real32),
        . wBitsPerSample = sizeof (real32) * 8,
        . cbSize = 0,
    };
    
    WAVEFORMATEX* closest = nullptr;
    hr = ctx->audio_client->IsFormatSupported(AUDCLNT_SHAREMODE_SHARED, &new_wave_format, &closest);
    if_failed(hr, "unsupported format\n");
    
    WAVEFORMATEXTENSIBLE format_to_use;
    if(closest == nullptr)
    {
        memcpy (&format_to_use, &new_wave_format, sizeof(WAVEFORMATEX));
        printf("ok\n");
    }
    else
    {
        if(closest->wFormatTag == WAVE_FORMAT_EXTENSIBLE)
        {
            printf("extensible\n");
            memcpy (&format_to_use, closest, sizeof(WAVEFORMATEXTENSIBLE));
        }
        else
        {
            memcpy (&format_to_use, closest, sizeof(WAVEFORMATEX));
        }
        CoTaskMemFree(closest);
    }
    printf("sample rate asked  %d\n",new_wave_format.nSamplesPerSec); 
    printf("sample rate to use %d\n",format_to_use.Format.nSamplesPerSec); 
    
    
    ctx->sample_rate = format_to_use.Format.nSamplesPerSec;
    
    REFERENCE_TIME default_period, min_period;
    ctx->audio_client->GetDevicePeriod(&default_period, &min_period);
    
    u64 min_buffer_size = reftimes_to_samples (min_period, ctx->sample_rate);
    u64 default_buffer_size  = reftimes_to_samples (default_period, ctx->sample_rate);
    
    printf("min buffer size : %llu \ndefault buffer size : %llu\n", min_buffer_size, default_buffer_size);
    
    
    hr = ctx->audio_client->Initialize(AUDCLNT_SHAREMODE_SHARED, 
                                       AUDCLNT_STREAMFLAGS_EVENTCALLBACK | AUDCLNT_STREAMFLAGS_NOPERSIST, 
                                       default_period,
                                       0,
                                       (WAVEFORMATEX*)&format_to_use, 
                                       0);
    if(hr != S_OK)
    {
        if (hr == AUDCLNT_E_BUFFER_SIZE_NOT_ALIGNED )  // AUDCLNT_E_BUFFER_SIZE_NOT_ALIGNED
        {
            UINT32 num_frames;
            hr = ctx->audio_client->GetBufferSize(&num_frames);
            if(hr == AUDCLNT_E_NOT_INITIALIZED)
                printf("client not initialized when asked for buffer size\n");
            if_failed(hr, "failed to get buffer size again\n");
            ctx->audio_client->Release();
            
            printf("new buffer size %lu\n", num_frames);
            
            REFERENCE_TIME new_buffer_period = samples_to_reftime(num_frames, ctx->sample_rate);
            
            hr = ctx->output_device->Activate(IID_IAudioClient, 
                                              CLSCTX_ALL, 
                                              0, 
                                              (void**) &ctx->audio_client);
            if_failed(hr, "failed to re acquire client");
            
            hr = ctx->audio_client->Initialize(AUDCLNT_SHAREMODE_SHARED, 
                                               AUDCLNT_STREAMFLAGS_EVENTCALLBACK | AUDCLNT_STREAMFLAGS_NOPERSIST, 
                                               new_buffer_period,
                                               0,
                                               (WAVEFORMATEX*)&format_to_use, 
                                               0);
            if_failed(hr, "failed to initialize again");
            
        }
        else if(hr == AUDCLNT_E_INVALID_DEVICE_PERIOD)
        {
            printf("invalid device period\n"); return false;
        }
        else if(hr == AUDCLNT_E_UNSUPPORTED_FORMAT)
        {
            printf("unsupported format\n"); return false;
        }
        else if(hr == AUDCLNT_E_BUFFER_SIZE_ERROR)
        {
            printf("buffer size error\n"); return false;
        }
        else if(hr == AUDCLNT_E_BUFDURATION_PERIOD_NOT_EQUAL)
        {
            printf("duration et priod no equal\n"); return false;
        }
        else if(hr == E_INVALIDARG)
        {
            printf("invalid arguments\n"); return false;
        }
        else
        {
            printf("other error sorry\n"); return false;
        }
    }
    
    
    
    
    printf("\n\ninitialized\n");
    
    
    hr = ctx->audio_client->SetEventHandle(ctx->audio_sample_ready_event);
    if_failed(hr ,"failed to set device handlee\n");
    
    hr = ctx->audio_client->Start();
    if_failed(hr ,"failed to start audio client\n");
    
    hr = ctx->audio_client->GetService(IID_IAudioRenderClient, (void **)& ctx->render_client);
    if_failed(hr, "failed to get render client\n");
    
    
    hr = ctx->audio_client->GetBufferSize(&ctx->system_buffer_size);
    
    UINT32 padding;
    hr = ctx->audio_client->GetCurrentPadding(&padding);
    if_failed(hr, "failed to get padding\n");
    
    /*
    REFERENCE_TIME latency = -1;
    hr = ctx->audio_client->GetStreamLatency(&latency);
    if_failed(hr, "couldn't get latency");
    
    printf("latency frame count %llu\n", reftimes_to_samples(latency, ctx->sample_rate));
    printf("latency time : %lld  ms\n", latency * 10000);
    */
    
    printf("actual buffer size :%d, padding %d\n", ctx->system_buffer_size, padding);
    printf("actual buffer duration : %d ms\n", 1000 * ctx->system_buffer_size / (int)ctx->sample_rate);
    
    ctx->user_buffer_size = next_power_of_two(default_buffer_size); 
    u64 reservoir_byte_size = (ctx->user_buffer_size * format_to_use.Format.nBlockAlign * 2 /* channels */);
    ctx->user_buffer = (float*) malloc(reservoir_byte_size);
    
    BYTE *data; 
    hr = ctx->render_client->GetBuffer(ctx->system_buffer_size - padding, &data);
    if_failed(hr, "failed to get initial buffer\n");
    hr = ctx->render_client->ReleaseBuffer(ctx->system_buffer_size - padding, AUDCLNT_BUFFERFLAGS_SILENT);
    
    ctx->audio_thread = CreateThread(0,0, audio_thread_fn, (void*)ctx, 0, 0);
    
    return true;
}

void uninitialize_audio(WasapiContext *ctx)
{
    
    SetEvent(ctx->shutdown_event); //ferle stp
    WaitForSingleObject(ctx->audio_thread, INFINITE); //on attend sa réponse
    CloseHandle(ctx->audio_client);
    
    ctx->audio_client->Stop();
    
}

