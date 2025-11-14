### Processes, Handles, Pipes in lua 

A simple wrapper to spawn processes, create named pipes, deal with handles, etc in lua without using os.execute or io.popen to avoid terminal from popping up. Project can create puc lua or luajit dlls.

[currently only WIN32]

### Example:

Process Example:
```lua
cmd = <cmd to run>
local options = {
	flags = 0,
	inherit_handles = true,
	pipe_stdin = false,
	pipe_stdout = false,
	pipe_stderr = false,
	current_directory = <cwd>
	environment = {
		<key-value pairs to replace in processes environment>
	},
}

success, info = luarun.create_process(cmd, options)
```

Pipe Example:
```lua

pipe_name = [[app_nvim_rpc]]
hPipe, err = luarun.create_named_pipe(pipe_name, "r", -2)
if not hPipe then return end

-- Test write to pipe
pipe = luarun.open_named_pipe(pipe_name, "w")
if not pipe then
	print("Failed to connect")
	return
end

if pipe:write('{"test":"data"}') then
	print("Message sent successfully!")
else
	print("Failed to send message")
end
pipe:close()

msg = hPipe:read()
hPipe:close();
```

Pipes, under lua run, are managed handles. They simply wrap handle based functions and allow using : referencing for a cleaner feel.
