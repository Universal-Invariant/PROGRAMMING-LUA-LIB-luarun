/*
	Lua absolute indexing is extremely error prone which chaining c lua funciton calls since absolute values will reference stack values of previous functions.
	Negative indexing solvest his problem except that it cannot be used with optional arguments which then forces absolute arguments to be used because there is no way to get the actual number of arguments passed. (else one could calculate the absolute indecies correctly).

	This means that c lua functions that use optional lua arguments cannot be chained unless the stack is cleared before pushing the arguments. This way the absolute indecies corespond exactly to the arguments passed.

	When calling lua c functions from c that do not use any optional parameters one can call them directly. Of course this requires pushing the argumnets onto the lua stack before the c call.
	
	When calling lua c functions from c that have optional parameters the c lua funciton cannot use negative indexing(negative indexing does not work with optional arguments).
	Hence the c lua function will use absolute indexing and this requires the stack to be "new". 
	In this case the only way to make it work is to use lua call or a hack which lets us treat a lua c function as having negative and positive indexing. We do this by supplying an offset value which is 1+# of total parameters and we subtract it from the absolute indexing to make negative.

	The problem with negative indexing is that they still cause problems because if we pass more than arguments than possible parameters the negative indexing will reference those incorrectly.
	e.g., foo(A,B,C) but we call it like foo(A,B,C,X,Y,Z). In this case negative indexing will reference X,Y,Z and not A,B,C.



*/


// --- Helper to push arguments onto the Lua stack ---
// Define overloads for different types

void push_to_lua_stack(lua_State* L, int value) {
    lua_pushinteger(L, value);
}

void push_to_lua_stack(lua_State* L, lua_Integer value) {
    lua_pushinteger(L, value);
}

void push_to_lua_stack(lua_State* L, double value) {
    lua_pushnumber(L, value);
}

void push_to_lua_stack(lua_State* L, float value) {
    lua_pushnumber(L, static_cast<double>(value));
}

void push_to_lua_stack(lua_State* L, bool value) {
    lua_pushboolean(L, value ? 1 : 0);
}

void push_to_lua_stack(lua_State* L, const char* value) {
    lua_pushstring(L, value);
}

void push_to_lua_stack(lua_State* L, const std::string& value) {
    lua_pushlstring(L, value.c_str(), value.size());
}

void push_to_lua_stack(lua_State* L, void* value) {
    lua_pushlightuserdata(L, value);
}

// For lua_CFunction (though usually you don't pass these as arguments)
void push_to_lua_stack(lua_State* L, lua_CFunction value) {
    lua_pushcfunction(L, value);
}

// --- Helper to push a tuple of arguments ---
template<typename Tuple, std::size_t... I>
void push_tuple_impl(lua_State* L, const Tuple& tuple, std::index_sequence<I...>) {
    // Expands the tuple elements as arguments to push_to_lua_stack
    // Uses comma operator to ensure order of evaluation
    ((push_to_lua_stack(L, std::get<I>(tuple))), ...);
}

template<typename... Args>
void push_tuple(lua_State* L, const std::tuple<Args...>& args) {
    push_tuple_impl(L, args, std::index_sequence_for<Args...>{});
}

// --- Main template function for calling Lua C functions ---
// lc<function_ptr>(L, arg1, arg2, ...)
// Uses pcall for safety.

