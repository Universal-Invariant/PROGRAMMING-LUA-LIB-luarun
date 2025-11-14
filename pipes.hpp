
#define LUARUN_NAMED_PIPE_MT "luarun.NamedPipe"

/*
    In luarun/lua pipes are wrapped so they are safer to use
*/

// Structure to hold named pipe information
typedef struct {
    BOOL success;
    DWORD error_code;       // Set if success is FALSE
    HANDLE hPipe;           // Set if success is TRUE
    std::string pipeName;   // Set if success is TRUE
    BOOL isClosed;           // If pipe is closed
} Pipe;





// --- Pure C Function: create_named_pipe_server ---
// Creates a new named pipe server.
// Returns a PipeResult struct. If result.success is TRUE, caller owns the HANDLE result.hPipe.
Pipe create_named_pipe(const std::string& pipeName, const std::string& mode, int wait_mode) {
    Pipe result = { 0 };
    result.success = FALSE;
    result.hPipe = INVALID_HANDLE_VALUE;
    result.error_code = 0;
    result.pipeName = pipeName; // Store the name even on failure if needed for logging

    std::string fullPipeName = "\\\\.\\pipe\\" + pipeName;

    DWORD dwOpenMode = 0;
    if (mode == "w") {
        dwOpenMode = PIPE_ACCESS_OUTBOUND;
    }
    else if (mode == "r") {
        dwOpenMode = PIPE_ACCESS_INBOUND;
    }
    else { // "rw" or default
        dwOpenMode = PIPE_ACCESS_DUPLEX;
    }

    DWORD dwPipeMode = PIPE_TYPE_BYTE | PIPE_READMODE_BYTE;
    if (wait_mode == -2) {
        dwPipeMode |= PIPE_NOWAIT;
    }
    else {
        dwPipeMode |= PIPE_WAIT;
    }

    HANDLE hPipe = CreateNamedPipeA(
        fullPipeName.c_str(),
        dwOpenMode,
        dwPipeMode,
        10, // nMaxInstances
        BUFSIZE, // nOutBufferSize
        BUFSIZE, // nInBufferSize
        0,       // nDefaultTimeOut
        NULL     // lpSecurityAttributes
    );

    /*
    Error
        230 ERROR_BAD_PIPE
        231 ERROR_PIPE_BUSY
        233 ERROR_PIPE_NOT_CONNECTED
    */


    if (hPipe == INVALID_HANDLE_VALUE) {
        result.error_code = GetLastError();
        return result;
    }

    // Success
    result.success = TRUE;
    result.hPipe = hPipe;
    // result.pipeName is already set
    return result;
}







// --- Lua C Function: create_named_pipe(pipe_name, [mode], [wait_mode])
//
// Lua C wrapper for create_named_pipe_server. Do not chain, use lua_call
static int l_create_named_pipe(lua_State* L) {
    const char* pipeName = luaL_checkstring(L, 1);
    const char* mode = luaL_optstring(L, 2, "rw");
    int wait_mode = (int)luaL_optinteger(L, 3, -2); // -2 = default (non-blocking)

    Pipe result = create_named_pipe(std::string(pipeName), std::string(mode), wait_mode);

    if (!result.success) {
        lua_pushnil(L);
        lua_pushstring(L, FormatErrorMessage("CreateNamedPipe failed: %lu (%s).", result.error_code).c_str());
        return 2;
    }

    // Allocate raw memory block for the Pipe structure, and fill out data
    void* raw_ud = lua_newuserdata(L, sizeof(Pipe));
    Pipe* ud = new (raw_ud) Pipe(); 
    ud->success = result.success;
    ud->error_code = result.error_code;
    ud->hPipe = result.hPipe;
    ud->pipeName = result.pipeName;
    ud->isClosed = FALSE; // Initially open

    // Set the metatable for the userdata
    luaL_getmetatable(L, LUARUN_NAMED_PIPE_MT);
    if (lua_isnil(L, -1)) {
        lua_pop(L, 1); // Remove the nil
        lua_pushnil(L);
        lua_pushstring(L, "Metatable for NamedPipe not found");
        return 2;
    }

    lua_setmetatable(L, -2); // Assign the metatable to the userdata    
    return 1; // Return the userdata object
}



