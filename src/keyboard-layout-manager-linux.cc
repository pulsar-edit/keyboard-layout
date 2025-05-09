#include "keyboard-layout-manager.h"

#include <xkbcommon/xkbcommon.h>
#include <X11/XKBlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/XKBrules.h>
#include <cwctype>
#include <cctype>
#include <stdio.h>
#include <iostream>
#include <locale.h>

// Function to get current keyboard layout using xkbcommon
static char* get_current_layout() {
    // Set locale to use system defaults
    setlocale(LC_ALL, "");

    // Create xkb context
    struct xkb_context *context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    if (!context) return strdup("unknown");

    // Get layout from environment or default rules
    const char *env_layout = getenv("XKB_DEFAULT_LAYOUT");
    const char *env_rules = getenv("XKB_DEFAULT_RULES");
    const char *env_model = getenv("XKB_DEFAULT_MODEL");
    const char *env_variant = getenv("XKB_DEFAULT_VARIANT");
    const char *env_options = getenv("XKB_DEFAULT_OPTIONS");

    struct xkb_rule_names names = {
        .rules = env_rules,
        .model = env_model,
        .layout = env_layout,
        .variant = env_variant,
        .options = env_options
    };

    // Create keymap from names
    struct xkb_keymap *keymap =
        xkb_keymap_new_from_names(context, &names, XKB_KEYMAP_COMPILE_NO_FLAGS);

    // Default result
    char *result = strdup("unknown");

    if (keymap) {
        // Get the number of layouts (groups)
        xkb_layout_index_t num_layouts = xkb_keymap_num_layouts(keymap);

        if (num_layouts > 0) {
            // Get the name of the first layout
            const char *layout_name = xkb_keymap_layout_get_name(keymap, 0);
            if (layout_name) {
                free(result);
                result = strdup(layout_name);
            }
        }

        xkb_keymap_unref(keymap);
    }

    xkb_context_unref(context);
    return result;
}

// More robust detection combining multiple checks
static int detect_display_server() {
  // Method 1: XDG_SESSION_TYPE - Can be most reliable when set correctly
  const char *session_type = getenv("XDG_SESSION_TYPE");
  if (session_type != NULL) {
    if (strcmp(session_type, "wayland") == 0) {
      return 1; // Wayland
    } else if (strcmp(session_type, "x11") == 0) {
      return 0; // X11
    }
    // Other values like "tty" could exist
  }

  // Method 2 & 3: Check for environment variables
  const char *wayland_display = getenv("WAYLAND_DISPLAY");
  const char *x_display = getenv("DISPLAY");

  // Both could be set in some edge cases (X on Wayland or Wayland on X)
  if (wayland_display && strlen(wayland_display) > 0) {
    if (!(x_display && strlen(x_display) > 0)) {
      return 1; // Only Wayland is set
    }
  } else if (x_display && strlen(x_display) > 0) {
    return 0; // Only X11 is set
  }

  // If we get here, situation is ambiguous
  return -1;
}

void KeyboardLayoutManager::PlatformSetup(const Napi::CallbackInfo& info) {
  auto env = info.Env();

  // isWayland = detect_display_server() == 1;
  isWayland = true;
  if (isWayland) {
    struct xkb_context *context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    struct xkb_keymap *keymap = nullptr;
    bool gotKeymap = GetWaylandKeymap(context, &keymap);
    if (gotKeymap) {
      xkbContext = context;
      xkbKeymap = keymap;
      return;
    }
    // KeyboardMonitor* monitor = calloc(1, sizeof(KeyboardMonitor));
    // if (!monitor) {
    //   return;
    // }
    //
    // monitor->xkb_context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    // if (!monitor->xkb_context) {
    //   free(monitor);
    //   return;
    // }
    //
    // monitor->display = wl_display_connect(NULL);
    // if (!monitor->display) {
    //   xkb_context_unref(monitor->xkb_context);
    //   free(monitor);
    //   return;
    // }
  }

  xDisplay = XOpenDisplay("");
  CHECK_VOID(
    xDisplay,
    "Could not connect to X display",
    env
  );

  xInputMethod = XOpenIM(xDisplay, 0, 0, 0);
  if (!xInputMethod) return;

  XIMStyles* styles = 0;
  if (XGetIMValues(xInputMethod, XNQueryInputStyle, &styles, NULL) || !styles) {
    return;
  }

  XIMStyle bestMatchStyle = 0;
  for (int i = 0; i < styles->count_styles; i++) {
    XIMStyle thisStyle = styles->supported_styles[i];
    if (thisStyle == (XIMPreeditNothing | XIMStatusNothing))
    {
      bestMatchStyle = thisStyle;
      break;
    }
  }
  XFree(styles);
  if (!bestMatchStyle) return;

  Window window;
  int revert_to;
  XGetInputFocus(xDisplay, &window, &revert_to);
  if (window != BadRequest) {
    xInputContext = XCreateIC(
      xInputMethod, XNInputStyle, bestMatchStyle, XNClientWindow, window,
      XNFocusWindow, window, NULL
    );
  }
}

