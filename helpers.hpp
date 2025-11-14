


// Helper: Push error message and return nil, error
static int push_error(lua_State* L, const char* msg) {
    lua_pushnil(L);
    lua_pushstring(L, msg);
    return 2;
}


// Converts an std::string to an std::wstring
inline std::wstring convert(const std::string& as)
{
    // deal with trivial case of empty string
    if (as.empty())    return std::wstring();

    // determine required length of new string
    size_t reqLength = ::MultiByteToWideChar(CP_UTF8, 0, as.c_str(), (int)as.length(), 0, 0);

    // construct new string of required length
    std::wstring ret(reqLength, L'\0');

    // convert old string to new string
    ::MultiByteToWideChar(CP_UTF8, 0, as.c_str(), (int)as.length(), &ret[0], (int)ret.length());

    // return new string ( compiler should optimize this away )
    return ret;
}



/*
* Creates a formated Error message using format. Use %s for the error string and %lu for the error code.
*/
std::wstring FormatErrorMessage(std::wstring format)
{
    DWORD last_error = GetLastError();
    // Convert error code to human-readable message
    LPSTR message_buffer = NULL;
    FormatMessageA(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        NULL,
        last_error,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        (LPSTR)&message_buffer,
        0,
        NULL
    );

    wchar_t error_msg[1024];
    if (message_buffer) {
        _snwprintf_s(error_msg, sizeof(error_msg), format.c_str(), message_buffer, last_error);
        LocalFree(message_buffer);
    }
    else {
        _snwprintf_s(error_msg, sizeof(error_msg), format.c_str(), last_error);
    }

    return std::wstring(error_msg);
}

/*
* Creates a formated Error message using format. Use %s for the error string and %lu for the error code.
*/
std::string FormatErrorMessage(std::string format, DWORD error = 0)
{
    if (error == 0) error = GetLastError();

    // Convert error code to human-readable message
    LPSTR message_buffer = NULL;
    FormatMessageA(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        NULL,
        error,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        (LPSTR)&message_buffer,
        0,
        NULL
    );

    std::string system_message;
    if (message_buffer) {
        system_message = std::string(message_buffer);
        LocalFree(message_buffer); // Free the buffer allocated by FormatMessageA
    }
    else {
        system_message = "Unknown error";
    }

    // Find the positions of %lu and %s in the format string
    size_t pos_lu = format.find("%lu");
    size_t pos_s = format.find("%s");

    char error_msg[1024];
    if (pos_lu != std::string::npos && pos_s != std::string::npos) {
        // Both %lu and %s found
        if (pos_lu < pos_s) {
            // %lu comes before %s -> snprintf expects error_code first, then message_string
            snprintf(error_msg, sizeof(error_msg), format.c_str(), error, system_message.c_str());
        }
        else {
            // %s comes before %lu -> snprintf expects message_string first, then error_code
            snprintf(error_msg, sizeof(error_msg), format.c_str(), system_message.c_str(), error);
        }
    }
    else if (pos_lu != std::string::npos) {
        // Only %lu found -> snprintf expects error_code
        snprintf(error_msg, sizeof(error_msg), format.c_str(), error);
    }
    else if (pos_s != std::string::npos) {
        // Only %s found -> snprintf expects message_string
        snprintf(error_msg, sizeof(error_msg), format.c_str(), system_message.c_str());
    }
    else {
        // Neither %lu nor %s found, just print the format string as is
        snprintf(error_msg, sizeof(error_msg), "%s", format.c_str());
    }

    return std::string(error_msg);
}


// Helper: Convert UTF-8 string to wide string
std::wstring utf8_to_wide_string(const char* utf8_str, lua_State* L = NULL ) {
    if (!utf8_str) return NULL;
    int wide_len = MultiByteToWideChar(CP_UTF8, 0, utf8_str, -1, NULL, 0);
    if (wide_len == 0) return NULL;
    wchar_t* wide_str = (wchar_t*)malloc(wide_len * sizeof(wchar_t));
    if (!wide_str) {
        if (L != NULL) luaL_error(L, "Memory allocation failed for wide string conversion");
        return NULL; 
    }
    MultiByteToWideChar(CP_UTF8, 0, utf8_str, -1, wide_str, wide_len);
    auto str = std::wstring(wide_str);
    free(wide_str);
    return str;
}


std::string wide_to_utf8_string(const wchar_t* wide_str, lua_State* L = NULL) {
    if (!wide_str) return NULL;
    int utf8_len = WideCharToMultiByte(CP_UTF8, 0, wide_str, -1, NULL, 0, NULL, NULL);
    if (utf8_len == 0) return NULL;
    char* utf8_str = (char*)malloc(utf8_len);
    if (!utf8_str) {
        if (L != NULL) luaL_error(L, "Memory allocation failed for wide string conversion");
        return NULL; 
    }
    WideCharToMultiByte(CP_UTF8, 0, wide_str, -1, utf8_str, utf8_len, NULL, NULL);
    auto str = std::string(utf8_str);
    free(utf8_str);
    return str;
}