// --- Pure C Function: open_named_pipe_client ---
// Opens an existing named pipe client.
// Returns a PipeResult struct. If result.success is TRUE, caller owns the HANDLE result.hPipe.
Pipe open_named_pipe(const std::string& pipeName, const std::string& mode, int wait_mode) {
    Pipe result = { 0 };
    result.success = FALSE;
    result.hPipe = INVALID_HANDLE_VALUE;
    result.error_code = 0;
    result.pipeName = pipeName; // Store the name even on failure if needed for logging

    std::string fullPipeName = "\\\\.\\pipe\\" + pipeName;

    HANDLE hPipe = INVALID_HANDLE_VALUE;
    DWORD desired_access = 0;

    if (mode == "w") {
        desired_access = GENERIC_WRITE;
    }
    else if (mode == "r") {
        desired_access = GENERIC_READ;
    }
    else { // "rw" or default
        desired_access = GENERIC_READ | GENERIC_WRITE;
    }

    DWORD dwPipeMode = PIPE_TYPE_BYTE;
    if (wait_mode == -2) {
        dwPipeMode |= PIPE_NOWAIT;
    }
    else {
        dwPipeMode |= PIPE_WAIT;
    }

    hPipe = CreateFileA(
        fullPipeName.c_str(),
        desired_access,
        dwPipeMode,
        NULL,
        OPEN_EXISTING,
        0,
        NULL
    );

    if (hPipe == INVALID_HANDLE_VALUE) {
        result.error_code = GetLastError();
        return result;
    }

    // Success
    result.success = TRUE;
    result.hPipe = hPipe;
    // result.pipeName is already set
    return result;
}




// --- Lua C Function: open_named_pipe(pipe_name, [mode], [wait_mode])
//
// Lua C wrapper for open_named_pipe_client.
static int l_open_named_pipe(lua_State* L) {
    const char* pipeName = luaL_checkstring(L, 1);
    const char* mode = luaL_optstring(L, 2, "rw");
    int wait_mode = (int)luaL_optinteger(L, 3, -2); // -2 = default (non-blocking)

    Pipe result = open_named_pipe(std::string(pipeName), std::string(mode), wait_mode);

    if (!result.success) {
        lua_pushnil(L);
        lua_pushfstring(L, "Failed to open named pipe (CreateFileA): %d", result.error_code);
        return 2;
    }

    // Create the userdata object using the handle from the C function
    void* raw_ud = lua_newuserdata(L, sizeof(Pipe));
    Pipe* ud = new (raw_ud) Pipe();
    ud->success = result.success;
    ud->error_code = result.error_code;
    ud->hPipe = result.hPipe;
    ud->pipeName = result.pipeName;
    ud->isClosed = FALSE; // Initially open

    // Set the metatable for the userdata
    luaL_getmetatable(L, LUARUN_NAMED_PIPE_MT);
    if (lua_isnil(L, -1)) {
        lua_pop(L, 1); // Remove the nil
        lua_pushnil(L);
        lua_pushstring(L, "Metatable for NamedPipe not found");
        return 2;
    }
    lua_setmetatable(L, -2); // Assign the metatable to the userdata

    return 1; // Return the userdata object
}




// Helper function to check and get the PipeUserData from the Lua stack
static Pipe* check_pipe_userdata(lua_State* L, int arg_index) {
    void* ud = luaL_checkudata(L, arg_index, LUARUN_NAMED_PIPE_MT);    
    luaL_argcheck(L, ud != NULL, arg_index, "NamedPipe expected");
    return (Pipe*)ud;
}




// LuaJIT doesn't have 64-bit numbers so use hex-strings intead.
static int l_get_pipe_handle(lua_State* L) {
    Pipe* ud = check_pipe_userdata(L, 1);
    char handle_str[32]; 
    sprintf_s(handle_str, sizeof(handle_str), "0x%p", ud->hPipe); 
    lua_pushstring(L, handle_str);
    return 1;
}


static int l_write_pipe(lua_State* L) {
    Pipe* ud = check_pipe_userdata(L, 1); 
    if (ud->isClosed) { lua_pushboolean(L, 0); lua_pushstring(L, "Cannot write to closed pipe handle"); return 2; }    
    return lc<l_write_handle>(L, ud->hPipe, luaL_checkstring(L, 2));
}

static int l_peek_pipe(lua_State* L) {
    Pipe* ud = check_pipe_userdata(L, 1);
    if (ud->isClosed) { lua_pushinteger(L, 0); return 1; }
    return lc<l_peek_handle>(L, ud->hPipe, (int)luaL_optinteger(L, 2, 0));
}