void KeyboardLayoutManager::PlatformTeardown() {
  if (xkbState) {
    xkb_state_unref(xkbState);
  }

  if (xkbKeymap) {
    xkb_keymap_unref(xkbKeymap);
  }

  if (xkbContext) {
    xkb_context_unref(xkbContext);
  }

  std::cout << "Teardown!" << std::endl;
  if (xInputContext) {
    XDestroyIC(xInputContext);
  }
  std::cout << "Teardown 1" << std::endl;

  if (xInputMethod) {
    XCloseIM(xInputMethod);
  }

  std::cout << "Teardown 2" << std::endl;
  if (xDisplay) {
    XCloseDisplay(xDisplay);
  }
  callback.Reset();
  std::cout << "Teardown 3" << std::endl;
};

void KeyboardLayoutManager::HandleKeyboardLayoutChanged() {
}

Napi::Value KeyboardLayoutManager::GetCurrentKeyboardLayout(const Napi::CallbackInfo& info) {
  auto env = info.Env();
  Napi::HandleScope scope(env);
  Napi::Value result;

  if (isWayland) {
    // Wayland
    setlocale(LC_ALL, "");
    struct xkb_context *context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    if (context) {
      // Get layout from environment or default rules
      const char *env_layout = getenv("XKB_DEFAULT_LAYOUT");
      const char *env_rules = getenv("XKB_DEFAULT_RULES");
      const char *env_model = getenv("XKB_DEFAULT_MODEL");
      const char *env_variant = getenv("XKB_DEFAULT_VARIANT");
      const char *env_options = getenv("XKB_DEFAULT_OPTIONS");

      // std::cout << "Layout from env?" << env_layout << std::endl;

      struct xkb_rule_names names = {
        .rules = env_rules,
        .model = env_model,
        .layout = env_layout,
        .variant = env_variant,
        .options = env_options
      };

      struct xkb_keymap *keymap = xkb_keymap_new_from_names(
        context,
        &names,
        XKB_KEYMAP_COMPILE_NO_FLAGS
      );

      if (!keymap) {
        std::cout << "No keymaps!" << std::endl;
        result = env.Null();
      } else {
        std::cout << "Keymaps!" << std::endl;
        xkb_layout_index_t num_layouts = xkb_keymap_num_layouts(keymap);
        const char *layout_name = NULL;
        if (num_layouts > 0) {
          std::cout << "More than one layout: " << num_layouts << std::endl;
          for (int i = 0; i < num_layouts; i++) {
            std::cout << "Layout " << i << " is " << xkb_keymap_layout_get_name(keymap, i) << std::endl;
          }
          layout_name = xkb_keymap_layout_get_name(keymap, 0);

          std::cout << "First layout name: " << layout_name << std::endl;

          result = Napi::String::New(env, layout_name);

          std::cout << "Done with result!" << std::endl;

          // Add null checks before string construction
          // std::string layout_str = names.layout ? std::string(names.layout) : "unknown";
          // std::string variant_str = names.variant ? std::string(names.variant) : "";

          // if (!variant_str.empty()) {
          //   result = Napi::String::New(env, layout_str + "," + variant_str);
          // } else {
          //   result = Napi::String::New(env, layout_str);
          // }
        }
      }

      std::cout << "Unreffing…" << std::endl;
      xkb_keymap_unref(keymap);
      std::cout << "…unreffed!" << std::endl;
      xkb_context_unref(context);
    } else {
      result = env.Null();
    }
  } else {
    // X11
    XkbRF_VarDefsRec vdr;
    char *tmp = NULL;
    if (XkbRF_GetNamesProp(xDisplay, &tmp, &vdr) && vdr.layout) {
      XkbStateRec xkbState;
      XkbGetState(xDisplay, XkbUseCoreKbd, &xkbState);
      if (vdr.variant) {
        result = Napi::String::New(env, std::string(vdr.layout) + "," + std::string(vdr.variant) + " [" + std::to_string(xkbState.group) + "]");
      } else {
        result = Napi::String::New(env, std::string(vdr.layout) + " [" + std::to_string(xkbState.group) + "]");
      }
    } else {
      result = env.Null();
    }
  }

  std::cout << "Returning!" << std::endl;
  return result;
}

