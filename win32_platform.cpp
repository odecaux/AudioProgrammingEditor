#include "initguid.h"
#include "windows.h"
#include "windowsx.h"
#include "assert.h"
#include "d2d1.h"
#include "stdlib.h"
#include "stdio.h"
#define  DIRECTINPUT_VERSION 0x0800
#include "Dinput.h"
#include "Dwrite.h"
#include "Mmdeviceapi.h"
#include "functiondiscoverykeys.h"
#include "Audioclient.h"



#include "math.h"
struct GraphicsContext{
    ID2D1Factory* factory;
    ID2D1HwndRenderTarget* render_target;
    IDWriteFactory* write_factory;
    IDWriteTextFormat* text_format;
    ID2D1SolidColorBrush* grey_brush;
    ID2D1SolidColorBrush* dark_brush;
    ID2D1SolidColorBrush* red_brush;
    ID2D1SolidColorBrush* green_brush;
};

GraphicsContext graphics_ctx;

#include "structs.h"

OS os;
#include "audio.cpp"

#include "strings.h"
#include "memory.h"
#include "app.h"

void* win32_reserve(u64 size)
{
    return VirtualAlloc(0, size, MEM_RESERVE, PAGE_NOACCESS);
}

void win32_commit(void* memory, u64 size)
{
    VirtualAlloc(memory, size, MEM_COMMIT, PAGE_READWRITE);
}

void win32_release(void* memory)
{
    VirtualFree(memory, 0, MEM_RELEASE);
}


HINSTANCE win32_load_library(String path)
{
    return LoadLibrary(path.str);
}

void win32_free_library(HINSTANCE library_handle)
{
    FreeLibrary(library_handle);
}

void* win32_get_library_function(HINSTANCE library,String function_name) 
{
    return GetProcAddress(library, function_name.str);
}

