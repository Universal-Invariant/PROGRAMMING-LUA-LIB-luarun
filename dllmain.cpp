

#pragma comment(lib, "lua54.lib")

#ifdef _WIN32

#include <windows.h>
#include <string>
#include <vector>
#include <psapi.h>  // For GetModuleFileNameEx
#include <tlhelp32.h> // For process enumeration
#include <shlobj.h>      // For CSIDL_* constants and SHGetFolderPath
#include <shlwapi.h>     // For path handling functions
#include <comdef.h>      // For COM smart pointers and BSTR support
#include <process.h>     // For _beginthreadex if needed
#include <stdio.h>
#else
#include <sys/wait.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#endif



extern "C" {
    #include "include\lua.h"
    #include "include\lauxlib.h"
    #include "include\lualib.h"
}


// Windows implementation
#ifdef _WIN32
static int l_spawn_process(lua_State* L) {
    const char* command = luaL_checkstring(L, 1);

    STARTUPINFOA si;
    PROCESS_INFORMATION pi;

    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE; // Hide the window

    // Create pipes for stdin/stdout/stderr
    HANDLE stdin_read, stdin_write;
    HANDLE stdout_read, stdout_write;
    HANDLE stderr_read, stderr_write;

    SECURITY_ATTRIBUTES sa;
    sa.nLength = sizeof(SECURITY_ATTRIBUTES);
    sa.bInheritHandle = TRUE;
    sa.lpSecurityDescriptor = NULL;

    // Create pipes
    CreatePipe(&stdin_read, &stdin_write, &sa, 0);
    CreatePipe(&stdout_read, &stdout_write, &sa, 0);
    CreatePipe(&stderr_read, &stderr_write, &sa, 0);

    // Set handles
    si.hStdInput = stdin_read;
    si.hStdOutput = stdout_write;
    si.hStdError = stderr_write;

    // Create the process
    if (!CreateProcessA(
        NULL,           // Application name
        (LPSTR)command, // Command line
        NULL,           // Process security attributes
        NULL,           // Thread security attributes
        TRUE,           // Inherit handles
        CREATE_NO_WINDOW, // Creation flags
        NULL,           // Environment
        NULL,           // Current directory
        &si,            // Startup info
        &pi             // Process information
    )) {
        lua_pushnil(L);
        lua_pushstring(L, "Failed to create process");
        return 2;
    }

    // Close unused handles
    CloseHandle(stdin_read);
    CloseHandle(stdout_write);
    CloseHandle(stderr_write);

    // Return process handle and pipes
    lua_createtable(L, 0, 4);  // Create table with 4 pre-allocated fields
    lua_pushinteger(L, (lua_Integer)pi.hProcess);
    lua_setfield(L, -2, "handle");
    lua_pushinteger(L, (lua_Integer)stdin_write);
    lua_setfield(L, -2, "stdin");
    lua_pushinteger(L, (lua_Integer)stdout_read);
    lua_setfield(L, -2, "stdout");
    lua_pushinteger(L, (lua_Integer)stderr_read);
    lua_setfield(L, -2, "stderr");

    return 1;
}

static int l_wait_process(lua_State* L) {
    HANDLE hProcess = (HANDLE)luaL_checkinteger(L, 1);
    DWORD exit_code;

    WaitForSingleObject(hProcess, INFINITE);
    GetExitCodeProcess(hProcess, &exit_code);

    lua_pushinteger(L, (lua_Integer)exit_code);
    return 1;
}

static int l_terminate_process(lua_State* L) {
    HANDLE hProcess = (HANDLE)luaL_checkinteger(L, 1);
    TerminateProcess(hProcess, 1);

    lua_pushboolean(L, 1);
    return 1;
}

