#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <d3d12.h>
#include <d3d12video.h>
#include <dxgi1_6.h>
#include <windows.h>

#if __MINGW32__
#include "unfuck-mingw.h"
#endif

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

static const char* app_name      = "d3d12-video-decode";
static HINSTANCE   module_handle = NULL;
static HWND        window_handle = NULL;
static WNDCLASSA   window_class  = {0};

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
    if (window_handle != NULL)
    {
        DestroyWindow(window_handle);
        window_handle = NULL;
    }
    if (module_handle != NULL)
    {
        UnregisterClassA(app_name, module_handle);
        module_handle = NULL;
    }
}

void print_win_msg(DWORD msg)
{
    LPVOID lpMsgBuf;
    DWORD  res = FormatMessage(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        NULL,
        msg,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        (LPSTR)&lpMsgBuf,
        0,
        NULL
    );
    if (res == 0)
    {
        printf("Failed to format windows message: 0x%lx\n", GetLastError());
        return;
    }
    printf("windows: %s\n", (LPCTSTR)lpMsgBuf);
    LocalFree(lpMsgBuf);
}

void print_usage()
{
    printf("Usage: d3d12-video-decode.exe <filename> [options]\n");
    printf("  options:\n");
    printf("    --width     <int> (default: 1920)       Sets the input video width.\n");
    printf("    --height    <int> (default: 1080)       Sets the input video height.\n");
    printf("    --fps       <int> (default: 60)         Sets the input video framerate.\n");
    printf("    --bitrate   <int> (default: 3000000)    Sets the input video bitrate estimate.\n");
    printf("    --profile   <str> (default: h264)       Sets the input video profile.\n");
    printf("  profiles: [h264]\n");
}

const char* profile_to_str(const GUID* guid)
{
    if (IsEqualGUID(guid, &D3D12_VIDEO_DECODE_PROFILE_H264))
    {
        return "h264";
    }
    return "unk";
}

int need_conversion()
{
    if (video_input_format.Format != video_output_format.Format)
    {
        return 1;
    }

    if (video_input_format.ColorSpace != video_output_format.ColorSpace)
    {
        return 1;
    }

    return 0;
}

void print_options()
{
    printf("video_decode_config:\n");
    printf("  DecodeProfile: %s\n", profile_to_str(&video_decode_config.DecodeProfile));
    printf("  BitstreamEncryption: %d\n", video_decode_config.BitstreamEncryption);
    printf("  InterlaceType: %d\n", video_decode_config.InterlaceType);
    printf("video_input_format:\n");
    printf("  Format: %d\n", video_input_format.Format);
    printf("  ColorSpace: %d\n", video_input_format.ColorSpace);
    printf("video_input_sample:\n");
    printf("  Width: %u\n", video_input_sample.Width);
    printf("  Height: %u\n", video_input_sample.Height);
    printf("video_input_framerate:\n");
    printf("  Numerator: %u\n", video_input_framerate.Numerator);
    printf("  Denominator: %u\n", video_input_framerate.Denominator);
    printf("video_input_bitrate: %u\n", video_input_bitrate);
    printf("video_output_format:\n");
    printf("  Format: %d\n", video_output_format.Format);
    printf("  ColorSpace: %d\n", video_output_format.ColorSpace);
    printf("convert: %d\n", need_conversion());
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    switch (msg)
    {
        case WM_KEYDOWN:
        {
            if (wparam == VK_ESCAPE)
            {
                PostQuitMessage(0);
                return 0;
            }
            else
            {
                return DefWindowProc(hwnd, msg, wparam, lparam);
            }
        }

        case WM_DESTROY:
        case WM_CLOSE:
        {
            PostQuitMessage(0);
            return 0;
        }

        default:
        {
            return DefWindowProc(hwnd, msg, wparam, lparam);
        }
    }
}

