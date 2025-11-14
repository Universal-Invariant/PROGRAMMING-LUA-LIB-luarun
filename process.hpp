






// Structure to hold the results of the process creation
typedef struct {
    BOOL success;
    DWORD error_code; // Set if success is FALSE
    DWORD process_id;
    HANDLE hProcess;
    HANDLE hThread;
    // Handles created by the function that the *caller* might need
    HANDLE stdin_read; // If pipe_stdin was true, caller might need this
    HANDLE stdin_write; // If pipe_stdin was true, caller might need this
    HANDLE stdout_read; // If pipe_stdout was true, caller might need this
    HANDLE stdout_write; // If pipe_stdout was true, caller might need this
    HANDLE stderr_read; // If pipe_stderr was true, caller might need this
    HANDLE stderr_write; // If pipe_stderr was true, caller might need this
} ProcessResult;




/*
* CreatesProcess: Creates a process
*/
ProcessResult create_process(
    LPCWSTR cmd_line_w,
    LPCWSTR current_dir_w, // Can be NULL
    DWORD flags,
    BOOL inherit_handles,
    HANDLE explicit_stdin, // Can be NULL
    HANDLE explicit_stdout, // Can be NULL
    HANDLE explicit_stderr, // Can be NULL
    BOOL pipe_stdin,
    BOOL pipe_stdout,
    BOOL pipe_stderr,
    LPWSTR merged_env // Can be NULL
) {
    ProcessResult result = { 0 }; // Initialize all fields to 0/FALSE/NULL

    // Handle pipes if requested
    SECURITY_ATTRIBUTES sa = { 0 };
    sa.nLength = sizeof(SECURITY_ATTRIBUTES);
    sa.bInheritHandle = inherit_handles; // If we want the child to inherit, it must be inheritable
    sa.lpSecurityDescriptor = NULL;

    HANDLE stdin_read = NULL, stdin_write = NULL;
    HANDLE stdout_read = NULL, stdout_write = NULL;
    HANDLE stderr_read = NULL, stderr_write = NULL;

    STARTUPINFOW si = { 0 };
    PROCESS_INFORMATION pi = { 0 };
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    BOOL success = FALSE;

    // Create pipes for stdin, stdout, stderr if requested
    if (pipe_stdin) {
        if (!CreatePipe(&stdin_read, &stdin_write, &sa, 0)) {
            result.success = FALSE;
            result.error_code = GetLastError();
            goto cleanup; // Jump to cleanup on error
        }
        if (!inherit_handles) {
            SetHandleInformation(stdin_read, HANDLE_FLAG_INHERIT, 0);
            SetHandleInformation(stdin_write, HANDLE_FLAG_INHERIT, 0);
        }
        // Use the read end for the child's stdin
        // explicit_stdin is set later if it was originally NULL
    }
    if (pipe_stdout) {
        if (!CreatePipe(&stdout_read, &stdout_write, &sa, 0)) {
            result.success = FALSE;
            result.error_code = GetLastError();
            goto cleanup;
        }
        if (!inherit_handles) {
            SetHandleInformation(stdout_read, HANDLE_FLAG_INHERIT, 0);
            SetHandleInformation(stdout_write, HANDLE_FLAG_INHERIT, 0);
        }
        // Use the write end for the child's stdout
        // explicit_stdout is set later if it was originally NULL
    }
    if (pipe_stderr) {
        if (!CreatePipe(&stderr_read, &stderr_write, &sa, 0)) {
            result.success = FALSE;
            result.error_code = GetLastError();
            goto cleanup;
        }
        if (!inherit_handles) {
            SetHandleInformation(stderr_read, HANDLE_FLAG_INHERIT, 0);
            SetHandleInformation(stderr_write, HANDLE_FLAG_INHERIT, 0);
        }
        // Use the write end for the child's stderr
        // explicit_stderr is set later if it was originally NULL
    }


    // Determine final handles to use, prioritizing explicit ones, then pipe ones, then default
    si.hStdInput = explicit_stdin ? explicit_stdin : (stdin_read ? stdin_read : GetStdHandle(STD_INPUT_HANDLE));
    si.hStdOutput = explicit_stdout ? explicit_stdout : (stdout_write ? stdout_write : GetStdHandle(STD_OUTPUT_HANDLE));
    si.hStdError = explicit_stderr ? explicit_stderr : (stderr_write ? stderr_write : GetStdHandle(STD_ERROR_HANDLE));

    

    success = CreateProcessW(
        NULL,             // lpApplicationName
        (LPWSTR)cmd_line_w, // lpCommandLine (Must be writable, cast is okay as CreateProcess makes a copy)
        NULL,             // lpProcessAttributes
        NULL,             // lpThreadAttributes
        inherit_handles,  // bInheritHandles
        flags | CREATE_UNICODE_ENVIRONMENT, // dwCreationFlags
        merged_env,       // lpEnvironment
        current_dir_w,    // lpCurrentDirectory
        &si,              // lpStartupInfo
        &pi               // lpProcessInformation
    );

    if (!success) {
        result.success = FALSE;
        result.error_code = GetLastError();
        goto cleanup; // Jump to cleanup on error
    }

    // Success!
    result.success = TRUE;
    result.process_id = pi.dwProcessId;
    result.hProcess = pi.hProcess;
    result.hThread = pi.hThread;

    // Store the handles created by this function that the *caller* might need
    // The caller is responsible for closing these if they are non-NULL.
    result.stdin_read = stdin_read;
    result.stdin_write = stdin_write;
    result.stdout_read = stdout_read;
    result.stdout_write = stdout_write;
    result.stderr_read = stderr_read;
    result.stderr_write = stderr_write;

    // Close handles that the *child* process uses in the *parent* process
    // These are not returned to the caller and should be closed here.
    if (stdin_read && pipe_stdin) CloseHandle(stdin_read);
    if (stdout_write && pipe_stdout) CloseHandle(stdout_write);
    if (stderr_write && pipe_stderr) CloseHandle(stderr_write);

    return result; // Return the populated result structure

cleanup:
    // Close any handles we opened during the failed attempt
    if (stdin_read) CloseHandle(stdin_read);
    if (stdin_write) CloseHandle(stdin_write);
    if (stdout_read) CloseHandle(stdout_read);
    if (stdout_write) CloseHandle(stdout_write);
    if (stderr_read) CloseHandle(stderr_read);
    if (stderr_write) CloseHandle(stderr_write);

    // result.success is already FALSE, error_code is set
    return result;
}