Napi::Value KeyboardLayoutManager::GetCurrentKeyboardLanguage(const Napi::CallbackInfo& info) {
  // No distinction between “language” and “layout” on Linux.
  return GetCurrentKeyboardLayout(info);
}

Napi::Value KeyboardLayoutManager::GetInstalledKeyboardLanguages(const Napi::CallbackInfo& info) {
  auto env = info.Env();
  Napi::HandleScope scope(env);
  return env.Undefined();
}

struct KeycodeMapEntry {
  uint xkbKeycode;
  const char *dom3Code;
};

#define USB_KEYMAP_DECLARATION static const KeycodeMapEntry keyCodeMap[] =
#define USB_KEYMAP(usb, evdev, xkb, win, mac, code, id) {xkb, code}

#include "keycode_converter_data.inc"


Napi::Value CharacterForNativeCodeWayland(
  Napi::Env env,
  xkb_context *xkbContext,
  xkb_keymap *xkbKeymap,
  xkb_state *xkbState
) {
  if (!xkbContext || !xkbKeymap || !xkbState) {
    return env.Null();
  }

  xkb_state_update_mask(xkbState, 0, 0, 0, 0, 0, 0);

  xkb_mod_mask_t mod_mask = 0;

  // Map standard modifiers
  struct {
    uint32_t x11_mask;
    const char* xkb_name;
  } modifiers[] = {
    { ShiftMask, XKB_MOD_NAME_SHIFT },
    { LockMask, XKB_MOD_NAME_CAPS },
    { ControlMask, XKB_MOD_NAME_CTRL },
    { Mod1Mask, XKB_MOD_NAME_ALT },
    // Mod5Mask is often ISO_Level3_Shift (AltGr)
    { Mod5Mask, "iso_level3_shift" }
  };

  for (const auto &mod : modifiers) {
    if (state & mod.x11_mask) {
      xkb_mod_index_t mod_idx = xkb_keymap_mod_get_index(xkbKeymap, mod.xkb_name);
      if (mod_idx != XKB_MOD_INVALID) {
        mod_mask |= (1 << mod_idx);
      }
    }
  }

  xkb_state_update_mask(xkbState, mod_mask, 0, 0, 0, 0, 0);

  xkb_keysym_t keysym = xkb_state_key_get_one_sym(xkbState, xkbKeycode);

  char buffer[8] = {0};
  int length = xkb_keysym_to_utf8(keysym, buffer, sizeof(buffer));

  if (length > 0 && !std::iscntrl(buffer[0])) {
    return Napi::String::New(env, std::string(buffer, length));
  } else {
    return env.Null();
  }

}

Napi::Value CharacterForNativeCode(Napi::Env env, XIC xInputContext, XKeyEvent *keyEvent, uint xkbKeycode, uint state) {
  keyEvent->keycode = xkbKeycode;
  keyEvent->state = state;

  if (xInputContext) {
    wchar_t characters[2];
    char utf8[MB_CUR_MAX * 2 + 1];
    int count = XwcLookupString(xInputContext, keyEvent, characters, 2, NULL, NULL);
    size_t len = wcstombs(utf8, characters, sizeof(utf8));
    if (len == (size_t)-1) {
      return env.Null();
    }

    if (count > 0 && !std::iswcntrl(characters[0])) {
      return Napi::String::New(
        env,
        std::string(utf8, len)
      );
    } else {
      return env.Null();
    }
  } else {
    // Graceful fallback for systems where no window is open or no input
    // context can be found.
    char characters[2];
    int count = XLookupString(keyEvent, characters, 2, NULL, NULL);
    if (count > 0 && !std::iscntrl(characters[0])) {
      return Napi::String::New(
        env,
        std::string(characters, count)
      );
    } else {
      return env.Null();
    }
  }
}