u64 win32_get_last_modified_time(String path)
{
    FILETIME last_write_time{};
    WIN32_FILE_ATTRIBUTE_DATA attributes;
    
    if (GetFileAttributesEx(path.str, GetFileExInfoStandard, &attributes) != 0)
    {
        bool is_file = (attributes.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
        if(is_file)
            last_write_time = attributes.ftLastWriteTime;
    }
    _ULARGE_INTEGER u_integer_time;
    u_integer_time.u.LowPart = last_write_time.dwLowDateTime;
    u_integer_time.u.HighPart = last_write_time.dwHighDateTime;
    return (u64) u_integer_time.QuadPart;
}


bool win32_copy_file(String source_path, String destination_path, bool overwrite)
{
    return CopyFile(source_path.str, destination_path.str, !overwrite);
}

bool win32_create_directory(String path)
{
    return CreateDirectoryA(path.str, 0); 
}


u64 win32_enumerate_matching_filenames(String pattern, 
                                       String* out_filenames,// TODO(octave): rempalcer par une pool 
                                       StringStorage* storage)// TODO(octave): size
{
    WIN32_FIND_DATA matching_file;
    auto result = FindFirstFile(pattern.str, &matching_file);
    
    u64 filename_idx = 0;
    if (result == INVALID_HANDLE_VALUE)
    {
        return 0;
    }
    do
    {
        out_filenames[filename_idx++] = string_storage_copy_c_string(storage,matching_file.cFileName);
    }
    while (FindNextFile(result, &matching_file) != 0);
    
    auto dw_error = GetLastError();
    assert(dw_error == ERROR_NO_MORE_FILES);
    
    FindClose(result);
    
    return filename_idx;
}

Vec2 win32_get_mouse_position(HWND window)
{
    POINT mouse_position;
    GetCursorPos(&mouse_position);
    ScreenToClient(window, &mouse_position);
    return Vec2{(real32)mouse_position.x, (real32)mouse_position.y};
}

void win32_error_window(String title, String text)
{
    MessageBox(0, text.str, title.str, MB_OK);
}

i64 win32_init_timer()
{
    LARGE_INTEGER time;
    QueryPerformanceCounter(&time);
    return (i64) time.QuadPart;
}

i64 win32_pace_60_fps(i64 last_time, LARGE_INTEGER counter_frequency, real32* delta_time) // TODO(octave): refactor variable names
{
    LARGE_INTEGER render_end;
    QueryPerformanceCounter(&render_end);
    u64 render_elapsed  = render_end.QuadPart - last_time;
    auto ms_render = ((1000000.0f*render_elapsed) / counter_frequency.QuadPart) / 1000.0f;
    int ceiled = ceil(ms_render);
    
    if(50-ceiled  > 0)
    {
        Sleep(50 - ceiled);
    }
    
    LARGE_INTEGER frame_end;
    QueryPerformanceCounter(&frame_end);
    auto frame_elapsed  = frame_end.QuadPart - last_time;
    auto ms_frame = ((1000000.0f*frame_elapsed) / counter_frequency.QuadPart) / 1000.0f;
    *delta_time = ms_frame;
    ceiled = floor(ms_frame);
    return frame_end.QuadPart;
}


D2D1_POINT_2F to_d2d_point(const Vec2& point)
{
    return {point.x, point.y};
}
D2D1_RECT_F to_d2d_rect(Rect rect)
{
    return D2D1::RectF(rect.origin.x, rect.origin.y, rect.origin.x + rect.dim.x, rect.origin.y + rect.dim.y);
}

ID2D1SolidColorBrush* win32_color_to_brush(Color color, GraphicsContext& graphics_ctx)
{
    ID2D1SolidColorBrush* brush;
    switch(color)
    {
        case Color_Grey:
        {
            brush = graphics_ctx.grey_brush;
        }break;
        case Color_Dark:
        {
            brush = graphics_ctx.dark_brush;
        }break;
        case Color_Red:
        {
            brush = graphics_ctx.red_brush;
        }break;
        case Color_Green:
        {
            brush = graphics_ctx.green_brush;
        }break;
    }
    return brush;
}

void win32_fill_rectangle(Rect bounds, Color color, GraphicsContext& graphics_ctx)
{
    auto d2d_bounds  = to_d2d_rect(bounds);
    auto brush = win32_color_to_brush(color, graphics_ctx);
    graphics_ctx.render_target->FillRectangle(&d2d_bounds, brush);
}

void win32_draw_text(const String& text, i32 size, Rect bounds, Color color, GraphicsContext& graphics_ctx)
{
    auto brush = win32_color_to_brush(color, graphics_ctx);
    // TODO(octave): verifier que y a pas de bug ici
    WCHAR dest[512] = {0};
    MultiByteToWideChar(CP_UTF8, 0,text.str, (i32)text.size,dest,511);
    graphics_ctx.render_target->DrawText(dest, size,  graphics_ctx.text_format,to_d2d_rect(bounds), brush); 
    
}

void win32_draw_line(Vec2 start, Vec2 end, Color color, real32 width, GraphicsContext& graphics_ctx)
{
    auto brush = win32_color_to_brush(color, graphics_ctx);
    graphics_ctx.render_target->DrawLine({start.x, start.y}, {end.x, end.y}, brush, width);
}




LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM w_param, LPARAM l_param)
{
    LRESULT result = 0;
    
    if (message == WM_SIZE)
    {
        if(graphics_ctx.render_target != nullptr)
        {
            UINT width = LOWORD(l_param);
            UINT height = HIWORD(l_param);
            graphics_ctx.render_target->Resize(D2D1::SizeU(width, height));
        }
        return 0;
    }
    else if(message == WM_MOUSEMOVE)
    {
        //something
    }
    else if(message == WM_DESTROY || message == WM_DESTROY || message == WM_QUIT)
    {
        PostQuitMessage(0);
        return 0;
    }
    else 
    {
        result = DefWindowProc(window, message, w_param, l_param); 
    }
    return result;
}

//c sin(20)

