# Link the host unit tests with the sanitizers they are compiled with (native env only).
Import("env")
env.Append(LINKFLAGS=["-fsanitize=address,undefined,float-cast-overflow"])
