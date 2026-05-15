#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <d3d12.h>
#include <d3d12video.h>
#include <dxgi1_6.h>
#include <Windows.h>

#define TO_GB(B) ((double)(B) / (1024.0 * 1024.0 * 1024.0))

static ID3D12Debug*            debug              = NULL;
static IDXGIFactory6*          factory            = NULL;
static IDXGIAdapter*           adapter            = NULL;
static ID3D12Device*           device             = NULL;
static ID3D12VideoDevice*      video_device       = NULL;
static ID3D12VideoDecoder*     video_decoder      = NULL;
static ID3D12VideoDecoderHeap* video_decoder_heap = NULL;

static DXGI_ADAPTER_DESC             adapter_desc            = {0};
static D3D12_VIDEO_DECODER_DESC      video_decoder_desc      = {0};
static D3D12_VIDEO_DECODER_HEAP_DESC video_decoder_heap_desc = {0};

static D3D12_FEATURE_DATA_VIDEO_DECODE_PROFILES           video_decode_profiles    = {0};
static D3D12_FEATURE_DATA_VIDEO_DECODE_FORMATS            video_decode_formats     = {0};
static D3D12_FEATURE_DATA_VIDEO_DECODE_CONVERSION_SUPPORT video_decode_conversions = {0};
static D3D12_VIDEO_DECODE_CONFIGURATION                   video_decode_config      = {0};
static D3D12_VIDEO_SAMPLE                                 video_input_sample       = {0};
static D3D12_VIDEO_FORMAT                                 video_input_format       = {0};
static D3D12_VIDEO_FORMAT                                 video_output_format      = {0};
static DXGI_RATIONAL                                      video_input_framerate    = {0};
static UINT                                               video_input_bitrate      = 0;

static void print_guid(const GUID* guid)
{
    if (!guid)
    {
        printf("(null)");
    }

    printf(
        "{%08lx-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x}",
        (unsigned long)guid->Data1,
        guid->Data2,
        guid->Data3,
        guid->Data4[0],
        guid->Data4[1],
        guid->Data4[2],
        guid->Data4[3],
        guid->Data4[4],
        guid->Data4[5],
        guid->Data4[6],
        guid->Data4[7]
    );
}

void release()
{
    if (video_decode_formats.pOutputFormats != NULL)
    {
        free(video_decode_formats.pOutputFormats);
        video_decode_formats.pOutputFormats = NULL;
    }
    if (video_decode_profiles.pProfiles != NULL)
    {
        free(video_decode_profiles.pProfiles);
        video_decode_profiles.pProfiles = NULL;
    }
    if (video_decoder_heap != NULL)
    {
        video_decoder_heap->lpVtbl->Release(video_decoder_heap);
        video_decoder_heap = NULL;
    }
    if (video_decoder != NULL)
    {
        video_decoder->lpVtbl->Release(video_decoder);
        video_decoder = NULL;
    }
    if (video_device != NULL)
    {
        video_device->lpVtbl->Release(video_device);
        video_device = NULL;
    }
    if (device != NULL)
    {
        device->lpVtbl->Release(device);
        device = NULL;
    }
    if (adapter != NULL)
    {
        adapter->lpVtbl->Release(adapter);
        adapter = NULL;
    }
    if (factory != NULL)
    {
        factory->lpVtbl->Release(factory);
        factory = NULL;
    }
    if (debug != NULL)
    {
        debug->lpVtbl->Release(debug);
        debug = NULL;
    }
}

