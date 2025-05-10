#include "keyboard-layout-manager.h"

#include <xkbcommon/xkbcommon.h>
#include <X11/XKBlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/XKBrules.h>
#include <sys/mman.h>
#include <unistd.h>
#include <cwctype>
#include <cctype>
#include <stdio.h>
#include <iostream>
#include <locale.h>

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


// REGISTRY LISTENER
// =================

static void registry_global(void *data, struct wl_registry *registry, uint32_t name, const char *interface, uint32_t version) {
  std::cout << "Registry global!" << std::endl;
  WaylandKeymapContext *ctx = (WaylandKeymapContext *)data;
  if (strcmp(interface, "wl_seat") == 0) {
    std::cout << "Binding seat!" << std::endl;
    ctx->seat = (struct wl_seat*)wl_registry_bind(registry, name, &wl_seat_interface, 1);
    if (ctx->seat) {
      std::cout << "Seat bound! Getting keyboard…" << std::endl;
      ctx->keyboard = wl_seat_get_keyboard(ctx->seat);
      std::cout << "…done!" << std::endl;
    } else {
      std::cout << "Failed!" << std::endl;
    }
  }
}

static void registry_global_remove(void *data, struct wl_registry *registry,
                                   uint32_t name) {
  // Not used
}

static const struct wl_registry_listener registry_listener = {
    registry_global, registry_global_remove};



// KEYBOARD LISTENER
// =================

// Keyboard listener callbacks
static void keyboard_keymap(void *data, struct wl_keyboard *keyboard,
                            uint32_t format, int32_t fd, uint32_t size) {

  std::cout << "In keyboard_keymap!" << std::endl;
  WaylandKeymapContext *ctx = (WaylandKeymapContext *)data;

  if (format != WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1) {
    close(fd);
    return;
  }

  char *keymap_string = (char *)mmap(NULL, size, PROT_READ, MAP_PRIVATE, fd, 0);
  if (keymap_string == MAP_FAILED) {
    close(fd);
    return;
  }

  ctx->xkb_keymap = xkb_keymap_new_from_string(ctx->xkb_context, keymap_string,
                                              XKB_KEYMAP_FORMAT_TEXT_V1,
                                              XKB_KEYMAP_COMPILE_NO_FLAGS);

  munmap(keymap_string, size);
  close(fd);

  if (!ctx->xkb_keymap)
    return;

  ctx->xkb_state = xkb_state_new(ctx->xkb_keymap);

  if (!ctx->xkb_state)
    return;

  // Get shift mask
  xkb_mod_index_t shift_idx =
      xkb_keymap_mod_get_index(ctx->xkb_keymap, XKB_MOD_NAME_SHIFT);
  if (shift_idx != XKB_MOD_INVALID) {
    ctx->shift_mask = 1 << shift_idx;
  }

  // Try to find AltGr (ISO Level3 Shift) - this varies by layout
  const char *alt_gr_names[] = {"ISO_Level3_Shift", "Mode_switch", "Alt",
                                "AltGr"};

  size_t alt_gr_length = sizeof(alt_gr_names) / sizeof(alt_gr_names[0]);
  for (size_t i = 0; i < alt_gr_length; i++) {
    xkb_mod_index_t idx =
        xkb_keymap_mod_get_index(ctx->xkb_keymap, alt_gr_names[i]);
    if (idx != XKB_MOD_INVALID) {
      ctx->alt_gr_mask = 1 << idx;
      break;
    }
  }

  ctx->keymap_received = true;
}

static void keyboard_enter(void *data, struct wl_keyboard *keyboard,
                           uint32_t serial, struct wl_surface *surface,
                           struct wl_array *keys) {
  // Not used
}

static void keyboard_leave(void *data, struct wl_keyboard *keyboard,
                           uint32_t serial, struct wl_surface *surface) {
  // Not used
}

