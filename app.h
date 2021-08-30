
/*
// TODO(octave): vérifier que les allocations se passent bien
TODO faire des sliders 
TODO choper la vraie taille de buffer
TODO menu de creation de plugin

TODO wrap HINSTANCE
TODO utiliser des PluginAction
*/

//#define if_failed(hr, message, ...) if(FAILED(hr)) { printf(message, __VA_ARGS__); return -1;}
#define if_null(hr, message, ...) if(hr == NULL) { printf(message, __VA_ARGS__); return -1;}
#define message_box(message) MessageBox(0, message, 0, MB_OK);

#include "math.h"
#include "assert.h"


Vec2 operator-(const Vec2& lhs, const Vec2& rhs)
{
    return Vec2{lhs.x - rhs.x, lhs.y - rhs.y};
}


Rect shrinked(Rect rect, real32 margin)
{
    assert(margin < rect.dim.x && margin < rect.dim.y);
    return {rect.origin.x + 5.0f,
        rect.origin.y + 5.0f,
        rect.dim.x - 5.0f * 2,
        rect.dim.y - 5.0f * 2};
}

bool contains(Rect rect, Vec2 point)
{
    return point.x > rect.origin.x && point.x < (rect.origin.x + rect.dim.x) &&
        point.y > rect.origin.y && point.y < (rect.origin.y + rect.dim.y);
}


real32 distance_to_line(Vec2 start, Vec2 end, Vec2 point)
{
    real32 x1 = start.x;
    real32 y1 = start.y;
    
    real32 x2 = end.x;
    real32 y2 = end.y;
    
    real32 x3 = point.x;
    real32 y3 = point.y;
    
    
    real32 a = (y1 - y2) / (x1 - x2);
    real32 b = y1 - a * x1;
    
    real32 a2 = - 1 / a;
    real32 b2 = y3 - a2 * x3;
    real32 x = (b2 - b) / (a - a2);
    real32 y = a * x + b;
    
    return sqrt((x3 - x)*(x3 - x) + (y3 - y)*(y3 - y));
};


const static real32 pin_dim = 10.0f;

const static real32 plugin_width = 90.0f;
const static real32 plugin_height = 40.0f;

Rect default_plugin_rect(real32 x, real32 y)
{
    return Rect{x, y, plugin_width, plugin_height};
}

Rect plugin_to_inlet(const Plugin& plugin, u8 inlet_idx)
{
    return {plugin.bounds.origin.x + 20 * inlet_idx,
        plugin.bounds.origin.y - pin_dim,
        pin_dim,
        pin_dim};
}
Rect plugin_to_outlet(const Plugin& plugin, u8 outlet_idx)
{
    return
    {plugin.bounds.origin.x + 15 + 20 * outlet_idx,
        plugin.bounds.origin.y + plugin.bounds.dim.x,
        pin_dim,
        pin_dim};
}


Rect soundcard_input_outlet(Vec2 position)
{
    return {position.x + 15, position.y - pin_dim, pin_dim, pin_dim};
}

Rect soundcard_output_inlet(Vec2 position)
{
    return {position.x + 15, position.y + plugin_height, pin_dim, pin_dim};
}

Vec2 soundcard_input_position = { 50,100};
Vec2 soundcard_output_position = {200,150};


MouseDownInteractionState mouse_down_interaction = {};
Selection selected;


Pool<Plugin> plugins;
Pool<Link> links;


/*
struct PluginAction{
    enum class action_type{
        create, remove, create_connection, remove_connection, none
    } action_type;

    union{
        struct {
            const char *name;
            PluginDefinition* definition;
        } create;
        struct {
            u16 id;
        } remove;
        struct {
            u16 source, destination;
        } create_connection;
        struct {
            u16 source, destination;
        } remove_connection;
    };

};
*/


const static u16 plugin_provision = 64;

void reallocate_plugindef_memory_chunk(PluginDefinition& definition, PluginParameters& new_parameters, AudioParameters& audio)
{
    free(definition.memory.chunk_base);
    free(definition.memory.in_use);
    
    u64 audio_buffer_byte_size = sizeof(float) * audio.num_samples * audio.num_channels;
    
    u64 footprint = audio_buffer_byte_size * (new_parameters.inlet_count + new_parameters.outlet_count)
        + new_parameters.bloat_size
        + sizeof(bool) * (new_parameters.inlet_count + new_parameters.outlet_count);
    //inlet buffers            | outlet buffers             | bloat
    
    definition.memory.plugin_footprint = footprint;
    definition.memory.chunk_base = calloc(plugin_provision, footprint);
    definition.memory.in_use = (bool*)calloc(plugin_provision, sizeof(bool));
}

void deallocate_all_memory_chunks(PluginDefinition *definitions, u16 definition_count)
{
    for(u64 i = 0; i < definition_count; i++)
    {
        free(definitions[i].memory.chunk_base);
        free(definitions[i].memory.in_use);
    }
}