int WinMain(HINSTANCE instance, HINSTANCE previous, LPSTR cmdLine, int showCmd)
{
    {
        os.load_library = win32_load_library,
        os.free_library = win32_free_library,
        os.create_directory = win32_create_directory,
        os.get_library_function = win32_get_library_function,
        os.get_last_modified_time = win32_get_last_modified_time,
        os.copy_file = win32_copy_file,
        os.error_window = win32_error_window,
        os.init_timer = win32_init_timer,
        os.pace_60_fps = win32_pace_60_fps,
        os.fill_rectangle = win32_fill_rectangle,
        os.draw_text = win32_draw_text,
        os.draw_line = win32_draw_line,
        os.enumerate_matching_filenames = win32_enumerate_matching_filenames;
        os.reserve = win32_reserve;
        os.commit = win32_commit;
        os.release = win32_release;
    }
    
    //check that there's only one instance
    {
        auto single_instance_event_flag = CreateEventW( NULL, TRUE, FALSE, L"APE_instance_flag" );
        if(GetLastError() == ERROR_ALREADY_EXISTS) 
        {
            return -1;
        }
    }
    
    CoInitializeEx(NULL, COINIT_MULTITHREADED); 
    
    //d2d loading
    {
        auto success  = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,
                                          __uuidof(graphics_ctx.factory),
                                          (void**)&graphics_ctx.factory);
        assert(graphics_ctx.factory != nullptr && SUCCEEDED(success));
    }
    
    
    WasapiContext audio_ctx;
    audio_initialize(&audio_ctx);
    
    AudioParameters audio_parameters{};
    audio_parameters.sample_rate = audio_ctx.sample_rate;
    audio_parameters.num_channels = 2;
    audio_parameters.num_samples = audio_ctx.user_buffer_size; 
    
    
    if(timeBeginPeriod(1) != TIMERR_NOERROR)
    {
        OutputDebugString("Could not set scheduler granularity");
        return -1;
    }
    
    LARGE_INTEGER counter_frequency;
    QueryPerformanceFrequency(&counter_frequency);
    
    {
        HANDLE current_process = GetCurrentProcess();
        auto hr = SetPriorityClass(current_process, HIGH_PRIORITY_CLASS);
        if(FAILED(hr))
        {
            printf("failed to elevate process to high priority");
            return -1;
        }
    }
    
    
    WNDCLASSEX main_class{
        .cbSize = sizeof main_class,
        .style         = CS_HREDRAW | CS_VREDRAW,
        .lpfnWndProc = WindowProc,
        .hInstance = instance,
        .lpszClassName = "Main Class"
    };
    RegisterClassEx(&main_class);
    
    HWND window = CreateWindow("Main Class", "Test", 
                               WS_OVERLAPPEDWINDOW,
                               CW_USEDEFAULT, CW_USEDEFAULT, 
                               1280,720,
                               0,0,instance,0);
    ShowWindow(window, showCmd);
    UpdateWindow(window);
    
    
    SystemArena main_arena = system_arena_create();
    Arena frame_arena = arena_create_from_system_arena(&main_arena, Megabytes(4));
    
    StringStorage plugin_name_storage = string_storage_from_system_arena(&main_arena, Megabytes(4));
    
    PluginDefinition definitions[512];
    u16 definition_count = 0;
    search_and_load_plugins(definitions, definition_count, plugin_name_storage); 
    
    Pool<Plugin> plugins;
    Pool<Link> links;
    
    IO frame_io
    {
        .delta_time = 0,
        .time = 0,
        
        .mouse_down = false,
        .mouse_clicked = false,
        .mouse_released = true,
        
        .right_mouse_down = false,
        .mouse_double_clicked = false,
        .delete_pressed = false,
        
        /*
        .mouse_position,
        .mouse_pos_prev,
        .mouse_delta,
        */ // TODO(octave): on peut les initialiser ici en vrai
        
        .mouse_double_click_time = 175.0f,
        .mouse_down_time = -1.0f,
        .right_mouse_down_time = -1.0f,
        .mouse_clicked_time = 0 // TODO(octave): on est sur ?
    };
    
    
    BOOL done = FALSE;
    MSG message;
    
    u64 frame_count = 0;
    i64 last_time = win32_init_timer();
    while(done == FALSE)
    {
        arena_clear(&frame_arena);
        if(frame_count++ % 60 == 0)
        {
            //pour l'instant on cherche pas de nouveau, juste on les update
            swap_plugin_code_if_library_was_modified(definitions, definition_count, &frame_arena, plugins, links, plugin_name_storage);
        }
        
        frame_io.delete_pressed = false;
        
        while(PeekMessage(&message,0,0,0, PM_REMOVE))
        {
            switch (message.message)
            {
                case WM_QUIT : 
                {
                    done = true;
                }break;
                
                case WM_LBUTTONDOWN :
                {
                    
                    frame_io.mouse_down = true;
                }break;
                
                case WM_LBUTTONUP :
                {
                    frame_io.mouse_down = false;
                }break;
                
                
                case WM_RBUTTONDOWN :
                {
                    frame_io.right_mouse_down = true;
                }break;
                
                case WM_RBUTTONUP :
                {
                    frame_io.right_mouse_down = false;
                }break;
                
                case WM_KEYDOWN:{
                    if (message.wParam == VK_BACK || message.wParam == VK_DELETE)
                    {
                        frame_io.delete_pressed = true;
                    }
                }break;
            }
            
            TranslateMessage(&message);
            DispatchMessage(&message);
        }
        
        //~
        //acquire render resources
        if(graphics_ctx.render_target == nullptr)
        {
            printf("recreate target\n");
            RECT rc;
            GetClientRect(window, &rc);
            D2D1_SIZE_U size = D2D1::SizeU(rc.right - rc.left, rc.bottom - rc.top);
            auto success = graphics_ctx.factory->CreateHwndRenderTarget(D2D1::RenderTargetProperties(),
                                                                        D2D1::HwndRenderTargetProperties(window, size, D2D1_PRESENT_OPTIONS_IMMEDIATELY),
                                                                        &graphics_ctx.render_target);
            assert(SUCCEEDED(success));
        }
        if(graphics_ctx.write_factory == nullptr)
        {
            // Create a DirectWrite factory.
            auto hr = DWriteCreateFactory(
                                          DWRITE_FACTORY_TYPE_SHARED,
                                          __uuidof(graphics_ctx.write_factory),
                                          reinterpret_cast<IUnknown **>(&graphics_ctx.write_factory)
                                          );
            assert(SUCCEEDED(hr));
        }
        if(graphics_ctx.text_format == nullptr)
        {
            
            static const WCHAR msc_fontName[] = L"Verdana";
            static const FLOAT msc_fontSize = 14;
            // Create a DirectWrite text format object.
            auto hr = graphics_ctx.write_factory->CreateTextFormat(
                                                                   msc_fontName,
                                                                   NULL,
                                                                   DWRITE_FONT_WEIGHT_NORMAL,
                                                                   DWRITE_FONT_STYLE_NORMAL,
                                                                   DWRITE_FONT_STRETCH_NORMAL,
                                                                   msc_fontSize,
                                                                   L"", //locale
                                                                   &graphics_ctx.text_format
                                                                   );
            
            assert(SUCCEEDED(hr));
            // Center the text horizontally and vertically.
            graphics_ctx.text_format->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            graphics_ctx.text_format->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            
        }
        
        if(graphics_ctx.grey_brush == nullptr)
        {
            auto success = graphics_ctx.render_target->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::LightSlateGray),&graphics_ctx.grey_brush);
            assert(SUCCEEDED(success));
        }
        if(graphics_ctx.dark_brush == nullptr)
        {
            auto success = graphics_ctx.render_target->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::DarkGray),&graphics_ctx.dark_brush);
            assert(SUCCEEDED(success));
        }
        if(graphics_ctx.green_brush == nullptr)
        {
            auto success = graphics_ctx.render_target->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::ForestGreen),&graphics_ctx.green_brush);
            assert(SUCCEEDED(success));
        }
        if(graphics_ctx.red_brush == nullptr)
        {
            auto success = graphics_ctx.render_target->CreateSolidColorBrush(
                                                                             D2D1::ColorF(D2D1::ColorF::DarkRed),
                                                                             &graphics_ctx.red_brush
                                                                             );
            assert(SUCCEEDED(success));
        }
        
        //~
        //frame
        
        frame_io.mouse_position = win32_get_mouse_position(window);
        
        frame_io = frame(window, frame_io, graphics_ctx, &frame_arena, definitions, definition_count, plugins, links);
        ValidateRect(window, NULL);
        
        
        //~
        //release render resource if needed
        auto result = graphics_ctx.render_target->EndDraw();
        if(result == D2DERR_RECREATE_TARGET)
        {
            printf("recreate graphics\n");
            graphics_ctx.render_target->Release();
            graphics_ctx.render_target = nullptr;
            
            graphics_ctx.write_factory->Release();
            graphics_ctx.write_factory = nullptr;
            
            graphics_ctx.text_format->Release();
            graphics_ctx.text_format = nullptr;
            
            graphics_ctx.dark_brush->Release();
            graphics_ctx.dark_brush = nullptr;
            
            graphics_ctx.red_brush->Release();
            graphics_ctx.red_brush = nullptr;
            
            graphics_ctx.green_brush->Release();
            graphics_ctx.green_brush = nullptr;
            
            graphics_ctx.grey_brush->Release();
            graphics_ctx.grey_brush = nullptr;
        }
        
        last_time = win32_pace_60_fps(last_time, counter_frequency, &frame_io.delta_time);
        
    }
    
    return message.message;
}