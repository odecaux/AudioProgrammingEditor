/* date = August 24th 2021 11:50 pm */

#ifndef MEMORY_H
#define MEMORY_H


SystemArena system_arena_create(void)
{
    SystemArena arena = {};
    arena.capacity = Gigabytes(4);
    arena.commit_position = 0;
    arena.base = os.reserve(arena.capacity);
    return arena;
}

void* system_arena_push(SystemArena *arena, u64 size)
{
    void* out;
    if(arena->allocated_position + size > arena->commit_position) //>= ....
    {
        u64 a = size + COMMIT_BLOCK_SIZE - 1;
        u64 block_count = a / COMMIT_BLOCK_SIZE;
        u64 bytes_to_commit = block_count * COMMIT_BLOCK_SIZE ;
        os.commit((u8 *)arena->base + arena->commit_position, bytes_to_commit);
        arena->commit_position += bytes_to_commit;
        assert(arena->commit_position < arena->capacity); 
    }
    out = (u8*) arena->base + arena->allocated_position;
    arena->allocated_position += size;
    return out;
}

void system_arena_pop(SystemArena *arena, u64 size)
{
    arena->allocated_position -= size;
    assert(arena->allocated_position >= 0);
}

void system_arena_clear(SystemArena *arena)
{
    arena->allocated_position = 0;
}

void system_arena_release(SystemArena *arena)
{
    os.release(arena->base);
}

Arena arena_create_from_system_arena(SystemArena* system_arena, u64 size)
{
    Arena arena{};
    arena.capacity = size;
    arena.position = 0;
    arena.base = system_arena_push(system_arena, size);
    return arena;
}

// TODO(octave): aligner sur cachline
void* arena_push(Arena* arena, u64 size)
{
    assert(arena->position + size <= arena->capacity);
    void* memory_to_give = (void*)((u64)arena->base + (u64)arena->position);
    arena->position += size;
    return memory_to_give;
}

void* arena_get_current_ptr(Arena *arena)
{
    return (void*)((u64)arena->base + (u64)arena->position);
}

void arena_pop_at(Arena* arena, void* new_position)
{
    assert(new_position >= arena->base);
    assert((u64)new_position <= (u64)arena->base + arena->position);
    arena->position = (u64)new_position - (u64)arena->base;
}

void arena_pop(Arena *arena, u64 size)
{
    assert(size <= arena->position);
    arena->position -= size;
}

void arena_clear(Arena *arena)
{
    arena->position = 0;
}

StringStorage string_storage_from_system_arena(SystemArena *arena, u64 storage_size)
{
    StringStorage storage{};
    storage.capacity = storage_size;
    storage.base = (char*) system_arena_push(arena, storage_size);
    //memset(storage.base,0, storage_size);
    storage.position = storage.base;
    return storage;
}


// TODO(octave): mettre une size maximum
String string_storage_copy_c_string(StringStorage* storage, char* c_string)
{
    assert(c_string != 0);
    assert(storage->position >= storage->base); //???
    
    u64 position_idx = (u64)(storage->position - storage->base);
    
    u64 i = 0;
    while(i + position_idx < storage->capacity - 1 && c_string[i] != 0)
    {
        storage->base[position_idx + i] = c_string[i]; 
        i++;
    }
    
    storage->base[position_idx + i] = 0;
    String pushed_string = {storage->base + position_idx, i + 1};
    storage->position += i + 1;
    return pushed_string;
}

void string_storage_clear(StringStorage* storage)
{
    storage->position = 0;
}

String string_storage_push_format(StringStorage* storage, const char *const format, ...)
{
    u64 remaining_capacity = storage->capacity - (u64)(storage->position - storage->base);
    
    String new_string;
    new_string.str = storage->position;
    
    va_list args;
    va_start(args, format);
    u64 wanted_length = vsnprintf(storage->position, remaining_capacity, format, args);
    va_end(args);
    
    
    if(wanted_length + 1 <= remaining_capacity)
        new_string.size = wanted_length + 1;
    else
        new_string.size = remaining_capacity;
    storage->position += new_string.size;
    
    return new_string;
}

#endif //MEMORY_H
