





static bool g_prev_key_state[256] = { false };

static int l_get_key_press(lua_State* L) {
    // Optional: allow caller to pass a "clear" flag or delay (not needed for edge detection)

    // Check if REAPER is foreground (optional but recommended)
    /*
    HWND reaperHwnd = FindWindowA("REAPERMainWindow", NULL);
    if (!reaperHwnd || GetForegroundWindow() != reaperHwnd) {
        lua_pushnil(L);
        return 1;
    }*/

    bool ctrl = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
    bool shift = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
    bool alt = (GetAsyncKeyState(VK_MENU) & 0x8000) != 0;

    // Special key map (same as before)
    struct KeyMap {
        UINT vk;
        const char* nvim_name;
    };
    struct PrintableKey {
        UINT vk;
        char plain_char;      // for when no modifiers
        char shift_char;      // for when shift is held
    };

    static const KeyMap special_keys[] = {
        {VK_ESCAPE, "Esc"}, {VK_RETURN, "CR"}, {VK_TAB, "Tab"}, {VK_BACK, "BS"},
        {VK_SPACE, "Space"}, {VK_LEFT, "Left"}, {VK_RIGHT, "Right"}, {VK_UP, "Up"},
        {VK_DOWN, "Down"}, {VK_HOME, "Home"}, {VK_END, "End"}, {VK_INSERT, "Insert"},
        {VK_DELETE, "Del"}, {VK_PRIOR, "PageUp"}, {VK_NEXT, "PageDown"},
        {VK_F1, "F1"}, {VK_F2, "F2"}, {VK_F3, "F3"}, {VK_F4, "F4"},
        {VK_F5, "F5"}, {VK_F6, "F6"}, {VK_F7, "F7"}, {VK_F8, "F8"},
        {VK_F9, "F9"}, {VK_F10, "F10"}, {VK_F11, "F11"}, {VK_F12, "F12"},
    };

    static const PrintableKey oem_keys[] = {
        {VK_OEM_PERIOD, '.', '>'},
        {VK_OEM_2, '/', '?'},      // US layout: / and ?
        {VK_OEM_1, ';', ':'},      // ; and :
        {VK_OEM_7, '\'', '"'},     // ' and "
        {VK_OEM_COMMA, ',', '<'},
        {VK_OEM_MINUS, '-', '_'},
        {VK_OEM_PLUS, '=', '+'},   // Note: + is shift on US
        {VK_OEM_4, '[', '{'},
        {VK_OEM_6, ']', '}'},
        {VK_OEM_5, '\\', '|'},    // or VK_OEM_102 on some keyboards
        {VK_OEM_3, '`', '~'},
        // Add more based on US QWERTY layout
    };

    for (const auto& k : oem_keys) {
        bool is_down = (GetAsyncKeyState(k.vk) & 0x8000) != 0;
        if (is_down && !g_prev_key_state[k.vk]) {
            g_prev_key_state[k.vk] = true;

            char ch;
            if (shift) {
                ch = k.shift_char;
            }
            else {
                ch = k.plain_char;
            }

            std::string result;
            if (ctrl || alt) {
                // Wrap in <C-...> etc.
                result = "<";
                if (ctrl) result += "C-";
                if (alt)  result += "A-";
                // For punctuation, Neovim expects the actual char: <C-@>, <C-]>
                result += ch;
                result += ">";
            }
            else {
                // Plain character — send as single char string
                result = std::string(1, ch);
            }
            lua_pushstring(L, result.c_str());
            return 1;
        }
        g_prev_key_state[k.vk] = is_down;
    }
    // Check special keys: only trigger on **press** (rising edge)
    for (const auto& k : special_keys) {
        bool is_down = (GetAsyncKeyState(k.vk) & 0x8000) != 0;
        if (is_down && !g_prev_key_state[k.vk]) {
            // Rising edge: key was up, now down
            g_prev_key_state[k.vk] = true;

            std::string result = "<";
            if (ctrl)  result += "C-";
            if (shift) result += "S-";
            if (alt)   result += "A-";
            result += k.nvim_name;
            result += ">";
            lua_pushstring(L, result.c_str());
            return 1;
        }
        g_prev_key_state[k.vk] = is_down; // update state
    }

    for (const auto& k : oem_keys) {
        bool is_down = (GetAsyncKeyState(k.vk) & 0x8000) != 0;
        if (is_down && !g_prev_key_state[k.vk]) {
            g_prev_key_state[k.vk] = true;

            char ch;
            if (shift) {
                ch = k.shift_char;
            }
            else {
                ch = k.plain_char;
            }

            std::string result;
            if (ctrl || alt) {
                // Wrap in <C-...> etc.
                result = "<";
                if (ctrl) result += "C-";
                if (alt)  result += "A-";
                // For punctuation, Neovim expects the actual char: <C-@>, <C-]>
                result += ch;
                result += ">";
            }
            else {
                // Plain character — send as single char string
                result = std::string(1, ch);
            }
            lua_pushstring(L, result.c_str());
            return 1;
        }
        g_prev_key_state[k.vk] = is_down;
    }


    // Check printable keys (0x20–0x5A is safe for A-Z, 0-9, symbols)
    for (UINT vk = 0x20; vk <= 0x5A; ++vk) {
        bool is_down = (GetAsyncKeyState(vk) & 0x8000) != 0;
        if (is_down && !g_prev_key_state[vk]) {
            g_prev_key_state[vk] = true;

            std::string result = "";
            if (ctrl || shift || alt) {
                result += "<";
                if (ctrl)  result += "C-";
                if (shift) result += "S-";
                if (alt)   result += "A-";

                if (vk >= 'A' && vk <= 'Z') {
                    result += (char)('a' + (vk - 'A'));
                }
                else {
                    result += (char)vk;
                }
                result += ">";
            }
            else {
                // Plain character: send as raw char (critical for Neovim)
                if (vk >= 'A' && vk <= 'Z') {
                    // Without modifiers, GetAsyncKeyState(vk) for 'A' means 'A' key,
                    // but actual char depends on caps/shift. For simplicity, assume
                    // user wants lowercase unless shift is held.
                    // But since we already handled shift above, here shift=false.
                    result += (char)('a' + (vk - 'A'));
                }
                else {
                    result += (char)vk;
                }
            }
            lua_pushstring(L, result.c_str());
            return 1;
        }
        g_prev_key_state[vk] = is_down;
    }

    // Also update state for modifier keys themselves (so they don't "stick")
    g_prev_key_state[VK_CONTROL] = ctrl;
    g_prev_key_state[VK_SHIFT] = shift;
    g_prev_key_state[VK_MENU] = alt;
    g_prev_key_state[VK_LWIN] = (GetAsyncKeyState(VK_LWIN) & 0x8000) != 0;
    g_prev_key_state[VK_RWIN] = (GetAsyncKeyState(VK_RWIN) & 0x8000) != 0;

    lua_pushnil(L);
    return 1;
}


