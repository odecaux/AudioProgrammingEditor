/* date = August 26th 2021 0:01 pm */

#ifndef STRUCTS_H
#define STRUCTS_H



typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef unsigned long long u64;

typedef char i8;
typedef short i16;
typedef int i32;
typedef long long i64;

typedef float real32;
typedef double real64;

#define Kilobytes(count) (u64)(count*(u64)1024)
#define Megabytes(count) (u64)(count*Kilobytes(1024))
#define Gigabytes(count) (u64)(count*Megabytes(1024))

#define COMMIT_BLOCK_SIZE (Kilobytes(4))




struct SystemArena
{
    void *base;
    u64 capacity;
    u64 allocated_position;
    u64 commit_position;
    
};


struct Arena
{
    void* base;
    u64 capacity;
    u64 position;
};


// TODO(octave): trouver comment éviter la fragmentation comment retirer des noms.
struct StringStorage
{
    char* base;
    u64 capacity;
    char* position;
    
    
    
    /*
char* storage_buffer
    String* strings; //pointe vers le storage
    bool* in_use
    
    remove(u64 idx)
    {
                    string_idx[idx] = 0
        in_use[idx] = false
            
        for(string_idx)
{
on shift en avant le storage à droite de la string qu'on supprime
toutes les string qui pointent à droite on les redécale vers la nouvelle position
}

        }
            
            
        */
};




#define ConstString(text) String{(char *)text, sizeof(text)}

struct String
{
    char* str;
    u64 size;
};


enum Color
{
    Color_Grey,
    Color_Red,
    Color_Green,
    Color_Dark
};


struct Vec2{
    real32 x, y;
};


struct Rect{
    Vec2 origin;
    Vec2 dim;
    //real32 x, y, w, h;
};


struct OS
{
    HINSTANCE (*load_library)(String) ;
    void (*free_library)(HINSTANCE);
    bool (*create_directory)(String);
    void*(*get_library_function)(HINSTANCE, String);
    u64 (*get_last_modified_time)(String);
    bool (*copy_file)(String, String, bool) ;
    void (*error_window)(String, String) ;
    i64 (*init_timer)();
    i64 (*pace_60_fps)(i64, LARGE_INTEGER, real32*);
    void (*fill_rectangle)(Rect, Color, GraphicsContext&);
    void (*draw_text)(const String&, i32, Rect, Color, GraphicsContext&) ;
    void (*draw_line)(Vec2, Vec2, Color, real32, GraphicsContext&) ;
    u64 (*enumerate_matching_filenames)(String, String*, StringStorage*);
    void* (*reserve)(u64);
    void (*commit)(void*, u64);
    void (*release)(void*);
};


struct PluginParameters
{
    u32 inlet_count;
    u32 outlet_count;
    u64 bloat_size;
    char name[1024] ;
};

struct AudioParameters
{
    float sample_rate;
    unsigned int num_channels;
    unsigned int num_samples;
};


typedef void (*render_t)(real32*, real32*, u32);
typedef PluginParameters(*get_plugin_parameter_t)(AudioParameters);


// TODO(octave): choper la fonction pour générer le bloat

struct PluginDefinition{
    u32 inlet_count;
    u32 outlet_count; //le cast ça va pas se passer bizarrement ? c'est bien du u8 que ressort le dll ?
    u64 bloat_size;
    
    struct 
    {
        u64 allocated;
        void* chunk_base;
        bool* in_use;
        u64 plugin_footprint;
    } memory;
    
    String plugin_name;
    String original_filename;
    String original_filename_without_extension;
    
    get_plugin_parameter_t get_parameters_fn;
    
    enum {A, B} current_library;
    render_t render_A;
    render_t render_B;
    
    HINSTANCE library_A;
    HINSTANCE library_B;
    String temp_filename_A;
    u64 last_modified_time;
};




static const u16 max_plugin_count = 1024;



struct Plugin{
    Rect bounds;
    u64 definition_idx; 
    
    String name;
    u64 bloat_size;
    
    u32 inlet_count;
    u32 outlet_count;
    render_t render;
    
    real32 *inlet_buffers;
    real32 *outlet_buffers;
    void *bloat;
    bool *inlet_connected_to_input;
    bool *outlet_connected_to_output;
};

struct Link{
    i64 source_plugin_idx;
    i64 source_outlet_idx;
    i64 dest_plugin_idx;
    i64 dest_inlet_idx;
    
    bool operator==(const Link& other) const = default;
};



enum class MouseDownInteractionType
{
    None, 
    Node,
    SoundCardInput,
    SoundCardOutput,
    Link,
    CreatingLink,
    PluginMenu
};

enum class PinType
{
    Inlet, 
    Outlet, 
    SoundCardInputOutlet, 
    SoundCardOutputInlet, 
    None
};

struct HoverState
{
    enum {
        Plugin, 
        PluginInlet, 
        PluginOutlet, 
        Link, 
        SoundCardInput, 
        SoundCardOutput, 
        SoundCardInputOutlet, 
        SoundCardOutputInlet, 
        None
    } element = None;
    
    u64 plugin_idx;
    u64 pin_idx;
    u64 link_idx;
};
struct Selection{
    enum { Node, Link, SoundCardInput, SoundCardOutput, None} type = None;
    u64 idx;
};


//TODO c'est pas ça pcq y aura aussi le menu de séléction
struct MouseDownInteractionState{
    MouseDownInteractionType type = MouseDownInteractionType::None;
    union
    {
        u64 clicked_plugin_idx;
        u64 clicked_link_idx;
        struct {
            PinType type;
            u64 plugin_idx;
            u64 pin_idx;
        } clicked_pin;
        Vec2 plugin_menu_origin;
    };
};


const int capacity = 256;
template<typename T>
struct Pool
{
    T    array[capacity];
    bool in_use[capacity];
    
    u64 free_list[capacity];
    
    u64 size = 0;                    // TODO(octave): euh c'est u8 ?
    u64 free_count = 0;
    
    void insert(const T& new_element)
    {
        if(free_count == 0) 
        {
            assert(size < capacity);
            in_use[size] = true;
            array[size] = new_element;
            size++;
        }
        else
        {
            auto free_index = free_list[free_count--];
            in_use[free_index] = true;
            array[free_index] = new_element;
        }
    }
    
    void remove(u64 idx)
    {
        assert(in_use[idx]);
        free_list[free_count++] = idx;
        in_use[idx] = false;
    }
    
    void clear()
    {
        memset(in_use, 0, capacity);
        size = 0;
        free_count = 0;
    }
    
    u64 count_in_use()
    {
        u64 count = 0;
        for(u64 i = 0; i < size; i++)
        {
            if(in_use[i])
                count++;
        }
        return count;
    }
};

struct IO
{
    real32 delta_time;
    real32 time;
    
    bool mouse_down;
    bool mouse_clicked;
    bool mouse_released;
    
    bool right_mouse_down;
    bool mouse_double_clicked;
    bool right_mouse_clicked;
    
    bool delete_pressed;
    
    
    Vec2 mouse_position;
    Vec2 mouse_pos_prev;
    Vec2 mouse_delta;
    
    real32 mouse_double_click_time;
    real32 mouse_down_time;
    real32 right_mouse_down_time;
    real32 mouse_clicked_time;
};


#endif //STRUCTS_H