#else // Unix/Linux implementation
static int l_spawn_process(lua_State* L) {
    const char* command = luaL_checkstring(L, 1);

    int stdin_pipe[2], stdout_pipe[2], stderr_pipe[2];

    if (pipe(stdin_pipe) == -1 || pipe(stdout_pipe) == -1 || pipe(stderr_pipe) == -1) {
        lua_pushnil(L);
        lua_pushstring(L, "Failed to create pipes");
        return 2;
    }

    pid_t pid = fork();

    if (pid == 0) {
        // Child process
        close(stdin_pipe[1]);
        close(stdout_pipe[0]);
        close(stderr_pipe[0]);

        dup2(stdin_pipe[0], STDIN_FILENO);
        dup2(stdout_pipe[1], STDOUT_FILENO);
        dup2(stderr_pipe[1], STDERR_FILENO);

        close(stdin_pipe[0]);
        close(stdout_pipe[1]);
        close(stderr_pipe[1]);

        // Execute the command
        execl("/bin/sh", "sh", "-c", command, NULL);
        exit(1); // If exec fails
    }
    else if (pid > 0) {
        // Parent process
        close(stdin_pipe[0]);
        close(stdout_pipe[1]);
        close(stderr_pipe[1]);

        lua_createtable(L, 0, 4);  // Create table with 4 pre-allocated fields
        lua_pushinteger(L, (lua_Integer)pid);
        lua_setfield(L, -2, "pid");
        lua_pushinteger(L, stdin_pipe[1]);
        lua_setfield(L, -2, "stdin");
        lua_pushinteger(L, stdout_pipe[0]);
        lua_setfield(L, -2, "stdout");
        lua_pushinteger(L, stderr_pipe[0]);
        lua_setfield(L, -2, "stderr");

        return 1;
    }
    else {
        // Fork failed
        close(stdin_pipe[0]);
        close(stdin_pipe[1]);
        close(stdout_pipe[0]);
        close(stdout_pipe[1]);
        close(stderr_pipe[0]);
        close(stderr_pipe[1]);

        lua_pushnil(L);
        lua_pushstring(L, "Failed to fork process");
        return 2;
    }
}

static int l_wait_process(lua_State* L) {
    pid_t pid = (pid_t)luaL_checkinteger(L, 1);
    int status;

    waitpid(pid, &status, 0);

    if (WIFEXITED(status)) {
        lua_pushinteger(L, (lua_Integer)WEXITSTATUS(status));
    }
    else {
        lua_pushinteger(L, (lua_Integer)-1);
    }
    return 1;
}

static int l_terminate_process(lua_State* L) {
    pid_t pid = (pid_t)luaL_checkinteger(L, 1);
    int result = kill(pid, SIGTERM);

    lua_pushboolean(L, result == 0);
    return 1;
}

#endif

static int l_write_to_process(lua_State* L) {
    lua_Integer handle = luaL_checkinteger(L, 1);
    size_t len;
    const char* data = luaL_checklstring(L, 2, &len);

#ifdef _WIN32
    HANDLE hStdIn = (HANDLE)handle;
    DWORD bytes_written;
    WriteFile(hStdIn, data, (DWORD)len, &bytes_written, NULL);
#else
    write((int)handle, data, len);
#endif

    lua_pushboolean(L, 1);
    return 1;
}

static int l_read_from_process(lua_State* L) {
    lua_Integer handle = luaL_checkinteger(L, 1);
    int max_bytes = (int)luaL_optinteger(L, 2, 4096);

    char* buffer = (char*)malloc(max_bytes + 1);
    if (!buffer) {
        lua_pushnil(L);
        lua_pushstring(L, "Memory allocation failed");
        return 2;
    }

    int bytes_read = 0;

#ifdef _WIN32
    HANDLE hStdOut = (HANDLE)handle;
    DWORD available;
    if (PeekNamedPipe(hStdOut, NULL, 0, NULL, &available, NULL)) {
        if (available > 0) {
            if (available > max_bytes) available = max_bytes;
            ReadFile(hStdOut, buffer, available, (DWORD*)&bytes_read, NULL);
        }
    }
#else
    bytes_read = (int)read((int)handle, buffer, max_bytes);
#endif

    if (bytes_read > 0) {
        buffer[bytes_read] = '\0';
        lua_pushlstring(L, buffer, bytes_read);
    }
    else {
        lua_pushstring(L, "");
    }

    free(buffer);
    return 1;
}

static int l_close_handle(lua_State* L) {
    lua_Integer handle = luaL_checkinteger(L, 1);

#ifdef _WIN32
    CloseHandle((HANDLE)handle);
#else
    close((int)handle);
#endif

    lua_pushboolean(L, 1);
    return 1;
}

static const luaL_Reg process_lib[] = {
    {"spawn", l_spawn_process},
    {"wait", l_wait_process},
    {"terminate", l_terminate_process},
    {"write", l_write_to_process},
    {"read", l_read_from_process},
    {"close", l_close_handle},
    {NULL, NULL}
};

// Lua 5.4 compatible module loading
extern "C" __declspec(dllexport) int luaopen_luarun(lua_State* L) {
    luaL_newlib(L, process_lib);  // This is Lua 5.4 compatible
    return 1;
}