Napi::Value KeyboardLayoutManager::GetCurrentKeymap(const Napi::CallbackInfo& info) {
  auto env = info.Env();
  Napi::Object result = Napi::Object::New(env);
  Napi::String unmodifiedKey = Napi::String::New(env, "unmodified");
  Napi::String withShiftKey = Napi::String::New(env, "withShift");

  if (isWayland) {

    const char *xdg_runtime = getenv("XDG_RUNTIME_DIR");
    if (xdg_runtime);

    // Construct path to the active keymap if it exists
    std::string keymap_path = std::string(xdg_runtime) + "/keymap";
    FILE *f = fopen(keymap_path.c_str(), "r");
    if (!f) return false;

    // Read the keymap string
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);


    char *keymap_string = new char[size + 1];
    fread(keymap_string, 1, size, f);
    keymap_string[size] = '\0';
    fclose(f);

    // Create keymap from the string
    *keymap = xkb_keymap_new_from_string(context, keymap_string,
                                        XKB_KEYMAP_FORMAT_TEXT_V1,
                                        XKB_KEYMAP_COMPILE_NO_FLAGS);

    delete[] keymap_string;
    return (*keymap != nullptr);

    size_t keyCodeMapSize = sizeof(keyCodeMap) / sizeof(keyCodeMap[0]);
    for (size_t i = 0; i < keyCodeMapSizel i++) {
      const char *dom3Code = keyCodeMap[i].dom3Code;
      uint xkbKeyCode = keyCodeMap[i].xkbKeyCode;

      if (dom3Code && xkbKeyCode > 0x0000) {
        Napi::String dom3CodeKey = Napi::String::New(env, dom3Code);
        Napi::Value unmodified = CharacterForNativeCodeWayland(env, xkbContext, xkbKeymap, xkbState, xjbKeycode, keyboardBaseState);
        Napi::Value withShift = CharacterForNativeCodeWayland(env, xkbContext, xkbKeymap, xkbState, keyboardBaseState | ShiftMask);

        if (unmodified.IsString() || withShift.IsString()) {
          Napi::Object entry = Napi::Object::New(env);
          (entry).Set(unmodifiedKey, unmodified);
          (entry).Set(withShiftKey, withShift);
          (result).Set(dom3CodeKey, entry);
        }
      }
    }
  } else {
    // Clear cached keymap.
    XMappingEvent eventMap = {MappingNotify, 0, false, xDisplay, 0, MappingKeyboard, 0, 0};
    XRefreshKeyboardMapping(&eventMap);

    XkbStateRec xkbState;
    XkbGetState(xDisplay, XkbUseCoreKbd, &xkbState);
    uint keyboardBaseState = 0x0000;
    if (xkbState.group == 1) {
      keyboardBaseState = 0x2000;
    } else if (xkbState.group == 2) {
      keyboardBaseState = 0x4000;
    }

    // Set up an event to reuse across CharacterForNativeCode calls.
    XEvent event;
    memset(&event, 0, sizeof(XEvent));
    XKeyEvent* keyEvent = &event.xkey;
    keyEvent->display = xDisplay;
    keyEvent->type = KeyPress;

    size_t keyCodeMapSize = sizeof(keyCodeMap) / sizeof(keyCodeMap[0]);
    for (size_t i = 0; i < keyCodeMapSize; i++) {
      const char *dom3Code = keyCodeMap[i].dom3Code;
      uint xkbKeycode = keyCodeMap[i].xkbKeycode;

      if (dom3Code && xkbKeycode > 0x0000) {
        Napi::String dom3CodeKey = Napi::String::New(env, dom3Code);
        Napi::Value unmodified = CharacterForNativeCode(env, xInputContext, keyEvent, xkbKeycode, keyboardBaseState);
        Napi::Value withShift = CharacterForNativeCode(env, xInputContext, keyEvent, xkbKeycode, keyboardBaseState | ShiftMask);

        if (unmodified.IsString() || withShift.IsString()) {
          Napi::Object entry = Napi::Object::New(env);
          (entry).Set(unmodifiedKey, unmodified);
          (entry).Set(withShiftKey, withShift);
          (result).Set(dom3CodeKey, entry);
        }
      }
    }
  }

  return result;
}
