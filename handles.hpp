

// Structure to hold the result of a read operation
typedef struct {
    BOOL success;
    DWORD error_code; // Set if success is FALSE
    char* data;       // Allocated buffer containing data, NULL if failure
    DWORD data_len;   // Length of data in the buffer
    BOOL pipe_broken; // Flag indicating ERROR_BROKEN_PIPE was encountered
} ReadResult;





// --- Pure C Function: write_to_handle ---
// Writes data to a Windows handle.
// Returns TRUE on success, FALSE on failure. Sets *error_code if FALSE.
BOOL write_handle(HANDLE hHandle, const char* data, size_t len, DWORD* error_code) {
    if (!hHandle || hHandle == INVALID_HANDLE_VALUE) { return FALSE; }
    DWORD bytes_written = 0;
    BOOL success = WriteFile(hHandle, data, (DWORD)len, &bytes_written, NULL);

    if (!success) {
        *error_code = GetLastError();
        return FALSE;
    }

    if (bytes_written != len) {
        return FALSE;
    }

    // IMPORTANT: Flush the pipe to ensure data is sent immediately
    //FlushFileBuffers(hHandle);
    return TRUE;
}


// --- Lua C Function: write_process(handle, data)
//
// Writes data to the given process handle (stdin).
// Assumes the handle is a valid HANDLE stored as an integer in Lua.
//
// Arguments:
// 1. handle (integer): The HANDLE value (as lightuserdata cast to integer) for the process stdin pipe.
// 2. data (string): The data to write.
//
// Returns:
// 1. success (boolean): true if the write was successful and complete, false otherwise.
// 2. error_msg (string, optional): On failure, an error message string.
static int l_write_handle(lua_State* L) {
    HANDLE hHandle = lua_touserdata(L, 1);
    if (!hHandle || hHandle == INVALID_HANDLE_VALUE) { return push_error(L, "Invalid handle provided to write_handle"); }
    size_t len;
    const char* data = luaL_checklstring(L, 2, &len);

    DWORD error_code = 0;
    BOOL success = write_handle(hHandle, data, len, &error_code);

    if (!success) {
        lua_pushboolean(L, 0);
        if (error_code != 0)
            lua_pushstring(L, FormatErrorMessage("WriteFile failed with error: %lu (%s)").c_str());
        else
            lua_pushstring(L, "WriteFile failed or did not write all bytes");
        return 2;
    }

    lua_pushboolean(L, 1);
    return 1;
}














// First, check if data is available with timeout. Negative wait is same as 0, timeout returns 0, else bytes available
DWORD peek_handle(HANDLE hHandle, int wait = 0)
{
    ULONGLONG start_time = GetTickCount64();
    DWORD bytes_available = 0;
    do {
        if (PeekNamedPipe(hHandle, NULL, 0, NULL, &bytes_available, NULL) && bytes_available > 0) return bytes_available;        
        Sleep(1);
    } while (GetTickCount64() - start_time < wait);
    return 0;
}



/*
 * Lua C function: peek_handle(handle)
 *
 * Checks if data is available for reading on a Windows HANDLE using PeekNamedPipe.
 *
 * Arguments:
 * 1. handle (lightuserdata): The HANDLE (pipe) to peek.
 *
 * Returns:
 * 1. has_data (boolean): true if data is available, false otherwise.
 * 2. bytes_available (number): Number of bytes available for reading.
 * 
 * do not chain with c lua functions, use lua call
 */
static int l_peek_handle(lua_State* L) {
    HANDLE hHandle = lua_touserdata(L, 1);
    if (!hHandle || hHandle == INVALID_HANDLE_VALUE) { return push_error(L, "Invalid handle provided to peek_handle"); }
    auto wait = (int)luaL_optinteger(L, 2, 0);
     
    DWORD bytes_available = peek_handle(hHandle, wait);

    lua_pushinteger(L, bytes_available);
    return 1;
}




















