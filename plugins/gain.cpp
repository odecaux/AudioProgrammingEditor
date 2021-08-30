
extern "C"{
    int __declspec( dllexport ) get_inlet_count() {
        return 1;
    }
    
    int __declspec( dllexport ) get_outlet_count(){
        return 1;
    }
    
    void __declspec( dllexport ) get_name(char* out_buffer){
        if(out_buffer == nullptr)
            return; //c'est tout ?
        // TODO(not-set): y se passe quoi si get_name veut dépasser la taille du buffer ?
        char name[] = "izi";
        char *source = name;
        
        while (*source != '\0')
        {
            *out_buffer = *source;
            out_buffer++;
            source++;
        }
        
        *out_buffer = '\0';
    }
    
    unsigned int __declspec( dllexport ) bloat_size(unsigned int channel_count)
    {
        return 0;
    }
    
    
    void __declspec( dllexport ) render(real32* input, real32* output, unsigned int channel_count)
    {
        for(auto i = 0; i < channel_count; i++)
            output[i] = 10.0f * input[i];
    }
}