template<lua_CFunction Func, typename... Args>
auto lc(lua_State* L, Args&&... args) {
    // 1. Calculate number of arguments
    constexpr int nargs = sizeof...(Args);
    int base_stack_size = lua_gettop(L);

    // 2. Push the function to call onto the stack
    lua_pushcfunction(L, Func);

    // 3. Push arguments onto the stack using the helper
    // Create a tuple to store the arguments (or use parameter pack directly)
    // Using a lambda to expand the pack with the helper function
    // Alternative: Use a fold expression directly: (push_to_lua_stack(L, args), ...);
    (push_to_lua_stack(L, args), ...); // Fold expression C++17 (equivalent)

    // 4. Call the function using lua_pcall
    // We don't specify a custom error handler here (msgh = 0)
    // LUA_MULTRET means accept any number of return values
    int status = lua_pcall(L, nargs, LUA_MULTRET, 0);

    // 5. Handle the result of pcall
    if (status != LUA_OK) {
        // lua_pcall failed (e.g., error in Func, stack overflow during call setup)
        // The error message is on the stack at -1 if pcall caught it, otherwise it longjmp'd
        // For this helper, we assume the error is propagated or handled elsewhere.
        // Let's push the error message onto the stack (if available) and return the status.
        if (lua_isstring(L, -1)) {
            const char* err_msg = lua_tostring(L, -1);
            // You could print it, log it, or re-raise it as a Lua error:
            luaL_error(L, "Error calling function via lc: %s", err_msg);
            // Or just return the status code and let the caller handle it.
            // return status; // If returning status was desired.
        }
        else {
            luaL_error(L, "Error calling function via lc, status code: %d", status);
        }
        // If luaL_error is not called, return the status code:
        // return status;
    }

    // If pcall was successful, the return values are on the stack.
    // This helper currently just leaves them there for the caller to manage.
    // To make it more seamless, you'd need a way to specify expected return types.
    // Example: lc<func_ptr, int, std::string>(L, args...) -> std::tuple<int, std::string>
    // This would pop the results and return them as a tuple.

    // For now, return the number of results pushed by the called function.
    // The caller must know how many results to expect and pop them accordingly.
    int current_stack_size = lua_gettop(L);
    int num_results = current_stack_size - base_stack_size;

    // Return the number of results pushed by the called function.
    return num_results; // This should now be 1 or 2 as expected by l_read_pipe
    
}




template<lua_CFunction Func, typename... Args>
auto lc2(lua_State* L, Args&&... args) {
    // 1. Calculate number of arguments
    constexpr int nargs = sizeof...(Args);

    // 2. Push the function to call onto the stack
    lua_pushcfunction(L, Func); // Stack: [..., func]

    // 3. Push arguments onto the stack using the helper
    (push_to_lua_stack(L, args), ...); // Stack: [..., func, arg1, arg2, ..., argN]

    // 4. Rotate the stack to move the function to the top, just after its args
    // We want: [..., arg1, arg2, ..., argN, func]
    // lua_rotate(L, start_index, n_positions)
    // Start at index where func is (which is nargs+1 from the *top* just after pushing args)
    // Move it down by nargs positions, so it ends up after the args.
    // Current stack top is at index (original_top + 1 + nargs) [func + args]
    // func is at index (original_top + 1)
    // We want to move func (at index +1 relative to start of func+args) down by nargs positions.
    // lua_rotate(L, -(nargs + 1), 1); // Move the item at -(nargs+1) (the function) down by 1 *group* of nargs items.
    // This is complex. A simpler way: move the function (at its current position) down by `nargs` positions.
    // lua_rotate(L, -(nargs + 1), nargs); // This moves the function (originally at -(nargs+1)) down by nargs steps, placing it at -1.
    // Let's think: Stack [func, arg1, arg2]. nargs = 2. Want [arg1, arg2, func].
    // func is at -3. lua_rotate(L, -3, 2). This takes element at -3, moves it down 2 positions (past arg1 and arg2), placing it at -1.
    // New stack: [arg1, arg2, func]. Correct!

    lua_rotate(L, -(nargs + 1), nargs); // Move function (currently at -(nargs+1)) down by nargs positions

    // Stack is now: [..., arg1, arg2, ..., argN, func]

    // 5. Call the function using lua_pcall
    // LUA_MULTRET means accept any number of return values
    int status = lua_pcall(L, nargs, LUA_MULTRET, 0); // nargs args, func is at top

    // 6. Handle the result of pcall
    if (status != LUA_OK) {
        // lua_pcall failed (e.g., error in Func, stack overflow during call setup)
        // The error message is on the stack at -1 if pcall caught it, otherwise it longjmp'd
        if (lua_isstring(L, -1)) {
            const char* err_msg = lua_tostring(L, -1);
            luaL_error(L, "Error calling function via lc: %s", err_msg);
        }
        else {
            luaL_error(L, "Error calling function via lc, status code: %d", status);
        }
    }

    // If pcall was successful, the return values are on the stack.
    // Return the number of results pushed by the called function.
    // lua_pcall consumed nargs+1 items (func + args) and pushed results.
    // The number of results is lua_gettop(L) after pcall.
    return lua_gettop(L);
}