static void keyboard_key(void *data, struct wl_keyboard *keyboard,
                         uint32_t serial, uint32_t time, uint32_t key,
                         uint32_t state) {
  // Not used
}

static void keyboard_modifiers(void *data, struct wl_keyboard *keyboard,
                               uint32_t serial, uint32_t mods_depressed,
                               uint32_t mods_latched, uint32_t mods_locked,
                               uint32_t group) {
  // Not used
}

static void keyboard_repeat_info(void *data, struct wl_keyboard *keyboard,
                                 int32_t rate, int32_t delay) {
  // Not used
}

static const struct wl_keyboard_listener keyboard_listener = {
  keyboard_keymap,
  keyboard_enter,
  keyboard_leave,
  keyboard_key,
  keyboard_modifiers,
  keyboard_repeat_info
};

static void FailOnWaylandSetup(Napi::Env env) {
  Napi::Error::New(env, "Failed to connect to Wayland display").ThrowAsJavaScriptException();
}

static void CleanupWaylandContext(WaylandKeymapContext* ctx) {
  if (ctx->xkb_state)
      xkb_state_unref(ctx->xkb_state);
  if (ctx->xkb_keymap)
      xkb_keymap_unref(ctx->xkb_keymap);
  if (ctx->xkb_context)
      xkb_context_unref(ctx->xkb_context);
  if (ctx->keyboard)
      wl_keyboard_destroy(ctx->keyboard);
  if (ctx->seat)
      wl_seat_destroy(ctx->seat);
  if (ctx->registry)
      wl_registry_destroy(ctx->registry);
  if (ctx->display) {
      wl_display_roundtrip(ctx->display);
      wl_display_disconnect(ctx->display);
  }
}