// --- Pure C Function: read_from_handle ---
// Reads data from a Windows handle, using PeekNamedPipe to avoid blocking.
// Returns a ReadResult struct. Caller must free result.data if result.success is TRUE.
// wait is the time in ticks to wait, -1 = blocking
ReadResult read_handle(HANDLE hHandle, DWORD max_bytes, int wait = 0) {    
    ReadResult result = { 0 }; // Initialize all fields to 0/FALSE/NULL
    result.success = FALSE;
    result.data = NULL;
    result.data_len = 0;
    result.pipe_broken = FALSE;
    if (!hHandle || hHandle == INVALID_HANDLE_VALUE) { result.pipe_broken = TRUE; return result; }

    DWORD bytes_read = 0;
    DWORD bytes_available = peek_handle(hHandle, wait);

    // First, check if data is available without blocking
    if (wait >= 0 && bytes_available == 0) {
        // No bytes ready and timed out so return
        result.success = TRUE;
        result.data = NULL;
        result.data_len = 0;
        result.pipe_broken = FALSE;
        return result;
    }
    
    
    // Data is available, read up to the requested amount or available amount
    DWORD to_read = (max_bytes < (int)bytes_available) ? max_bytes : (int)bytes_available;
    result.data = (char*)malloc(to_read + 4);
    ZeroMemory(result.data, to_read + 4);
    if (!result.data) {
        result.error_code = ERROR_NOT_ENOUGH_MEMORY; // Placeholder error code
        return result; // Allocation failed
    }

    result.success = ReadFile(hHandle, result.data, to_read, &bytes_read, NULL);

    if (!result.success) {
        DWORD error = GetLastError();
        free(result.data);
        result.data = NULL;
        result.data_len = 0;
        result.error_code = error;
        if (error == ERROR_BROKEN_PIPE) result.pipe_broken = TRUE;
        return result;
    }

    // Succesfully read, return data
    result.data_len = bytes_read;
    return result;
}



// --- Lua C Function: read_process(handle, [max_bytes])
//
// Reads data from the given process handle (stdout/stderr).
// Assumes the handle is a valid HANDLE stored as an integer in Lua.
// Uses PeekNamedPipe to avoid blocking if no data is available.
//
// Arguments:
// 1. handle (integer): The HANDLE value (as lightuserdata cast to integer) for the process stdout/stderr pipe.
// 2. max_bytes (integer, optional): Maximum number of bytes to read. Default is BUFSIZE.
//
// Returns:
// 1. data (string or nil): On success, the read data (can be empty string if no data available currently or pipe is broken).
//                          On failure (other than broken pipe), nil.
// 2. error_msg (string, optional): On failure (other than broken pipe), an error message string.
// Do not chain with c lua functions, use lua_call
static int l_read_handle(lua_State* L) {
    HANDLE hHandle = lua_touserdata(L, 1);
    if (!hHandle || hHandle == INVALID_HANDLE_VALUE) { return push_error(L, "Invalid handle provided to read_handle"); }
    DWORD max_bytes = (DWORD)luaL_optinteger(L, 2, BUFSIZE);
    int wait = (int)luaL_optinteger(L, 3, 0);

    ReadResult result = read_handle(hHandle, max_bytes, wait);

    if (!result.success) {
        lua_pushstring(L, "");
        lua_pushstring(L, FormatErrorMessage("ReadFile failed with error: %lu (%s)").c_str());
        return 2;
    }

    // Success: Push the data string (or empty string if pipe_broken)
    if (result.data) {
        lua_pushlstring(L, result.data, result.data_len);
        free(result.data); // Free the buffer allocated by the C function
    }
    else {
        // If no data buffer was allocated (e.g., Peek returned 0, and Read didn't happen)
        // or if pipe was broken but data_len is 0
        lua_pushstring(L, "");
    }
    return 1;
}









// --- Pure C Function: close_windows_handle ---
// Closes a Windows handle.
// Returns TRUE on success, FALSE on failure. Sets *error_code if FALSE.
BOOL close_handle(HANDLE hHandle, DWORD* error_code) {
    if (!hHandle || hHandle == INVALID_HANDLE_VALUE) { *error_code = ERROR_INVALID_HANDLE; return FALSE; }

    if (!CloseHandle(hHandle)) {
        *error_code = GetLastError();
        return FALSE;
    }

    return TRUE;
}



// --- Lua C Function: close_handle(handle)
//
// Closes the given Windows handle (process, thread, pipe, etc.).
// Assumes the handle is a valid HANDLE stored as lightuserdata in Lua.
//
// Arguments:
// 1. handle (lightuserdata): The HANDLE to close.
//
// Returns:
// 1. success (boolean): true if CloseHandle succeeded, false otherwise.
// 2. error_msg (string, optional): On failure, an error message string.
static int l_close_handle(lua_State* L) {
    HANDLE hHandle = lua_touserdata(L, 1); // Retrieve HANDLE from lightuserdata
    if (!hHandle || hHandle == INVALID_HANDLE_VALUE) { return push_error(L, "Invalid handle provided to close_handle"); }

    DWORD error_code = 0;
    BOOL success = close_handle(hHandle, &error_code);

    if (!success) {
        lua_pushboolean(L, 0);
        lua_pushstring(L, FormatErrorMessage("CloseHandle failed with error: %lu (%s)").c_str());
        return 2;
    }

    lua_pushboolean(L, 1);
    return 1;
}












