/* date = August 27th 2021 10:08 am */

#ifndef PLUGIN_HEADER_H
#define PLUGIN_HEADER_H


struct PluginParameters
{
    char name[1024];
    unsigned int inlet_count;
    unsigned int outlet_count;
    unsigned long long bloat_size;
};

struct AudioParameters
{
    float sample_rate;
    unsigned int num_channels;
    unsigned int num_samples;
};

#endif //PLUGIN_HEADER_H