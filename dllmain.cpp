#ifdef LUAJIT
#pragma comment(lib, "lua51.lib")
//#define LUA_BUILD_AS_DLL
#else
#pragma comment(lib, "lua54.lib")
#endif

#define BUFSIZE 32768


#ifdef _WIN32
#include <windows.h>
#include <string>
#include <vector>
#include <psapi.h>
#include <tlhelp32.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <comdef.h>
#include <process.h>
#include <stdio.h>
#include <unordered_map>
#include <tuple>
#include <type_traits>
#include <utility> 


#else
#include <sys/wait.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#endif

#ifdef LUAJIT
extern "C" {
    #include "includejit\luaconf.h"
    #include "includejit\lua.h"
    #include "includejit\lauxlib.h"
    #include "includejit\lualib.h"    
}
static_assert(LUA_VERSION_NUM == 502, "Must use LuaJIT 5.1 headers");
#else
extern "C" {
    #include "include\lua.h"
    #include "include\lauxlib.h"
    #include "include\lualib.h"
}
#endif



#include "luastack.hpp"
#include "helpers.hpp"
#include "handles.hpp"
#include "process.hpp"
#include "pipes.hpp"
#include "keyboard.hpp"







static const luaL_Reg process_lib[] = {
    // Process functions
    {"create_process", l_create_process},    
    
    // Handle functions    
    {"write_handle", l_write_handle},
    {"peek_handle", l_peek_handle},
    {"read_handle", l_read_handle},
    {"close_handle", l_close_handle},
    {"is_process_running", l_is_running_process},
    {"terminate", l_terminate_process},
    {"wait_process", l_wait_process},

        
    // Pipe functions
    {"create_named_pipe", l_create_named_pipe},
    {"open_named_pipe", l_open_named_pipe},
    {"get_pipe_handle", l_get_pipe_handle},        
    {"write_pipe", l_write_pipe},
    {"peek_pipe", l_peek_pipe},
    {"read_pipe", l_read_pipe},
    {"close_pipe", l_close_pipe},
    {"is_running_pipe", l_is_running_pipe},
    {"wait_pipe", l_wait_pipe},
    {"terminate_pipe", l_terminate_pipe},
    
    
    // Keyboard functions
    {"get_key_press", l_get_key_press},
    {"get_key_press_unicode", l_get_key_press_unicode},


    {NULL, NULL}
};

#ifdef LUAJIT

extern "C" __declspec(dllexport) int luaopen_luarunJIT(lua_State* L) {
    luaL_register(L, "luarunJIT", process_lib);    
    register_named_pipe_metatable(L);
    return 1;
}

#else

extern "C" __declspec(dllexport) int luaopen_luarun(lua_State* L) {
    luaL_newlib(L, process_lib);
    register_named_pipe_metatable(L);
    return 1;
}
#endif