void KeyboardLayoutManager::PlatformSetup(const Napi::CallbackInfo& info) {
  auto env = info.Env();

  // isWayland = detect_display_server() == 1;
  isWayland = true;

  if (isWayland) {
    std::cout << "in PlatformSetup!" << std::endl;
    waylandContext = new WaylandKeymapContext();
    memset(waylandContext, 0, sizeof(WaylandKeymapContext));
    std::cout << "memset!" << std::endl;

    waylandContext->display = wl_display_connect(NULL);
    if (!waylandContext->display) {
      std::cout << "Oof 1!" << std::endl;
      FailOnWaylandSetup(env);
      return;
    }

    std::cout << "Got this far 00!" << std::endl;

    waylandContext->xkb_context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    if (!waylandContext->xkb_context) {
      std::cout << "Oof 2!" << std::endl;
      wl_display_disconnect(waylandContext->display);
      FailOnWaylandSetup(env);
      return;
    }

    std::cout << "Got this far 0!" << std::endl;

    waylandContext->registry = wl_display_get_registry(waylandContext->display);
    std::cout << "Listener!" << std::endl;
    wl_registry_add_listener(waylandContext->registry, &registry_listener, waylandContext);

    // Process registry events.
    wl_display_roundtrip(waylandContext->display);

    std::cout << "Got this far 1!" << std::endl;

    // If a seat was found, add a keyboard listener.
    if (waylandContext->keyboard) {
      wl_keyboard_add_listener(
        waylandContext->keyboard,
        &keyboard_listener,
        waylandContext
      );
    } else {
      std::cout << "Oof 3!" << std::endl;
      CleanupWaylandContext(waylandContext);
      FailOnWaylandSetup(env);
      return;
    }

    std::cout << "Got this far 2!" << std::endl;
    // Wait for the keymap to be received.
    while (!waylandContext->keymap_received) {
      if (wl_display_dispatch(waylandContext->display) < 0) {
        CleanupWaylandContext(waylandContext);
        FailOnWaylandSetup(env);
        return;
      }
    }

    std::cout << "Miracle!" << std::endl;
    // We're good. We can exit.
    return;
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
    // TODO
    return env.Null();
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
  xkb_state *xkbState,
  uint32_t xkbKeycode,
  uint32_t state
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

static char* get_key_char(WaylandKeymapContext *ctx, uint32_t keycode, xkb_mod_mask_t modifiers) {
  // XKB keycodes are offset by 8 from evdev keycodes.
  xkb_keycode_t xkb_keycode = keycode + 8;

  // Create a copy of the XKB state so we can apply modifiers.
  struct xkb_state *temp_state = xkb_state_new(ctx->xkb_keymap);
  if (!temp_state) {
    return strdup("error");
  }

  xkb_state_update_mask(temp_state, modifiers, 0, 0, 0, 0, 0);

  xkb_keysym_t keysym = xkb_state_key_get_one_sym(temp_state, xkb_keycode);

  // Allocate memory for the result.
  char *result = new char[32]; // And remember to use delete[] instead of free

  // Try to get a UTF-8 representation
  int len = xkb_keysym_to_utf8(keysym, buffer, 32);

  if (!result) {
    xkb_state_unref(temp_state);
    delete[] result;
    return NULL;
  }

  // Convert keysym to UTF-8.
  if (keysym == XKB_KEY_NoSymbol) {
    strcpy(result, "Dead");
  } else if (len <= 0) {
    // If we couldn't get a UTF-8 character, fall back to the keysym’s name.
    xkb_keysym_get_name(keysym, result, 32);
  }

  xkb_state_unref(temp_state);
  return result;
}

static Napi::Value WaylandCharacterForCode(Napi::Env env, WaylandKeymapContext *ctx, uint32_t keycode, xkb_mod_mask_t modifiers) {
  char *result = get_key_char(ctx, keycode, modifiers);
  if (result) {
    auto wrappedResult = Napi::String::New(env, result);
    delete[] result;
    return wrappedResult;
  } else {
    return env.Null();
  }
}

Napi::Value KeyboardLayoutManager::GetCurrentKeymap(const Napi::CallbackInfo& info) {
  auto env = info.Env();
  Napi::Object result = Napi::Object::New(env);
  Napi::String unmodifiedKey = Napi::String::New(env, "unmodified");
  Napi::String withShiftKey = Napi::String::New(env, "withShift");
  Napi::String withAltGraphKey = Napi::String::New(env, "withAltGraph");
  Napi::String withAltGraphShiftKey = Napi::String::New(env, "withAltGraphShift");

  if (isWayland) {
    size_t keyCodeMapSize = sizeof(keyCodeMap) / sizeof(keyCodeMap[0]);
    for (size_t i = 0; i < keyCodeMapSize; i++) {
      const char *dom3Code = keyCodeMap[i].dom3Code;
      uint xkbKeycode = keyCodeMap[i].xkbKeycode;
      if (dom3Code && xkbKeycode > 0x0000) {
        Napi::String dom3CodeKey = Napi::String::New(env, dom3Code);
        Napi::Value unmodified = WaylandCharacterForCode(env, waylandContext, xkbKeycode, 0);
        Napi::Value withShift =
            WaylandCharacterForCode(env, waylandContext, xkbKeycode, waylandContext->shift_mask);
        Napi::Value withAltGraph = WaylandCharacterForCode(
            env, waylandContext, xkbKeycode, waylandContext->alt_gr_mask);
        Napi::Value withAltGraphShift = WaylandCharacterForCode(
            env, waylandContext, xkbKeycode,
            waylandContext->shift_mask | waylandContext->alt_gr_mask);

        if (unmodified.IsString() || withShift.IsString() ||
            withAltGraph.IsString() || withAltGraphShift.IsString()) {
          Napi::Object entry = Napi::Object::New(env);
          (entry).Set(unmodifiedKey, unmodified);
          (entry).Set(withShiftKey, withShift);
          (entry).Set(withAltGraphKey, withAltGraph);
          (entry).Set(withAltGraphShiftKey, withAltGraphShift);
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