// State tracking for key press detection (shared with other key functions if needed)
static bool g_prev_vk_state[256] = { false };

// Helper: format modifiers + char into Neovim key notation
static std::string format_nvim_key(bool ctrl, bool alt, bool shift, const std::string& base) {
    if (!ctrl && !alt) {
        // No wrapping for plain or Shift-only (Shift is implicit in char)
        return base;
    }
    std::string out = "<";
    if (ctrl) out += "C-";
    if (alt)  out += "A-";
    // Note: Shift is already reflected in the character (e.g., ':' vs ';')
    out += base;
    out += ">";
    return out;
}

// New Lua C function: get_key_press_unicode
static int l_get_key_press_unicode(lua_State* L) {
    // Optional: only respond if REAPER is foreground
    HWND reaperHwnd = FindWindowA("REAPERMainWindow", NULL);
    if (!reaperHwnd || GetForegroundWindow() != reaperHwnd) {
        lua_pushnil(L);
        return 1;
    }

    // Special virtual keys (non-printable)
    struct SpecialKey {
        UINT vk;
        const char* nvim_name;
    };
    static const SpecialKey special_keys[] = {
        {VK_ESCAPE, "Esc"}, {VK_RETURN, "CR"}, {VK_TAB, "Tab"}, {VK_BACK, "BS"},
        {VK_SPACE, "Space"}, {VK_LEFT, "Left"}, {VK_RIGHT, "Right"},
        {VK_UP, "Up"}, {VK_DOWN, "Down"}, {VK_HOME, "Home"}, {VK_END, "End"},
        {VK_INSERT, "Insert"}, {VK_DELETE, "Del"}, {VK_PRIOR, "PageUp"},
        {VK_NEXT, "PageDown"},
        {VK_F1, "F1"}, {VK_F2, "F2"}, {VK_F3, "F3"}, {VK_F4, "F4"},
        {VK_F5, "F5"}, {VK_F6, "F6"}, {VK_F7, "F7"}, {VK_F8, "F8"},
        {VK_F9, "F9"}, {VK_F10, "F10"}, {VK_F11, "F11"}, {VK_F12, "F12"},
    };

    // Check special keys first (arrows, Esc, F1–F12, etc.)
    for (const auto& k : special_keys) {
        bool is_down = (GetAsyncKeyState(k.vk) & 0x8000) != 0;
        if (is_down && !g_prev_vk_state[k.vk]) {
            g_prev_vk_state[k.vk] = true;

            bool ctrl = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
            bool alt = (GetAsyncKeyState(VK_MENU) & 0x8000) != 0;
            bool shift = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;

            std::string key_str = format_nvim_key(ctrl, alt, shift, k.nvim_name);
            lua_pushstring(L, key_str.c_str());
            return 1;
        }
        g_prev_vk_state[k.vk] = is_down;
    }

    // Check all possible virtual keys that can produce characters
    // Focus on common range: 0x08–0xFF (covers most OEM and alphanumeric)
    for (UINT vk = 0x08; vk <= 0xFF; ++vk) {
        // Skip special keys already handled
        if ((vk >= VK_F1 && vk <= VK_F24) ||
            (vk >= VK_LEFT && vk <= VK_DOWN) ||
            vk == VK_ESCAPE || vk == VK_RETURN || vk == VK_TAB || vk == VK_BACK ||
            vk == VK_SHIFT || vk == VK_CONTROL || vk == VK_MENU) {
            continue;
        }

        bool is_down = (GetAsyncKeyState(vk) & 0x8000) != 0;
        if (!is_down || g_prev_vk_state[vk]) {
            g_prev_vk_state[vk] = is_down;
            continue;
        }

        // Rising edge: new key press
        g_prev_vk_state[vk] = true;

        // Get current keyboard state
        BYTE keystate[256];
        if (!GetKeyboardState(keystate)) {
            continue;
        }

        // Try ToUnicode (supports dead keys, layouts, etc.)
        wchar_t wch[10] = { 0 };
        int result = ToUnicode(vk, MapVirtualKey(vk, MAPVK_VK_TO_VSC), keystate, wch, 10, 0);

        if (result > 0) {
            // Convert to UTF-8
            char utf8[32] = { 0 };
            int utf8_len = WideCharToMultiByte(CP_UTF8, 0, wch, result, utf8, sizeof(utf8) - 1, NULL, NULL);
            if (utf8_len > 0) {
                std::string char_str(utf8, utf8_len);

                // Handle Ctrl/Alt
                bool ctrl = (keystate[VK_CONTROL] & 0x80) != 0;
                bool alt = (keystate[VK_MENU] & 0x80) != 0;

                // For Ctrl+letter, Neovim expects lowercase (e.g., <C-d>)
                if (ctrl && !alt && char_str.length() == 1) {
                    unsigned char c = (unsigned char)char_str[0];
                    if (c >= 1 && c <= 26) {
                        // Ctrl+letter produces control codes (e.g., Ctrl+A = 1)
                        char ctrl_char = 'a' + (c - 1);
                        char_str = std::string(1, ctrl_char);
                    }
                }

                std::string key_str = format_nvim_key(ctrl, alt, false, char_str);
                lua_pushstring(L, key_str.c_str());
                return 1;
            }
        }
    }

    // Update modifier key states to avoid "sticky" detection
    g_prev_vk_state[VK_CONTROL] = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
    g_prev_vk_state[VK_SHIFT] = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
    g_prev_vk_state[VK_MENU] = (GetAsyncKeyState(VK_MENU) & 0x8000) != 0;

    lua_pushnil(L);
    return 1;
}
