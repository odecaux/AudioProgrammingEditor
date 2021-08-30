/* date = August 24th 2021 11:09 pm */


u64 c_string_length(char* c_string)
{
    return strlen(c_string);
}


String String_from_c_string_size(char* c_string, u64 size)
{
    String string;
    string.str = c_string;
    string.size = size;
    return string;
}

String String_from_c_string(char* c_string)
{
    return String_from_c_string_size(c_string, c_string_length(c_string));
}