// --- Pure C Function: is_process_handle_running ---
// Checks if a process handle is still running.
// Returns TRUE if running, FALSE otherwise (exited, invalid handle).
BOOL is_running_process(HANDLE hProcess) {
    if (!hProcess || hProcess == INVALID_HANDLE_VALUE) { return FALSE; }
    if (WaitForSingleObject(hProcess, 0) == WAIT_TIMEOUT) return TRUE;
    return FALSE;
}


// --- Lua C Function: is_process_running(handle)
//
// Checks if the process associated with the given handle is still running.
// Assumes the handle is a valid PROCESS HANDLE stored as lightuserdata in Lua.
//
// Arguments:
// 1. handle (lightuserdata): The PROCESS HANDLE.
//
// Returns:
// 1. is_running (boolean): true if the process is still running, false otherwise (exited, invalid handle).
static int l_is_running_process(lua_State* L) {
    HANDLE hProcess = lua_touserdata(L, 1); // Retrieve HANDLE from lightuserdata
    if (!hProcess || hProcess == INVALID_HANDLE_VALUE) { return push_error(L, "Invalid handle provided to is_process_running"); }
    lua_pushboolean(L, is_running_process(hProcess) ? 1 : 0);
    return 1;
}










// --- Pure C Function: wait_for_process_handle ---
// Waits for a process handle to exit.
// Returns TRUE on success, FALSE on error. Sets *exit_code if TRUE.
BOOL wait_process(HANDLE hProcess, DWORD* exit_code) {
    exit_code = 0;
    if (!hProcess || hProcess == INVALID_HANDLE_VALUE) { return FALSE; }    
    DWORD wait_result = WaitForSingleObject(hProcess, INFINITE);
    if (wait_result != WAIT_OBJECT_0) { return FALSE; }
    BOOL success = GetExitCodeProcess(hProcess, exit_code);
    return success;
}



// --- Lua C Function: wait_process(handle)
//
// Waits indefinitely for the process associated with the given handle to exit.
// Assumes the handle is a valid PROCESS HANDLE stored as an integer in Lua.
//
// Arguments:
// 1. handle (integer): The PROCESS HANDLE value (as lightuserdata cast to integer).
//
// Returns:
// 1. exit_code (integer): The exit code of the process.
static int l_wait_process(lua_State* L) {
    HANDLE hProcess = lua_touserdata(L, 1); // Retrieve HANDLE from integer
    if (!hProcess || hProcess == INVALID_HANDLE_VALUE) { return push_error(L, "Invalid handle provided to wait_for_process"); }
    DWORD exit_code = 0;

    if (!wait_process(hProcess, &exit_code)) {
        lua_pushinteger(L, (lua_Integer)exit_code);
        lua_pushstring(L, FormatErrorMessage("WaitForProcess failed with error: %lu (%s)").c_str());
        return 2;
    }

    lua_pushinteger(L, (lua_Integer)exit_code);
    return 1;
}











// --- Pure C Function: terminate_process_handle ---
// Attempts to terminate a process handle.
// Returns TRUE if TerminateProcess was called (success or failure is implicit).
BOOL terminate_process(HANDLE hProcess) {
    if (!hProcess || hProcess == INVALID_HANDLE_VALUE) { return FALSE; }
    if (!is_running_process(hProcess)) return TRUE;
    // Note: TerminateProcess can fail silently or cause unexpected behavior.
    // It's generally better to signal the process to shut down gracefully if possible.
    TerminateProcess(hProcess, 1); // Exit code 1 is arbitrary
    return TRUE; // Indicate the call was made
}




// --- Lua C Function: terminate_process(handle)
//
// Attempts to terminate the process associated with the given handle.
// Assumes the handle is a valid PROCESS HANDLE stored as an integer in Lua.
//
// Arguments:
// 1. handle (integer): The PROCESS HANDLE value (as lightuserdata cast to integer).
//
// Returns:
// 1. success (boolean): Always returns true (indication that TerminateProcess was called).
static int l_terminate_process(lua_State* L) {
    HANDLE hProcess = lua_touserdata(L, 1); // Retrieve HANDLE from integer
    if (!hProcess || hProcess == INVALID_HANDLE_VALUE) { return push_error(L, "Invalid handle provided to terminate_process"); }
    BOOL success = terminate_process(hProcess); // Always returns true based on C function
    lua_pushboolean(L, success ? 1 : 0);
    return 1;
}