void allocate_all_memory_chunks(PluginDefinition *definitions, u16 definition_count, AudioParameters& audio)
{
    for(u64 i = 0; i < definition_count; i++)
    {
        
        auto& definition = definitions[i];
        u64 audio_buffer_byte_size = sizeof(float) * audio.num_samples * audio.num_channels;
        
        u64 footprint =
            audio_buffer_byte_size * (definition.inlet_count + definition.outlet_count)
            + definition.bloat_size
            + sizeof(bool) * (definition.inlet_count + definition.outlet_count);
        //inlet buffers            | outlet buffers             | bloat           |is_connected_input             | is_connected_output
        
        definition.memory.plugin_footprint = footprint; // TODO(octave): est-ce qu'on a besoin de lui ?
        definition.memory.chunk_base = calloc(plugin_provision, footprint);
        definition.memory.in_use = (bool*)calloc(plugin_provision, sizeof(bool));
        //in_use                   | memory                     |
    }
}


Plugin create_plugin(u64 definition_idx,
                     PluginDefinition& definition,
                     AudioParameters& audio,
                     Rect bounds)
{
    Plugin new_plugin;
    new_plugin.bounds = bounds;
    new_plugin.definition_idx = definition_idx;
    new_plugin.bloat_size = definition.bloat_size;
    new_plugin.inlet_count = definition.inlet_count;
    new_plugin.outlet_count = definition.outlet_count;
    new_plugin.render = definition.render_A;
    
    i64 memory_idx = -1;
    
    for(u32 i = 0; i  < definition.memory.allocated; i++)
    {
        if(definition.memory.in_use[i] == false)
        {
            memory_idx = i;
            definition.memory.in_use[i] = true;
            break;
        }
    }
    assert(memory_idx != -1);
    // TODO(octave): si il faut étendre le nombre de plugins alloués
    
    u64 audio_buffer_byte_size = sizeof(float) * audio.num_samples * audio.num_channels;
    
    new_plugin.inlet_buffers = (real32*)((u64)definition.memory.chunk_base + definition.memory.plugin_footprint * memory_idx);
    new_plugin.outlet_buffers = (real32*)((u64)new_plugin.inlet_buffers + audio_buffer_byte_size * definition.inlet_count);
    new_plugin.bloat = (void*)((u64)new_plugin.outlet_buffers + audio_buffer_byte_size * definition.outlet_count);
    new_plugin.inlet_connected_to_input = (bool*)((u64)new_plugin.bloat + definition.bloat_size);
    new_plugin.outlet_connected_to_output = (bool*)((u64)new_plugin.inlet_connected_to_input + sizeof(bool) * definition.inlet_count);
    
    return new_plugin;
}

void search_and_load_plugins(PluginDefinition* definitions,
                             u16& count,
                             StringStorage& plugin_name_storage)
{
    os.create_directory(ConstString("temp"));
    
    String dll_filenames[1024]={};
    u64 dll_file_count = os.enumerate_matching_filenames(ConstString("*.dll"), dll_filenames, &plugin_name_storage);
    
    for(int i = 0; i < dll_file_count; i++)
    {
        String dll_filename = dll_filenames[i];
        HINSTANCE source_library = os.load_library(dll_filename);
        
        if (source_library != 0)
        {
            auto get_parameters_fn =
            (get_plugin_parameter_t)os.get_library_function(source_library, ConstString("get_plugin_parameters"));
            auto render_ptr =
            (render_t) os.get_library_function(source_library, ConstString("render"));
            
            
            if(get_parameters_fn &&
               render_ptr)
            {
                
                PluginParameters parameters = get_parameters_fn(AudioParameters{});
                
                String plugin_name = string_storage_copy_c_string(&plugin_name_storage, parameters.name);
                String original_filename = string_storage_copy_c_string(&plugin_name_storage, dll_filename.str);
                
                //without extension
                String without_extension{};
                without_extension.str = plugin_name_storage.position;
                without_extension.size = original_filename.size - 4; //.dll
                memcpy(without_extension.str, original_filename.str, original_filename.size - 5); //.dll + null
                without_extension.str[without_extension.size - 1] = 0; //null
                plugin_name_storage.position += without_extension.size;
                
                //temp/original_filename_A.dll
                String temp_filename_A;
                temp_filename_A.size = original_filename.size + 16;
                temp_filename_A.str = plugin_name_storage.position;
                sprintf(temp_filename_A.str,"temp/%s_A_temp.dll", original_filename.str);
                plugin_name_storage.position += temp_filename_A.size;
                
                os.copy_file(dll_filename, temp_filename_A, true);
                HINSTANCE loaded_library = os.load_library(temp_filename_A);
                
                u64 modification_time = os.get_last_modified_time(dll_filename);
                
                {
                    auto& new_definition = definitions[count++];
                    
                    new_definition.inlet_count        = parameters.inlet_count;
                    new_definition.outlet_count       = parameters.outlet_count;
                    new_definition.current_library    = PluginDefinition::A;
                    new_definition.bloat_size         = parameters.bloat_size;
                    new_definition.get_parameters_fn  = get_parameters_fn;
                    new_definition.render_A           = (render_t)os.get_library_function (loaded_library, ConstString("render"));
                    new_definition.library_A          = loaded_library;
                    new_definition.plugin_name        = plugin_name;
                    new_definition.original_filename  = original_filename;
                    new_definition.original_filename_without_extension   = without_extension;
                    new_definition.temp_filename_A    = temp_filename_A;
                    new_definition.last_modified_time = modification_time;
                }
            }
            os.free_library(source_library);
        }
    }
}