// Helper: Check if a key exists in a vector of "KEY=VALUE" strings
static bool env_has_key(const std::vector<std::string>& env, const std::string& key) {
    for (const auto& pair : env) {
        if (pair.substr(0, key.size()) == key && pair.size() > key.size() && pair[key.size()] == '=') {
            return true;
        }
    }
    return false;
}

// Helper: Set or override a key in environment vector
static void env_set(std::vector<std::string>& env, const std::string& key, const std::string& value) {
    std::string new_pair = key + "=" + value;
    for (auto& pair : env) {
        if (pair.substr(0, key.size()) == key && pair.size() > key.size() && pair[key.size()] == '=') {
            pair = new_pair;
            return;
        }
    }
    env.push_back(new_pair);
}

// Build environment block by merging parent + Lua table
static LPWSTR build_merged_environment(lua_State* L, int env_table_index) {
    // 1. Get current environment block
    LPWCH parent_env = GetEnvironmentStringsW();
    if (!parent_env) return NULL;

    std::unordered_map<std::string, std::string> env_map;
    env_map.clear(); // redundant but safe

    // 2. Convert to UTF-8 vector of "KEY=VALUE"
    std::vector<std::string> env_pairs;
    LPWSTR ptr = parent_env;
    while (*ptr) {
        auto utf8 = wide_to_utf8_string(ptr, L);
        env_pairs.push_back(utf8);

        ptr += wcslen(ptr) + 1;
    }
    FreeEnvironmentStringsW(parent_env);


    // 3. Merge with Lua table
    if (lua_type(L, env_table_index) == LUA_TTABLE) {
        // Push nil to start iteration
        lua_pushnil(L);
        while (lua_next(L, env_table_index) != 0) {
            // -2 = key, -1 = value
            const char* key = luaL_checkstring(L, -2);
            const char* val = luaL_checkstring(L, -1);
            if (key && val) {
                env_set(env_pairs, std::string(key), std::string(val));
            }
            // Pop the value, leave key for next iteration
            lua_pop(L, 1);
        }
    }

    // 4. Convert back to wide string block: KEY=VALUE\0...\0\0
    std::vector<std::wstring> wide_pairs;
    for (const auto& pair : env_pairs) {
        auto w = utf8_to_wide_string(pair.c_str(), L);
        wide_pairs.push_back(w);            
        
    }

    // 5. Compute total size in wchar_t needed: sum(len + 1) for each wide pair + 1 final null
    size_t total_wchars = 1; // for final double-null terminator
    for (const auto& wp : wide_pairs) {
        total_wchars += wp.length() + 1;
    }

    LPWSTR merged_env = (LPWSTR)calloc(total_wchars, sizeof(wchar_t));
    if (!merged_env) return NULL;

    wchar_t* out = merged_env;
    size_t remaining = total_wchars;

    for (const auto& wp : wide_pairs) {
        size_t len = wp.length();
        if (len + 1 > remaining) {
            break; // safety
        }
        wcscpy_s(out, remaining, wp.c_str());
        out += len + 1;
        remaining -= len + 1;
    }
    // Final null is already zero due to calloc, but ensure termination:
    *out = L'\0';



    return merged_env;
}






void print_lua_stack(lua_State* L, const char* context_label) {
    // Build the debug string
    char debug_buffer[4096]; // Make sure it's large enough
    int offset = 0;
    if (context_label) {
        offset += snprintf(debug_buffer + offset, sizeof(debug_buffer) - offset, "=== Lua Stack Debug (%s) ===\n", context_label);
    }
    else {
        offset += snprintf(debug_buffer + offset, sizeof(debug_buffer) - offset, "=== Lua Stack Debug ===\n");
    }

    int top = lua_gettop(L);
    offset += snprintf(debug_buffer + offset, sizeof(debug_buffer) - offset, "Stack Size: %d\n", top);

    if (top == 0) {
        offset += snprintf(debug_buffer + offset, sizeof(debug_buffer) - offset, "  <Stack is empty>\n");
        offset += snprintf(debug_buffer + offset, sizeof(debug_buffer) - offset, "=============================\n");
        OutputDebugStringA(debug_buffer);
        return;
    }

    for (int i = 1; i <= top; i++) {
        const char* type_name = lua_typename(L, lua_type(L, i));
        lua_pushvalue(L, i);
        const char* str_rep = lua_tostring(L, -1);
        // Ensure the string doesn't contain % to avoid issues with OutputDebugString formatting
        // Also ensure the line fits in the remaining buffer space
        int written = snprintf(debug_buffer + offset, sizeof(debug_buffer) - offset, "  Index %2d: Type = %-10s, Value = %s\n", i, type_name, str_rep);
        if (written < 0 || written >= (int)(sizeof(debug_buffer) - offset)) {
            // Handle buffer overflow if necessary
            break;
        }
        offset += written;
        lua_pop(L, 2); // Pop string and copied value
    }
    snprintf(debug_buffer + offset, sizeof(debug_buffer) - offset, "=============================\n");

    OutputDebugStringA(debug_buffer); // Send to Windows Debug Output
    printf(debug_buffer);
}
