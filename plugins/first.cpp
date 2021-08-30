
extern "C"{
    int __declspec( dllexport ) get_inlet_count() {
        return 2;
    }
    
    int __declspec( dllexport ) get_outlet_count(){
        return 3;
    }
    
    
    void __declspec( dllexport ) get_name(char* out_buffer){
        if(out_buffer == nullptr)
            return; //c'est tout ?
        // TODO(not-set): y se passe quoi si get_name veut dépasser la taille du buffer ?
        char name[] = "first_plugin_test";
        char *source = name;
        
        while (*source != '\0')
        {
            *out_buffer = *source;
            out_buffer++;
            source++;
        }
        
        *out_buffer = '\0';
    }
    
    
    void render(real32* input, real32* output, u8 channel_count)
    {
        for(auto i = 0; i < channel_count; i++)
            output[i] = 0.0f;
    }
}