void swap_plugin_code_if_library_was_modified(PluginDefinition* plugin_descriptors,
                                              u16 descriptor_count,
                                              Arena* arena)
{
    void* arena_base = (void*)((u64)arena->base + arena->position);
    bool *should_update_topology = (bool *)arena_push(arena, sizeof(bool) * descriptor_count);
    bool *should_update_render = (bool *)arena_push(arena, sizeof(bool) * descriptor_count);
    
    for(int i = 0; i < descriptor_count; i++)
    {
        auto& descriptor = plugin_descriptors[i];
        
        i64 modification_time = os.get_last_modified_time(descriptor.original_filename);
        
        if (modification_time != descriptor.last_modified_time && modification_time != 0) // TODO(octave): vérifier
        {
            
            os.free_library(descriptor.library_A);
            
            if(!os.copy_file(descriptor.original_filename, descriptor.temp_filename_A, true))
            {
                break;
            }
            
            
            HINSTANCE dll = os.load_library(descriptor.temp_filename_A);
            if (dll == nullptr)
            {
                assert(false); //refactor c'est pas une façon de gérer des erreurs
                break;
            }
            
            
            descriptor.library_A = dll;
            descriptor.render_A = (render_t)os.get_library_function(dll, ConstString("render"));
            should_update_render[i] = true;
            descriptor.last_modified_time = modification_time;
            
            {
                
                auto get_parameters_fn = (get_plugin_parameter_t)os.get_library_function(dll, ConstString("get_plugin_parameters"));
                
                PluginParameters new_parameters = get_parameters_fn(AudioParameters{});// TODO(octave): error
                
                if(strcmp(new_parameters.name, descriptor.plugin_name.str) == 0 &&
                   new_parameters.bloat_size == descriptor.bloat_size &&
                   new_parameters.inlet_count == descriptor.inlet_count &&
                   new_parameters.outlet_count == descriptor.outlet_count)
                {
                    descriptor.inlet_count = new_parameters.inlet_count;
                    descriptor.outlet_count = new_parameters.outlet_count;
                    descriptor.bloat_size = new_parameters.bloat_size;
                    // TODO(octave): update name
                    should_update_topology[i] = false;
                }
                else
                {
                    should_update_topology[i] = true;
                }
            }
            
        }
        else
        {
            should_update_render[i] = false;
            should_update_topology[i] = false;
        }
    }
    
    for(u64 i = 0; i < plugins.size; i++)
    {
        if(!plugins.in_use[i]) continue;
        
        auto& plugin = plugins.array[i];
        u64 definition_idx = plugin.definition_idx;
        auto& definition = plugin_descriptors[definition_idx];
        
        if(should_update_render[definition_idx])
        {
            plugin.render = definition.render_A;
        }
        if(should_update_topology[definition_idx])
        {
            plugin.bloat_size = definition.bloat_size;
            plugin.inlet_count = definition.inlet_count;
            plugin.outlet_count = definition.outlet_count;
            plugin.name = definition.plugin_name;
            // TODO(octave): vide les buffers, on supprime les links ou on les marque je sais pas
            // TODO(octave): on recréé le bloat!
        }
    }
    arena_pop_at(arena, arena_base);
}