int main(int argv, char** argc)
{
    HRESULT     res = S_OK;
    const char* filename;
    MSG         msg = {0};

    if (argv < 2)
    {
        printf("Missing filename argument\n");
        print_usage();
        return 1;
    }

    filename = argc[1];
    printf("Using file: %s\n", filename);

    /* Video Decode Configuration */
    video_decode_config.DecodeProfile       = D3D12_VIDEO_DECODE_PROFILE_H264;
    video_decode_config.BitstreamEncryption = D3D12_BITSTREAM_ENCRYPTION_TYPE_NONE;
    video_decode_config.InterlaceType       = D3D12_VIDEO_FRAME_CODED_INTERLACE_TYPE_NONE;
    video_input_format.Format               = DXGI_FORMAT_420_OPAQUE;
    video_input_format.ColorSpace           = DXGI_COLOR_SPACE_RGB_STUDIO_G22_NONE_P709;
    video_input_sample.Width                = 1920;
    video_input_sample.Height               = 1080;
    video_input_framerate.Numerator         = 60;
    video_input_framerate.Denominator       = 1;
    video_input_bitrate                     = 3000 * 1000;

    for (int i = 2; i < argv; i++)
    {
        if (strcmp(argc[i], "--width") == 0)
        {
            if (i + 1 >= argv)
            {
                printf("Missing argument for --width\n");
                print_usage();
                return 1;
            }

            video_input_sample.Width = (UINT)atoi(argc[i + 1]);
        }
        else if (strcmp(argc[i], "--height") == 0)
        {
            if (i + 1 >= argv)
            {
                printf("Missing argument for --height\n");
                print_usage();
                return 1;
            }

            video_input_sample.Height = (UINT)atoi(argc[i + 1]);
        }
        else if (strcmp(argc[i], "--fps") == 0)
        {
            if (i + 1 >= argv)
            {
                printf("Missing argument for --fps\n");
                print_usage();
                return 1;
            }

            video_input_framerate.Numerator = (UINT)atoi(argc[i + 1]);
        }
        else if (strcmp(argc[i], "--bitrate") == 0)
        {
            if (i + 1 >= argv)
            {
                printf("Missing argument for --bitrate\n");
                print_usage();
                return 1;
            }

            video_input_bitrate = (UINT)atoi(argc[i + 1]);
        }
        else if (strcmp(argc[i], "--profile") == 0)
        {
            if (i + 1 >= argv)
            {
                printf("Missing argument for --profile\n");
                print_usage();
                return 1;
            }

            if (strcmp(argc[i + 1], "h264") == 0)
            {
                video_decode_config.DecodeProfile = D3D12_VIDEO_DECODE_PROFILE_H264;
            }
            else
            {
                printf("Unsupported profile %s\n", argc[i + 1]);
                return 1;
            }
        }
    }

    video_input_sample.Format = video_input_format;
    video_output_format       = video_input_format;

    print_options();

    /* Window */
    module_handle              = GetModuleHandle(NULL);
    window_class.lpfnWndProc   = WindowProc;
    window_class.hInstance     = module_handle;
    window_class.lpszClassName = app_name;
    RegisterClassA(&window_class);
    window_handle = CreateWindowExA(
        WS_EX_APPWINDOW,
        app_name,
        app_name,
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        video_input_sample.Width,
        video_input_sample.Height,
        NULL,
        NULL,
        module_handle,
        NULL
    );
    if (window_handle == NULL)
    {
        printf("Failed to create a window\n");
        print_win_msg(GetLastError());
        release();
        return 1;
    }
    ShowWindow(window_handle, SW_SHOW);
    SetForegroundWindow(window_handle);
    SetFocus(window_handle);
    ShowCursor(1);

    /* Debug Layers */
    res = D3D12GetDebugInterface(&IID_ID3D12Debug, (void**)&debug);
    if (SUCCEEDED(res))
    {
        debug->lpVtbl->EnableDebugLayer(debug);
    }
    else
    {
        printf("Failed to enable debug layers\n");
        print_win_msg(res);
    }

    /* DXGI Factory */
    res = CreateDXGIFactory(&IID_IDXGIFactory6, (void**)&factory);
    if (FAILED(res))
    {
        printf("Failed to create DXGI Factory\n");
        print_win_msg(res);
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
        print_win_msg(res);
        release();
        return 1;
    }
    res = adapter->lpVtbl->GetDesc(adapter, &adapter_desc);
    if (FAILED(res))
    {
        printf("Failed to get DXGI Adapter Description\n");
        print_win_msg(res);
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
        print_win_msg(res);
        release();
        return 1;
    }

    /* Video Device */
    res = device->lpVtbl->QueryInterface(device, &IID_ID3D12VideoDevice, (void**)&video_device);
    if (FAILED(res))
    {
        printf("Failed to query Video Device\n");
        print_win_msg(res);
        release();
        return 1;
    }

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
        print_win_msg(res);
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
        print_win_msg(res);
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
        print_win_msg(res);
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
        print_win_msg(res);
        release();
        return 1;
    }
    printf("D3D12_FEATURE_VIDEO_DECODE_FORMATS:\n");
    for (UINT i = 0; i < video_decode_formats.FormatCount; i++)
    {
        printf("  %d\n", video_decode_formats.pOutputFormats[i]);
    }

    /* Video Conversion Support */
    if (need_conversion())
    {
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
            print_win_msg(res);
            release();
            return 1;
        }
        printf("D3D12_FEATURE_VIDEO_DECODE_CONVERSION_SUPPORT:\n");
        printf("  SupportFlags: %d\n", video_decode_conversions.SupportFlags);
        printf("  ScaleSupport:\n");
        printf(
            "    OutputSizeRange: (%u-%u)x(%u-%u)\n",
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
        print_win_msg(res);
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
        print_win_msg(res);
        release();
        return 1;
    }

    /* Window loop */
    while (GetMessage(&msg, NULL, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    release();
    return 0;
}