/*
 * Lua C function: create_process(command_line, [options])
 *
 * Launches a new process using the Win32 CreateProcessW function with advanced options.
 * Options table can include: current_directory, flags, inherit_handles, stdin_handle,
 * stdout_handle, stderr_handle, pipe_stdin, pipe_stdout, pipe_stderr.
 * If pipe_stdin/stdout/stderr are true, it creates pipes and returns the handles.
 *
 * Arguments:
 * 1. command_line (string): The command line to execute.
 * 2. options (table, optional): Configuration table.
 *    - current_directory (string): Starting directory for the process.
 *    - flags (number): dwCreationFlags for CreateProcess.
 *    - inherit_handles (boolean): Whether child inherits handles.
 *    - stdin_handle (number): HANDLE value for stdin (as lightuserdata).
 *    - stdout_handle (number): HANDLE value for stdout (as lightuserdata).
 *    - stderr_handle (number): HANDLE value for stderr (as lightuserdata).
 *    - pipe_stdin (boolean): Create a pipe for stdin.
 *    - pipe_stdout (boolean): Create a pipe for stdout.
 *    - pipe_stderr (boolean): Create a pipe for stderr.
 *
 * Returns:
 * 1. success (boolean): true if the process was created, false otherwise.
 * 2. info (table or string): On success, a table containing {pid, hProcess, hThread, stdin_write, stdout_read, stderr_read}.
 *                            On failure, an error message string.
 */