// TODO(octave): generer le graph linéarisé
bool sort(Pool<Link>& links, Pool<Plugin>& nodes, Link new_link, Arena* arena)
{
    bool is_a_dag = false;
    
    void* arena_base = (void*)((u64)arena->base + arena->position);
    u64 node_count_in_use = nodes.count_in_use();
    
    u64 *in_degree = (u64*)arena_push(arena, sizeof(u64) * nodes.size);
    for(u64  i = 0; i < links.size; i++)
    {
        if(links.in_use[i])
        {
            u64 dest_plugin_idx = links.array[i].dest_plugin_idx;
            in_degree[dest_plugin_idx]++;
        }
    }
    
    {
        in_degree[new_link.dest_plugin_idx]++;
    }
    
    u64 *empty_node_indices = (u64*)arena_push(arena, sizeof(u64) * node_count_in_use);
    u64 empty_node_count = 0;
    
    for(u64 node_idx = 0; node_idx < nodes.size; node_idx++)
    {
        if(nodes.in_use[node_idx] && in_degree[node_idx] == 0)
        {
            in_degree[node_idx] = -1;
            empty_node_indices[empty_node_count++] = node_idx;
        }
    }
    
    if(empty_node_count != 0)
    {
        
        u64 *sorted_node_indices = (u64*)arena_push(arena, sizeof(u64) * node_count_in_use);
        u64 sorted_node_count = 0;
        while(empty_node_count != 0)
        {
            u64 empty_node_idx = empty_node_indices[--empty_node_count];
            sorted_node_indices[sorted_node_count++] = empty_node_idx;
            
            //NOTE c'est pas du tout efficient comme algorithme ????
            for(u64 link_idx = 0; link_idx < links.size; link_idx++)
            {
                auto& link = links.array[link_idx];
                if(link.source_plugin_idx == empty_node_idx)
                {
                    u64 dest_node_idx = link.dest_plugin_idx;
                    in_degree[dest_node_idx]--;
                    if(in_degree[dest_node_idx] == 0)
                    {
                        in_degree[dest_node_idx] = -1;
                        empty_node_indices[empty_node_count++] = dest_node_idx;
                    }
                }
            }
            
            if(new_link.source_plugin_idx == empty_node_idx)
            {
                u64 dest_node_idx = new_link.dest_plugin_idx;
                in_degree[dest_node_idx]--;
                if(in_degree[dest_node_idx] == 0)
                {
                    in_degree[dest_node_idx] = -1;
                    empty_node_indices[empty_node_count++] = dest_node_idx;
                }
            }
        }
        
        if(sorted_node_count == node_count_in_use)
            is_a_dag = true;
    }
    
    
    arena_pop_at(arena, arena_base);
    return is_a_dag;
}// TODO(octave): mettre une arena pour gérer la mémoire