static int l_read_pipe(lua_State* L) {
    Pipe* ud = check_pipe_userdata(L, 1); 
    if (ud->isClosed) { lua_pushstring(L, ""); return 1; }
    auto n = lc<l_read_handle>(L, ud->hPipe, (int)luaL_optinteger(L, 2, BUFSIZE), (int)luaL_optinteger(L, 3, 0));
    return n;
}

static int l_close_pipe(lua_State* L) {
    Pipe* ud = check_pipe_userdata(L, 1);
    if (ud->isClosed) { lua_pushboolean(L, 1); return 1; }
    int num_results = lc<l_close_handle>(L, ud->hPipe);
    ud->hPipe = INVALID_HANDLE_VALUE;
    ud->isClosed = 1;     
    return num_results;
}

static int l_is_running_pipe(lua_State* L) {
    Pipe* ud = check_pipe_userdata(L, 1); 
    if (ud->isClosed) { lua_pushboolean(L, 0); return 1; }
    return lc<l_is_running_process>(L, ud->hPipe); 
}

static int l_wait_pipe(lua_State* L) {
    Pipe* ud = check_pipe_userdata(L, 1); 
    if (ud->isClosed) { lua_pushinteger(L, 0); lua_pushstring(L, "Cannot wait on closed pipe handle"); return 2; }
    return lc<l_wait_process>(L, ud->hPipe);
}

static int l_terminate_pipe(lua_State* L) {
    Pipe* ud = check_pipe_userdata(L, 1);
    if (ud->isClosed) { lua_pushboolean(L, 0); lua_pushstring(L, "Cannot terminate closed pipe handle"); return 2; }
    return lc<l_terminate_process>(L, ud->hPipe);
}

static int l_isSuccess_pipe(lua_State* L) {
    Pipe* ud = check_pipe_userdata(L, 1);
    lua_pushboolean(L, ud->success);
    return 1;
}

static int l_error_code_pipe(lua_State* L) {
    Pipe* ud = check_pipe_userdata(L, 1);
    lua_pushinteger(L, ud->error_code);
    return 1;
}

static int l_handle_pipe(lua_State* L) {
    l_get_pipe_handle(L);
    return 1;
}

static int l_name_pipe(lua_State* L) {
    Pipe* ud = check_pipe_userdata(L, 1);
    lua_pushstring(L, ud->pipeName.c_str());
    return 1;
}

static int l_isClosed_pipe(lua_State* L) {
    Pipe* ud = check_pipe_userdata(L, 1);
    lua_pushboolean(L, ud->isClosed);
    return 1;
}













// --- Registration Function ---
// This function should be called once during initialization of the pipe module
// to register the metatable containing ALL the methods.
int register_named_pipe_metatable(lua_State* L) {
    // 1. Create and register the metatable for the Pipe userdata
    luaL_newmetatable(L, LUARUN_NAMED_PIPE_MT); // Stack: metatable

    // Set its own "__index" to point to itself, so methods are found
    lua_pushvalue(L, -1); // Stack: metatable, metatable
    lua_setfield(L, -2, "__index"); // metatable.__index = metatable

    // Register ALL the methods into the metatable
    // This is the crucial part: ALL functions intended as methods must be listed here.
    static const luaL_Reg pipe_methods[] = {
        {"handle", l_get_pipe_handle},
        {"write", l_write_pipe},      
        {"peek", l_peek_pipe},
        {"read", l_read_pipe},
        {"close", l_close_pipe},              
        {"running", l_is_running_pipe},
        {"wait", l_wait_pipe},
        {"terminate", l_terminate_pipe},        

        {"isSuccess", l_isSuccess_pipe},
        {"error_code", l_error_code_pipe},
        {"handle", l_handle_pipe},
        {"name", l_name_pipe},
        {"isClosed", l_isClosed_pipe },

        {NULL, NULL} // Sentinel
    };

    #if LUAJIT
        for (const luaL_Reg* reg = pipe_methods; reg->name; reg++) {
            lua_pushcfunction(L, reg->func); // Push the C function
            lua_setfield(L, -2, reg->name); // Set it as a field in the metatable (index -2)
        }
    #else
        luaL_setfuncs(L, pipe_methods, 0); // Registers methods onto the metatable at stack top
    #endif

    lua_pop(L, 1); // Pop the metatable (it's now registered)

    return 0; // No values returned to Lua
}