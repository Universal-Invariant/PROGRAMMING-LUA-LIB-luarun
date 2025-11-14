-- test_process_spawn.lua
-- Test script for the process_spawn DLL
-- check_architecture.lua

print("Lua version:", _VERSION)
print("LuaJIT version:", jit and jit.version or "Not LuaJIT")
print("LuaJIT architecture:", jit and jit.arch or "Unknown")
print("OS:", jit and jit.os or "Unknown")

-- Alternative way to check pointer size (for older Lua versions)
local function get_pointer_size()
    local s = string.dump(function() end)
    local x = 1
    -- Try different patterns to detect pointer size
    if string.find(s, "^.*\x08\x00\x00\x00.*$") then
        return 8  -- 64-bit
    elseif string.find(s, "^.*\x04\x00\x00\x00.*$") then
        return 4  -- 32-bit
    else
        -- Fallback: try to detect from math library
        if math.maxinteger then
            -- Lua 5.3+ with integer support
            if math.maxinteger > 2^32 then
                return 8  -- 64-bit
            else
                return 4  -- 32-bit
            end
        else
            return 4  -- Assume 32-bit for older Lua
        end
    end
end

print("Pointer size (estimated):", get_pointer_size(), "bytes")





local success, process = pcall(require, "luarun")

if not success then
    print("ERROR: Could not load luarun module")
    print("Make sure luarun.dll is in your Lua package path")
    print("Error:", process)
    return
end

print("Testing process_spawn module...")
print("Available functions:", table.concat({
    "spawn", "wait", "terminate", "write", "read", "close"
}, ", "))

local p = "C:\\Users\\Main\\AppData\\Local\\Obsidian\\Obsidian.exe"
-- Test 1: Try to spawn obsidian.exe (or another program)
print("\n--- Test 1: Spawning obsidian.exe ---")
local proc = process.spawn(p)

if proc then
    print("SUCCESS: Process spawned")
    print("Handle/PID:", proc.handle or proc.pid)
    print("Stdin handle:", proc.stdin)
    print("Stdout handle:", proc.stdout) 
    print("Stderr handle:", proc.stderr)
    
    -- Test 2: Wait for process (with timeout to prevent hanging)
    print("\n--- Test 2: Waiting for process("..tostring(proc.handle or proc.pid)..") (with 3 second timeout) ---")
    print("Note: If obsidian.exe is not running, this will hang until it exits")
    print("You may need to close obsidian.exe manually to continue")

	io.read("*l")
    -- Instead of waiting indefinitely, we'll just close it
    print("Terminating process...")
    local terminated = process.terminate(proc.handle or proc.pid)
    print("Process terminated:", terminated)
    
    -- Clean up handles
    process.close(proc.stdin)
    process.close(proc.stdout) 
    process.close(proc.stderr)
    
    print("Handles closed successfully")
else
    print("FAILED: Could not spawn obsidian.exe")
    print("This might be because obsidian.exe is not in your PATH")
    print("Trying with a simpler command instead...")
    
    -- Test with a simpler command that should always exist on Windows
    print("\n--- Test 1b: Spawning cmd.exe /c echo hello ---")
    local proc2 = process.spawn("cmd.exe /c echo hello")
    
    if proc2 then
        print("SUCCESS: Simple command spawned")
        print("PID:", proc2.pid)
        
        -- Wait for it to complete
        local exit_code = process.wait(proc2.pid)
        print("Process exited with code:", exit_code)
        
        -- Try to read output (though echo might not produce readable output on stdout)
        local output = process.read(proc2.stdout, 1024)
        print("Output:", output)
        
        -- Clean up
        process.close(proc2.stdin)
        process.close(proc2.stdout)
        process.close(proc2.stderr)
    else
        print("FAILED: Could not spawn even simple command")
    end
end


-- Test 3: Try to read from a process that produces output
print("\n--- Test 3: Reading from a process ---")
local proc3 = process.spawn("cmd.exe /c dir")

if proc3 then
    print("Spawned dir command")
    
    -- Wait for it to complete first
    local exit_code = process.wait(proc3.handle)
    print("Dir command exited with code:", exit_code)
    
    -- Read output after completion
    local output = process.read(proc3.stdout, 4096)
    print("Output from dir command:")
    print(output)
    
    -- Clean up
    process.close(proc3.stdin)
    process.close(proc3.stdout)
    process.close(proc3.stderr)
else
    print("Could not spawn dir command")
end

-- Test 4: Try to write to a process
print("\n--- Test 4: Writing to a process ---")
local proc4 = process.spawn("cmd.exe")

if proc4 then
    print("Spawned cmd.exe")
    
    -- Write a command to stdin
    process.write(proc4.stdin, "echo Hello from Lua!\r\n")
    process.write(proc4.stdin, "exit\r\n") -- Exit the cmd process
    
    -- Wait for it to finish
    local exit_code = process.wait(proc4.handle)
    print("Cmd.exe exited with code:", exit_code)
    
    -- Clean up
    process.close(proc4.stdin)
    process.close(proc4.stdout)
    process.close(proc4.stderr)
else
    print("Could not spawn cmd.exe for write test")
end


print("\n--- Process spawn test completed ---")
print("If you saw SUCCESS messages, the DLL is working correctly!")
print("If you saw errors, check:")
print("1. The DLL file is in the right location")
print("2. You're linking against the correct Lua version")
print("3. The Lua require path includes the DLL location")