int main(int argv, char** argc)
{
    /* Debug Layers */
    HRESULT res = D3D12GetDebugInterface(&IID_ID3D12Debug, (void**)&debug);
    if (SUCCEEDED(res))
    {
        debug->lpVtbl->EnableDebugLayer(debug);
    }
    else
    {
        printf("Failed to enable debug layers\n");
    }

    /* DXGI Factory */
    res = CreateDXGIFactory(&IID_IDXGIFactory6, (void**)&factory);
    if (FAILED(res))
    {
        printf("Failed to create DXGI Factory\n");
        release();
        return 1;
    }

    /* Adapter */
    res = factory->lpVtbl->EnumAdapterByGpuPreference(
        factory,
        0,
        DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
        &IID_IDXGIAdapter,
        (void**)&adapter
    );
    if (FAILED(res))
    {
        printf("Failed to get DXGI Adapter\n");
        release();
        return 1;
    }
    res = adapter->lpVtbl->GetDesc(adapter, &adapter_desc);
    if (FAILED(res))
    {
        printf("Failed to get DXGI Adapter Description\n");
        release();
        return 1;
    }
    printf("Adapter:\n");
    printf("  %ls\n", adapter_desc.Description);
    printf("  VID MEM: %.1f GB\n", TO_GB(adapter_desc.DedicatedVideoMemory));
    printf("  SYS MEM: %.1f GB\n", TO_GB(adapter_desc.DedicatedSystemMemory));
    printf("  SHA MEM: %.1f GB\n", TO_GB(adapter_desc.SharedSystemMemory));

    /* Device */
    res = D3D12CreateDevice(
        (IUnknown*)adapter,
        D3D_FEATURE_LEVEL_12_0,
        &IID_ID3D12Device,
        (void**)&device
    );
    if (FAILED(res))
    {
        printf("Failed to create device\n");
        release();
        return 1;
    }

    /* Video Device */
    res = device->lpVtbl->QueryInterface(device, &IID_ID3D12VideoDevice, (void**)&video_device);
    if (FAILED(res))
    {
        printf("Failed to query Video Device: 0x%lx\n", res);
        release();
        return 1;
    }

    /* Video Decode Configuration */
    // TODO actually get this from the source file
    video_decode_config.DecodeProfile       = D3D12_VIDEO_DECODE_PROFILE_H264;
    video_decode_config.BitstreamEncryption = D3D12_BITSTREAM_ENCRYPTION_TYPE_NONE;
    video_decode_config.InterlaceType       = D3D12_VIDEO_FRAME_CODED_INTERLACE_TYPE_NONE;
    video_input_format.Format               = DXGI_FORMAT_420_OPAQUE;
    video_input_format.ColorSpace           = DXGI_COLOR_SPACE_RGB_STUDIO_G22_NONE_P709;
    video_input_sample.Width                = 1920;
    video_input_sample.Height               = 1080;
    video_input_sample.Format               = video_input_format;
    video_input_framerate.Numerator         = 60;
    video_input_framerate.Denominator       = 1;
    video_input_bitrate                     = 3000 * 1024;
    video_output_format                     = video_input_format;

    /* Video Decode Profiles */
    res = video_device->lpVtbl->CheckFeatureSupport(
        video_device,
        D3D12_FEATURE_VIDEO_DECODE_PROFILE_COUNT,
        &video_decode_profiles,
        sizeof(video_decode_profiles)
    );
    if (FAILED(res))
    {
        printf("Failed to check D3D12_FEATURE_VIDEO_DECODE_PROFILE_COUNT\n");
        release();
        return 1;
    }
    video_decode_profiles.pProfiles = malloc(sizeof(GUID) * video_decode_profiles.ProfileCount);
    res                             = video_device->lpVtbl->CheckFeatureSupport(
        video_device,
        D3D12_FEATURE_VIDEO_DECODE_PROFILES,
        &video_decode_profiles,
        sizeof(video_decode_profiles)
    );
    if (FAILED(res))
    {
        printf("Failed to check D3D12_FEATURE_VIDEO_DECODE_PROFILES\n");
        release();
        return 1;
    }
    printf("D3D12_FEATURE_VIDEO_DECODE_PROFILES:\n");
    for (UINT i = 0; i < video_decode_profiles.ProfileCount; i++)
    {
        printf("  ");
        print_guid(&video_decode_profiles.pProfiles[i]);
        printf("\n");
    }

    /* Video Decode Formats */
    video_decode_formats.Configuration = video_decode_config;
    res                                = video_device->lpVtbl->CheckFeatureSupport(
        video_device,
        D3D12_FEATURE_VIDEO_DECODE_FORMAT_COUNT,
        &video_decode_formats,
        sizeof(video_decode_formats)
    );
    if (FAILED(res))
    {
        printf("Failed to check D3D12_FEATURE_VIDEO_DECODE_FORMAT_COUNT\n");
        release();
        return 1;
    }
    video_decode_formats.pOutputFormats =
        malloc(sizeof(DXGI_FORMAT) * video_decode_formats.FormatCount);
    res = video_device->lpVtbl->CheckFeatureSupport(
        video_device,
        D3D12_FEATURE_VIDEO_DECODE_FORMATS,
        &video_decode_formats,
        sizeof(video_decode_formats)
    );
    if (FAILED(res))
    {
        printf("Failed to check D3D12_FEATURE_VIDEO_DECODE_FORMATS\n");
        release();
        return 1;
    }
    printf("D3D12_FEATURE_VIDEO_DECODE_FORMATS:\n");
    for (UINT i = 0; i < video_decode_formats.FormatCount; i++)
    {
        printf("  %d\n", video_decode_formats.pOutputFormats[i]);
    }

    /* Video Conversion Support */
    video_decode_conversions.Configuration = video_decode_config;
    video_decode_conversions.DecodeSample  = video_input_sample;
    video_decode_conversions.OutputFormat  = video_output_format;
    video_decode_conversions.BitRate       = video_input_bitrate;
    res                                    = video_device->lpVtbl->CheckFeatureSupport(
        video_device,
        D3D12_FEATURE_VIDEO_DECODE_CONVERSION_SUPPORT,
        &video_decode_conversions,
        sizeof(video_decode_conversions)
    );
    if (FAILED(res))
    {
        printf("Failed to check D3D12_FEATURE_VIDEO_DECODE_CONVERSION_SUPPORT\n");
        release();
        return 1;
    }
    printf("D3D12_FEATURE_VIDEO_DECODE_CONVERSION_SUPPORT:\n");
    printf("  SupportFlags: %d\n", video_decode_conversions.SupportFlags);
    printf("  ScaleSupport:\n");
    printf(
        "    OutputSizeRange: (%lu-%lu)x(%lu-%lu)\n",
        video_decode_conversions.ScaleSupport.OutputSizeRange.MinWidth,
        video_decode_conversions.ScaleSupport.OutputSizeRange.MaxWidth,
        video_decode_conversions.ScaleSupport.OutputSizeRange.MinHeight,
        video_decode_conversions.ScaleSupport.OutputSizeRange.MaxHeight
    );
    printf("    Flags: %d\n", video_decode_conversions.ScaleSupport.Flags);
    if (video_decode_conversions.SupportFlags
        != D3D12_VIDEO_DECODE_CONVERSION_SUPPORT_FLAG_SUPPORTED)
    {
        printf("Requested video decode conversion not supported\n");
        release();
        return 1;
    }

    /* Video Decoder */
    video_decoder_desc.Configuration = video_decode_config;
    res                              = video_device->lpVtbl->CreateVideoDecoder(
        video_device,
        &video_decoder_desc,
        &IID_ID3D12VideoDecoder,
        (void**)&video_decoder
    );
    if (FAILED(res))
    {
        printf("Failed to create Video Decoder\n");
        release();
        return 1;
    }

    /* Video Decoder Heap */
    video_decoder_heap_desc.Configuration               = video_decode_config;
    video_decoder_heap_desc.DecodeWidth                 = video_input_sample.Width;
    video_decoder_heap_desc.DecodeHeight                = video_input_sample.Height;
    video_decoder_heap_desc.Format                      = video_input_format.Format;
    video_decoder_heap_desc.FrameRate                   = video_input_framerate;
    video_decoder_heap_desc.BitRate                     = video_input_bitrate;
    video_decoder_heap_desc.MaxDecodePictureBufferCount = 8; // ?
    res = video_device->lpVtbl->CreateVideoDecoderHeap(
        video_device,
        &video_decoder_heap_desc,
        &IID_ID3D12VideoDecoderHeap,
        (void**)&video_decoder_heap
    );
    if (FAILED(res))
    {
        printf("Failed to create Video Decoder Heap\n");
        release();
        return 1;
    }

    release();
    return 0;
}