static int l_create_process(lua_State* L) {
    const char* cmd_line_utf8 = luaL_checkstring(L, 1);

    // Default options
    const char* current_dir_utf8 = NULL;
    DWORD flags = 0;
    BOOL inherit_handles = FALSE;
    HANDLE explicit_stdin = NULL;
    HANDLE explicit_stdout = NULL;
    HANDLE explicit_stderr = NULL;
    BOOL pipe_stdin = FALSE;
    BOOL pipe_stdout = FALSE;
    BOOL pipe_stderr = FALSE;
    LPWSTR merged_env = NULL;

    // Parse options table if provided
    if (lua_type(L, 2) == LUA_TTABLE) {
        lua_getfield(L, 2, "current_directory");
        if (lua_type(L, -1) == LUA_TSTRING) {
            current_dir_utf8 = lua_tostring(L, -1);
        }
        lua_pop(L, 1);

        lua_getfield(L, 2, "flags");
        if (lua_type(L, -1) == LUA_TNUMBER) {
            flags = (DWORD)luaL_checknumber(L, -1);
        }
        lua_pop(L, 1);

        lua_getfield(L, 2, "inherit_handles");
        if (lua_type(L, -1) == LUA_TBOOLEAN) {
            inherit_handles = lua_toboolean(L, -1);
        }
        lua_pop(L, 1);

        lua_getfield(L, 2, "stdin_handle");
        if (lua_type(L, -1) == LUA_TLIGHTUSERDATA) {
            explicit_stdin = lua_touserdata(L, -1);
        }
        lua_pop(L, 1);

        lua_getfield(L, 2, "stdout_handle");
        if (lua_type(L, -1) == LUA_TLIGHTUSERDATA) {
            explicit_stdout = lua_touserdata(L, -1);
        }
        lua_pop(L, 1);

        lua_getfield(L, 2, "stderr_handle");
        if (lua_type(L, -1) == LUA_TLIGHTUSERDATA) {
            explicit_stderr = lua_touserdata(L, -1);
        }
        lua_pop(L, 1);

        lua_getfield(L, 2, "pipe_stdin");
        if (lua_type(L, -1) == LUA_TBOOLEAN) {
            pipe_stdin = lua_toboolean(L, -1);
        }
        lua_pop(L, 1);

        lua_getfield(L, 2, "pipe_stdout");
        if (lua_type(L, -1) == LUA_TBOOLEAN) {
            pipe_stdout = lua_toboolean(L, -1);
        }
        lua_pop(L, 1);

        lua_getfield(L, 2, "pipe_stderr");
        if (lua_type(L, -1) == LUA_TBOOLEAN) {
            pipe_stderr = lua_toboolean(L, -1);
        }
        lua_pop(L, 1);

        lua_getfield(L, 2, "environment");
        if (lua_type(L, -1) == LUA_TTABLE) {
            merged_env = build_merged_environment(L, lua_gettop(L));
        }
        lua_pop(L, 1); // pop environment table
    }

    // Convert UTF-8 strings to wide strings
    auto cmd_line_w = utf8_to_wide_string(cmd_line_utf8, L);
    std::wstring current_dir_w;
    if (current_dir_utf8) {
        current_dir_w = utf8_to_wide_string(current_dir_utf8, L);
    }

    // Call the pure C core function
    ProcessResult core_result = create_process(
        cmd_line_w.c_str(),
        current_dir_w.c_str(),
        flags,
        inherit_handles,
        explicit_stdin,
        explicit_stdout,
        explicit_stderr,
        pipe_stdin,
        pipe_stdout,
        pipe_stderr,
        merged_env
    );

    // Cleanup wide strings and merged environment    
    if (merged_env) free(merged_env); // Assuming build_merged_environment allocates with malloc/free

    // Handle the result from the core function
    if (!core_result.success) {
        // Convert error code to human-readable message
        LPSTR message_buffer = NULL;
        FormatMessageA(
            FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
            NULL,
            core_result.error_code, // Use the error code from the core function
            MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
            (LPSTR)&message_buffer,
            0,
            NULL
        );

        char error_msg[512];
        if (message_buffer) {
            snprintf(error_msg, sizeof(error_msg), "CreateProcess failed: %s (Code: %lu)", message_buffer, core_result.error_code);
            LocalFree(message_buffer);
        }
        else {
            snprintf(error_msg, sizeof(error_msg), "CreateProcess failed with code %lu (Could not format message).", core_result.error_code);
        }
        return push_error(L, error_msg);
    }

    // Process created successfully
    // Push results back to Lua
    lua_pushboolean(L, 1); // Success boolean

    // Create return table
    lua_createtable(L, 0, 6); // Pre-allocate space for known keys

    lua_pushinteger(L, core_result.process_id);
    lua_setfield(L, -2, "pid");

    lua_pushlightuserdata(L, core_result.hProcess);
    lua_setfield(L, -2, "hProcess");

    lua_pushlightuserdata(L, core_result.hThread);
    lua_setfield(L, -2, "hThread");

    // Return the handles that the *parent* might need (only if they were created by the core function)
    if (core_result.stdin_read) {
        lua_pushlightuserdata(L, core_result.stdin_read);
        lua_setfield(L, -2, "stdin_read");
    }
    if (core_result.stdin_write) {
        lua_pushlightuserdata(L, core_result.stdin_write);
        lua_setfield(L, -2, "stdin_write");
    }
    if (core_result.stdout_read) {
        lua_pushlightuserdata(L, core_result.stdout_read);
        lua_setfield(L, -2, "stdout_read");
    }
    if (core_result.stdout_write) {
        lua_pushlightuserdata(L, core_result.stdout_write);
        lua_setfield(L, -2, "stdout_write");
    }
    if (core_result.stderr_read) {
        lua_pushlightuserdata(L, core_result.stderr_read);
        lua_setfield(L, -2, "stderr_read");
    }
    if (core_result.stderr_write) {
        lua_pushlightuserdata(L, core_result.stderr_write);
        lua_setfield(L, -2, "stderr_write");
    }

    return 2; // Return success boolean and info table
}
