IO frame(HWND window, 
         IO io, GraphicsContext& graphics_ctx, 
         Arena *frame_arena,
         PluginDefinition *definitions, u64 definition_count)
{
    // TODO(octave): y a un bug bizarre si je créé une messagebox, je vais pas recevoir le mouse_up event, et du coup ça flingue ma state machine, qui croit que mouse est toujours down
    
    
    //io state machine
    {
        io.time += io.delta_time;
        
        if(io.mouse_position.x < 0.0f && io.mouse_position.y < 0.0f)
            io.mouse_position = Vec2(-99999.0f, -99999.0f);
        
        //?
        if((io.mouse_position.x < 0.0f && io.mouse_position.y < 0.0f) ||
           (io.mouse_pos_prev.x < 0.0f && io.mouse_pos_prev.y < 0.0f))
            io.mouse_delta = Vec2{0.0f, 0.0f};
        else
            io.mouse_delta = io.mouse_position - io.mouse_pos_prev;
        io.mouse_pos_prev = io.mouse_position;
        
        io.mouse_released = !io.mouse_down && io.mouse_down_time >= 0.0f;
        
        
        if(io.mouse_down)
        {
            if(io.mouse_down_time < 0.0f)
            {
                io.mouse_down_time = 0.1f;
                io.mouse_clicked = true;
                
            }
            else
            {
                io.mouse_down_time += 0.2f;// io.delta_time;
                io.mouse_clicked = false;
            }
        }
        else
        {
            io.mouse_down_time = -1.0f;
            io.mouse_clicked = false;
        }
        
        
        io.mouse_double_clicked = false;
        if(io.mouse_clicked)
        {
            if(io.time - io.mouse_clicked_time < io.mouse_double_click_time)
            {
                io.mouse_double_clicked = true;
                io.mouse_clicked_time = -1000000.0f;
            }
            else
            {
                io.mouse_clicked_time = io.time;
            }
        }
        
        
        if(io.right_mouse_down)
        {
            if(io.right_mouse_down_time < 0.0f)
            {
                io.right_mouse_down_time = 0.1f;
                io.right_mouse_clicked = true;
            }
            else
            {
                io.right_mouse_down_time += io.delta_time;
                io.right_mouse_clicked = false;
            }
        }
        else
        {
            io.right_mouse_down_time = -1.0f;
            io.right_mouse_clicked = false;
        }
    }
    //D2D1_SIZE_F target_size = render_target->GetSize();
    
    //~
    //find hovered element
    HoverState hovered;
    
    
    auto soundcard_input_bounds = default_plugin_rect(soundcard_input_position.x, soundcard_input_position.y);
    auto soundcard_output_bounds = default_plugin_rect(soundcard_output_position.x, soundcard_output_position.y);
    
    auto soundcard_input_outlet_pin = soundcard_input_outlet(soundcard_input_position);
    auto soundcard_output_inlet_pin = soundcard_output_inlet(soundcard_output_position);
    
    if(contains(soundcard_input_bounds, io.mouse_position))
    {
        hovered.element = HoverState::SoundCardInput;
        
    }
    else if(contains(soundcard_output_bounds, io.mouse_position))
    {
        hovered.element = HoverState::SoundCardOutput;
    }
    else if(contains(soundcard_input_outlet_pin, io.mouse_position))
    {
        hovered.element = HoverState::SoundCardInputOutlet;
    }
    else if(contains(soundcard_output_inlet_pin, io.mouse_position))
    {
        hovered.element = HoverState::SoundCardOutputInlet;
    }
    else
    {
        for(auto i = 0; i < plugins.size && hovered.element == HoverState::None; i++)
        {
            if(!plugins.in_use[i]) continue;
            
            auto& plugin = plugins.array[i];
            
            if(contains(plugin.bounds, io.mouse_position))
            {
                hovered.element = HoverState::Plugin;
                hovered.plugin_idx = i;
            }
            
            for(auto inlet_idx = 0; inlet_idx < plugin.inlet_count && hovered.element == HoverState::None; inlet_idx++)
            {
                if(contains(plugin_to_inlet(plugin, inlet_idx), io.mouse_position))
                {
                    hovered.element = HoverState::PluginInlet;
                    hovered.plugin_idx = i;
                    hovered.pin_idx = inlet_idx;
                }
            }
            
            for(auto outlet_idx = 0; outlet_idx < plugin.outlet_count && hovered.element == HoverState::None; outlet_idx++)
            {
                if(contains(plugin_to_outlet(plugin, outlet_idx), io.mouse_position))
                {
                    
                    hovered.element = HoverState::PluginOutlet;
                    hovered.plugin_idx = i;
                    hovered.pin_idx = outlet_idx;
                }
            }
        }
        
        if(hovered.element == HoverState::None)
        {
            i16 closest_link_idx = -1;
            real32 closest_link_distance = 9999.0f;
            
            for(auto i = 0; i < links.size; i++)
            {
                if(!links.in_use[i]) continue;
                auto& link = links.array[i];
                
                const auto& source_plugin = plugins.array[link.source_plugin_idx];
                const auto& destination_plugin = plugins.array[link.dest_plugin_idx];
                
                auto start = plugin_to_outlet(source_plugin, link.source_outlet_idx);
                auto end = plugin_to_inlet(destination_plugin, link.dest_inlet_idx);
                
                
                real32 distance = distance_to_line(start.origin, end.origin, io.mouse_position);
                if(distance < closest_link_distance)
                {
                    closest_link_distance = distance;
                    closest_link_idx = i;
                }
            }
            if(closest_link_distance < 10.0f)
            {
                hovered.element = HoverState::Link;
                hovered.link_idx = closest_link_idx;
            }
        }
    }
    
    
    
    
    
    graphics_ctx.render_target->BeginDraw();
    graphics_ctx.render_target->SetTransform(D2D1::Matrix3x2F::Identity());
    graphics_ctx.render_target->Clear(D2D1::ColorF(D2D1::ColorF::White));
    
    
    //~
    //draw
    {
        auto color = (selected.type == Selection::SoundCardInput)
            ? Color_Green : Color_Red;
        
        os.fill_rectangle(soundcard_input_bounds, Color_Grey, graphics_ctx);
        
        auto inner = shrinked(soundcard_input_bounds, 5.0f);
        os.fill_rectangle(inner, color, graphics_ctx);
        
        os.draw_text(ConstString("input"), 40, inner, Color_Dark, graphics_ctx);
    }
    
    {
        auto color = (selected.type == Selection::SoundCardOutput)
            ? Color_Green : Color_Red;
        
        os.fill_rectangle(soundcard_output_bounds, Color_Grey, graphics_ctx);
        
        auto inner = shrinked(soundcard_output_bounds, 5.0f);
        os.fill_rectangle(inner, color, graphics_ctx);
        
        os.draw_text(ConstString("output"), 40, inner, Color_Dark, graphics_ctx);
    }
    
    {
        auto color = hovered.element == HoverState::SoundCardInputOutlet ? Color_Green : Color_Red;
        os.fill_rectangle(soundcard_input_outlet_pin, color, graphics_ctx);
    }
    
    {
        auto color = hovered.element == HoverState::SoundCardOutputInlet ? Color_Green : Color_Red;
        os.fill_rectangle(soundcard_output_inlet_pin, color, graphics_ctx);
    }
    
    for(auto i = 0; i < plugins.size; i++)
    {
        if(!plugins.in_use[i]) continue;
        
        auto& plugin = plugins.array[i];
        Rect& bounds = plugin.bounds;
        {
            
            auto color = (selected.type == Selection::Node && selected.idx == i)
                ? Color_Green : Color_Red;
            
            os.fill_rectangle(bounds, Color_Grey, graphics_ctx);
            
            auto inner = shrinked(bounds, 5.0f);
            os.fill_rectangle(inner, color, graphics_ctx);
            
            os.draw_text(plugin.name, 40, inner, Color_Dark, graphics_ctx);
        }
        
        for(auto inlet_idx = 0; inlet_idx < plugin.inlet_count; inlet_idx++)
        {
            auto color = 
                hovered.element == HoverState::PluginInlet
                && hovered.plugin_idx == i 
                && hovered.pin_idx == inlet_idx
                ? Color_Green : Color_Red;
            
            auto inlet_bounds = plugin_to_inlet(plugin, inlet_idx);
            os.fill_rectangle(inlet_bounds, color, graphics_ctx);
            
            if(plugin.inlet_connected_to_input[inlet_idx])
            {
                os.draw_line(inlet_bounds.origin, soundcard_input_outlet_pin.origin, color, 2.0f, graphics_ctx);
            }
        }
        
        for(auto outlet_idx = 0; outlet_idx < plugin.outlet_count; outlet_idx++)
        {
            
            auto color = 
                hovered.element == HoverState::PluginOutlet
                && hovered.plugin_idx == i 
                && hovered.pin_idx == outlet_idx
                ? Color_Green : Color_Red;
            
            auto outlet_bounds = plugin_to_outlet(plugin, outlet_idx);
            os.fill_rectangle(outlet_bounds, color, graphics_ctx);
            
            if(plugin.outlet_connected_to_output[outlet_idx])
            {
                os.draw_line(outlet_bounds.origin, soundcard_output_inlet_pin.origin, color, 2.0f, graphics_ctx);
            }
        }
        
        
    }
    
    for(auto i = 0; i < links.size; i++)
    {
        
        if(!links.in_use[i]) continue;
        const auto& link = links.array[i];
        
        const auto& source_plugin = plugins.array[link.source_plugin_idx];
        const auto& destination_plugin = plugins.array[link.dest_plugin_idx];
        
        auto start = plugin_to_outlet(source_plugin, link.source_outlet_idx);
        auto end = plugin_to_inlet(destination_plugin, link.dest_inlet_idx);
        
        // TODO(octave): transformer rect en vec2*vec2
        
        
        auto color = (selected.type == Selection::Link && selected.idx == i)
            ? Color_Green : Color_Red;
        
        os.draw_line(start.origin, end.origin, color, 2.0f, graphics_ctx);
    }
    
    
    //~
    //interaction overlay (the new connection for now)
    if(mouse_down_interaction.type == MouseDownInteractionType::CreatingLink)
    {
        
        if(mouse_down_interaction.clicked_pin.type == PinType::SoundCardInputOutlet)
        {
            
            os.draw_line(soundcard_input_outlet_pin.origin,
                         io.mouse_position,
                         Color_Grey,
                         4.0f,
                         graphics_ctx);
        }
        
        else if(mouse_down_interaction.clicked_pin.type == PinType::SoundCardOutputInlet)
        {
            
            os.draw_line(soundcard_output_inlet_pin.origin,
                         io.mouse_position,
                         Color_Grey,
                         4.0f,
                         graphics_ctx);
        }
        
        else if(mouse_down_interaction.clicked_pin.type == PinType::Inlet)
        {
            auto& clicked_plugin = plugins.array[mouse_down_interaction.clicked_pin.plugin_idx];
            auto clicked_pin_idx = mouse_down_interaction.clicked_pin.pin_idx;
            
            auto inlet = plugin_to_inlet(clicked_plugin, clicked_pin_idx);
            
            os.draw_line(inlet.origin,
                         io.mouse_position,
                         Color_Grey,
                         4.0f,
                         graphics_ctx);
        }
        
        else if(mouse_down_interaction.clicked_pin.type == PinType::Outlet)
        {
            auto& clicked_plugin = plugins.array[mouse_down_interaction.clicked_pin.plugin_idx];
            auto clicked_pin_idx = mouse_down_interaction.clicked_pin.pin_idx;
            
            auto outlet = plugin_to_outlet(clicked_plugin, clicked_pin_idx);
            
            os.draw_line(outlet.origin,
                         io.mouse_position,
                         Color_Grey,4.0f,
                         graphics_ctx);
        }
    }
    else if(mouse_down_interaction.type == MouseDownInteractionType::PluginMenu)
    {
        Vec2 dim = {30.0f,40.0f};
        Vec2 origin = mouse_down_interaction.plugin_menu_origin;
        Rect menu_bounds = {origin, {dim.x, dim.y * definition_count}};
        os.fill_rectangle(menu_bounds, Color_Red, graphics_ctx);
        for(u64 i = 0; i < definition_count; i++)
        {
            Rect bounds = { 
                {origin.x, origin.y + dim.y * i},
                dim};
            os.draw_text(definitions[i].plugin_name, 12, bounds, Color_Green, graphics_ctx);
        }
    }
    
    
    
    //~
    //interaction state machine
    {
        if(io.mouse_clicked)
        {
            switch(hovered.element)
            {
                case HoverState::SoundCardInputOutlet :
                {
                    MouseDownInteractionState new_state;
                    new_state.type = MouseDownInteractionType::CreatingLink;
                    new_state.clicked_pin.type = PinType::SoundCardInputOutlet;
                    mouse_down_interaction = new_state;
                    
                    selected.type = Selection::None;
                }break;
                
                case HoverState::SoundCardOutputInlet :
                {
                    MouseDownInteractionState new_state;
                    new_state.type = MouseDownInteractionType::CreatingLink;
                    new_state.clicked_pin.type = PinType::SoundCardOutputInlet;
                    mouse_down_interaction = new_state;
                    
                    selected.type = Selection::None;
                }break;
                
                case HoverState::PluginInlet :
                {
                    MouseDownInteractionState new_state;
                    new_state.type = MouseDownInteractionType::CreatingLink;
                    new_state.clicked_pin = {
                        PinType::Inlet,
                        hovered.plugin_idx,
                        (u8)hovered.pin_idx
                    };
                    mouse_down_interaction = new_state;
                    
                    selected.type = Selection::None;
                }break;
                
                case HoverState::PluginOutlet :
                {
                    MouseDownInteractionState new_state;
                    new_state.type = MouseDownInteractionType::CreatingLink;
                    new_state.clicked_pin = {
                        PinType::Outlet,
                        hovered.plugin_idx,
                        hovered.pin_idx
                    };
                    mouse_down_interaction = new_state;
                    
                    selected.type = Selection::None;
                }break;
                
                case HoverState::Link :
                {
                    if(mouse_down_interaction.type == MouseDownInteractionType::None)
                    {
                        MouseDownInteractionState new_state;
                        new_state.type = MouseDownInteractionType::Link;
                        new_state.clicked_link_idx = hovered.link_idx;
                        
                        mouse_down_interaction = new_state;
                        
                        selected = {Selection::Link, hovered.link_idx};
                    }
                }break;
                
                case HoverState::SoundCardInput :
                {
                    
                    if(mouse_down_interaction.type == MouseDownInteractionType::None) // TODO(octave): ça sert à quoi ce check
                    {
                        MouseDownInteractionState new_state;
                        new_state.type = MouseDownInteractionType::SoundCardInput;
                        mouse_down_interaction = new_state;
                        
                        selected.type = Selection::SoundCardInput;
                    }
                }break;
                
                case HoverState::SoundCardOutput :
                {
                    
                    if(mouse_down_interaction.type == MouseDownInteractionType::None) // TODO(octave): ça sert à quoi ce check
                    {
                        MouseDownInteractionState new_state;
                        new_state.type = MouseDownInteractionType::SoundCardOutput;
                        mouse_down_interaction = new_state;
                        
                        selected.type = Selection::SoundCardOutput;
                    }
                }break;
                
                case HoverState::Plugin :
                {
                    if(mouse_down_interaction.type == MouseDownInteractionType::None)
                    {
                        MouseDownInteractionState new_state;
                        new_state.type = MouseDownInteractionType::Node;
                        new_state.clicked_plugin_idx = hovered.plugin_idx;
                        
                        mouse_down_interaction = new_state;
                        
                        selected = {Selection::Node, hovered.plugin_idx};
                    }
                }break;
                
                default :
                {
                    selected.type = Selection::None;
                }break;
            }
            
        }
        
        else if(io.right_mouse_clicked)
        {
            if(hovered.element == HoverState::None)
            {
                mouse_down_interaction.type = MouseDownInteractionType::PluginMenu;
            }
        }
        
        
        switch(mouse_down_interaction.type)
        {
            case MouseDownInteractionType::None :
            {
                
            }break;
            
            case MouseDownInteractionType::CreatingLink :
            {
                if(io.mouse_released)
                {
                    //plugin to plugin
                    {
                        // TODO(octave): déplacer ça vers les actions à la fin
                        struct Link new_link{};
                        
                        bool should_create_plugin_to_plugin_link = false;
                        if(hovered.element == HoverState::PluginOutlet && mouse_down_interaction.clicked_pin.type == PinType::Inlet)
                        {
                            
                            new_link.source_plugin_idx = hovered.plugin_idx;
                            new_link.source_outlet_idx = hovered.pin_idx;
                            new_link.dest_plugin_idx = mouse_down_interaction.clicked_pin.plugin_idx;
                            new_link.dest_inlet_idx  = mouse_down_interaction.clicked_pin.pin_idx;
                            should_create_plugin_to_plugin_link = true;
                        }
                        
                        else if(hovered.element == HoverState::PluginInlet && mouse_down_interaction.clicked_pin.type == PinType::Outlet)
                        {
                            new_link.dest_plugin_idx = hovered.plugin_idx;
                            new_link.dest_inlet_idx = hovered.pin_idx;
                            new_link.source_plugin_idx = mouse_down_interaction.clicked_pin.plugin_idx;
                            new_link.source_outlet_idx  = mouse_down_interaction.clicked_pin.pin_idx;
                            should_create_plugin_to_plugin_link = true;
                        }
                        
                        if(should_create_plugin_to_plugin_link)
                        {
                            bool duplicate_exists = false;
                            for(int link_idx = 0; link_idx < links.size; link_idx++)
                            {
                                auto& link = links.array[link_idx];
                                if(link == new_link)
                                {
                                    duplicate_exists = true;
                                    break;
                                }
                            }
                            if(duplicate_exists == false)
                            {
                                bool is_a_dag = sort(links, plugins, new_link, frame_arena);
                                if(is_a_dag)
                                    links.insert(new_link);
                            }
                        }
                    }
                    
                    //plugin to soundcard input
                    {
                        bool should_connect_to_soundcard_input = false;
                        u64 plugin_idx;
                        u64 inlet_idx;
                        if(hovered.element == HoverState::PluginInlet && mouse_down_interaction.clicked_pin.type == PinType::SoundCardInputOutlet)
                        {
                            plugin_idx = hovered.plugin_idx;
                            inlet_idx = hovered.pin_idx;
                        }
                        
                        else if(hovered.element == HoverState::SoundCardInputOutlet && mouse_down_interaction.clicked_pin.type == PinType::Inlet)
                        {
                            plugin_idx = mouse_down_interaction.clicked_pin.plugin_idx;
                            inlet_idx = mouse_down_interaction.clicked_pin.pin_idx;
                        }
                        
                        if(should_connect_to_soundcard_input)
                        {
                            Plugin& plugin = plugins.array[plugin_idx];
                            plugin.inlet_connected_to_input[inlet_idx] = true;
                        }
                    }
                    
                    {
                        bool should_connect_to_soundcard_output = false;
                        u64 plugin_idx;
                        u64 outlet_idx;
                        if(hovered.element == HoverState::PluginOutlet && mouse_down_interaction.clicked_pin.type == PinType::SoundCardOutputInlet)
                        {
                            plugin_idx = hovered.plugin_idx;
                            outlet_idx = hovered.pin_idx;
                        }
                        else if(hovered.element == HoverState::SoundCardOutputInlet && mouse_down_interaction.clicked_pin.type == PinType::Outlet)
                        {
                            plugin_idx = mouse_down_interaction.clicked_pin.plugin_idx;
                            outlet_idx = mouse_down_interaction.clicked_pin.pin_idx;
                        }
                        
                        if(should_connect_to_soundcard_output)
                        {
                            Plugin& plugin = plugins.array[plugin_idx];
                            plugin.outlet_connected_to_output[outlet_idx] = true;
                        }
                    }
                    mouse_down_interaction.type = MouseDownInteractionType::None;
                }
            }break;
            
            case MouseDownInteractionType::SoundCardInput:{
                //translate
                soundcard_input_position.x += io.mouse_delta.x;
                soundcard_input_position.y += io.mouse_delta.y;
                
                if(io.mouse_released)
                    mouse_down_interaction.type = MouseDownInteractionType::None;
            }break;
            
            case MouseDownInteractionType::SoundCardOutput:{
                //translate
                soundcard_output_position.x += io.mouse_delta.x;
                soundcard_output_position.y += io.mouse_delta.y;
                
                if(io.mouse_released)
                    mouse_down_interaction.type = MouseDownInteractionType::None;
            }break;
            case MouseDownInteractionType::Node:{
                //translate
                plugins.array[mouse_down_interaction.clicked_plugin_idx].bounds.origin.x += io.mouse_delta.x;
                plugins.array[mouse_down_interaction.clicked_plugin_idx].bounds.origin.y += io.mouse_delta.y;
                
                if(io.mouse_released)
                    mouse_down_interaction.type = MouseDownInteractionType::None;
            }break;
            case MouseDownInteractionType::Link:{
                if(io.mouse_released)
                    mouse_down_interaction.type = MouseDownInteractionType::None;
            }break;
            case MouseDownInteractionType::PluginMenu:{
                if(io.mouse_clicked)
                {
                    Vec2 dim = {30.0f,40.0f};
                    Vec2 origin = mouse_down_interaction.plugin_menu_origin;
                    Rect menu_bounds = {origin, {dim.x, dim.y * definition_count}};
                    
                    if(contains(menu_bounds,io.mouse_position))
                    {
                        printf("create plugin\n");
                        mouse_down_interaction.type = MouseDownInteractionType::None;
                    }
                    else 
                        mouse_down_interaction.type = MouseDownInteractionType::None;
                }
            }break;
        }
    }
    
    
    
    // TODO(octave): je sais pas si c'est le bon endroit pour mettre ça, c'est à la fin de la frame, on est sûr que ça se fait dans le bon sens ?
    if(io.delete_pressed)
    {
        if(selected.type == Selection::Node)
        {
            selected.type = Selection::None;
            
            Plugin& plugin = plugins.array[selected.idx];
            PluginDefinition& definition = definitions[plugin.definition_idx];
            definition.memory.in_use[selected.idx] = false;
            
            plugins.remove(selected.idx);
            for(int i = 0; i < links.size; i++)
            {
                if(links.in_use[i])
                {
                    auto& link = links.array[i];
                    if(link.dest_plugin_idx == selected.idx || link.source_plugin_idx == selected.idx)
                        links.remove(i);
                }
            }
        }
        else if(selected.type == Selection::Link)
        {
            links.remove(selected.idx);
            selected.type = Selection::None;
        }
    }
    
    //dispatch_action(action, &plugins);
    return io;
};