// --- Example Usage ---
// Assuming you have your l_write_handle function defined somewhere:
// static int l_write_handle(lua_State* L) { ... }

// Inside another C function (e.g., l_some_other_function):
/*
static int l_some_other_function(lua_State* L) {
    // ... (do some setup) ...

    // Call l_write_handle indirectly using the helper
    // The helper pushes the function, the arguments, and calls lua_pcall.
    HANDLE hHandle = /* ... get handle ... * /;
    const char* data_to_write = "Hello from C++ helper!";
    int num_results = lc<l_write_handle>(L, hHandle, data_to_write); // Assumes HANDLE can be pushed as lightuserdata

    if (num_results == 2) { // l_write_handle returns success, error_msg on failure
        bool success = get_from_lua_stack(L, -2, (bool*)nullptr); // Get success boolean
        std::string error_msg = get_from_lua_stack(L, -1, (std::string*)nullptr); // Get error message
        if (!success) {
            printf("Write failed: %s\n", error_msg.c_str());
        }
        lua_pop(L, 2); // Pop the results
    } else if (num_results == 1) { // l_write_handle returns success boolean on success
        bool success = get_from_lua_stack(L, -1, (bool*)nullptr);
        if (success) {
            printf("Write succeeded\n");
        }
        lua_pop(L, 1); // Pop the result
    }
    // ... handle other potential result counts if l_write_handle is inconsistent ...

    // ... (do more work) ...
    return 0; // Or return values from l_some_other_function
}
*/

// --- Alternative: lc with Expected Return Types ---
// This version attempts to retrieve specific return types.
/*
template<lua_CFunction Func, typename... ReturnTypes, typename... Args>
std::tuple<ReturnTypes...> lc_with_returns(lua_State* L, Args&&... args) {
    // Call the function using the basic helper
    lc<Func>(L, std::forward<Args>(args)...);

    // Get the number of results expected
    constexpr int num_returns = sizeof...(ReturnTypes);

    // Check if the number of results matches expectations
    int top = lua_gettop(L);
    if (top != num_returns) {
        luaL_error(L, "Function returned %d values, expected %d", top, num_returns);
    }

    // Create a tuple to hold the results
    std::tuple<ReturnTypes...> results;

    // Helper lambda to assign results using index sequence
    auto assign_results = [&L, &results]<std::size_t... I>(std::index_sequence<I...>) {
        // Use fold expression to assign each result
        ((std::get<I>(results) = get_from_lua_stack(L, static_cast<int>(I) + 1, static_cast<ReturnTypes*>(nullptr))), ...);
    };

    assign_results(std::index_sequence_for<ReturnTypes...>{});

    // Pop the results from the stack
    lua_pop(L, num_returns);

    return results;
}*/

// --- Example Usage with Returns ---
/*
static int l_some_other_function(lua_State* L) {
    // ...
    auto [success, error_msg] = lc_with_returns<l_write_handle, bool, std::string>(L, hHandle, data_to_write);
    if (!success) {
        printf("Write failed: %s\n", error_msg.c_str());
    } else {
        printf("Write succeeded\n");
    }
    // ...
    return 0;
}
*/