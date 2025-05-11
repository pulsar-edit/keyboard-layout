#include "keyboard-layout-manager.h"
#include <iostream>
#ifdef DEBUG
#endif

KeyboardLayoutManager::KeyboardLayoutManager(const Napi::CallbackInfo& info):
 Napi::ObjectWrap<KeyboardLayoutManager>(info),
 _env(info.Env()) {
  #if defined(__linux__) || defined(__FreeBSD__)
    xInputContext = nullptr;
    xInputMethod = nullptr;
  #endif

  auto env = info.Env();
  _env = env;
  CHECK_VOID(
    info[0].IsFunction(),
    "Expected function as first argument",
    env
  );

  // In constructor
  auto fn = info[0].As<Napi::Function>();
  callback = Napi::Persistent(fn);

  // Log debug info
  fprintf(stderr, "Creating ThreadSafeFunction. Callback is %s\n",
          callback.IsEmpty() ? "empty" : "valid");

  // Create the ThreadSafeFunction with more explicit parameters
  tsfn = Napi::ThreadSafeFunction::New(
    env,                          // environment
    callback.Value(),             // js_callback
    "keyboard-layout-listener",   // resource_name
    0,                            // max_queue_size
    1                             // initial_thread_count
  );

  // auto fn = info[0].As<Napi::Function>();
  // callback = Napi::Persistent(fn);
  // tsfn = Napi::ThreadSafeFunction::New(
  //   env,
  //   callback.Value(),
  //   "keyboard-layout-listener",
  //   0,
  //   1
  // );

  callback.Unref();
  tsfn.Unref(env);

  std::cout << "Unreffed the tsfn!" << std::endl;

  env.SetInstanceData<KeyboardLayoutManager>(this);

  env.AddCleanupHook([this]() {
    this->Cleanup();
  });

  PlatformSetup(info);
}

// Runs on the main thread.
void KeyboardLayoutManager::ProcessCallback(
  Napi::Env env,
  Napi::Function callback
) {
  auto that = env.GetInstanceData<KeyboardLayoutManager>();
  auto current = that->GetCurrentKeyboardLayout(env);

  if (current.IsString()) {
    Napi::String str = current.As<Napi::String>();
    std::string value = str.Utf8Value();
    std::cout << "Sanity check: value is " << value << std::endl;
  } else {
    std::cout << "Sanity check: is NOT a string!";
  }

  // Create arguments array with the layout
  std::vector<napi_value> args = { current };

  // Call JS callback with explicit this and args
  napi_value global;
  napi_get_global(env, &global);

  napi_value result;
  napi_call_function(env, global, callback, 1, args.data(), &result);

  std::cout << "Weird Result: " << result << std::endl;

  // Napi::Object global = env.Global();
  // callback.MakeCallback(global, {current.As<Napi::String>()});
  // callback.Call({current});
}

// Static callback that doesn't rely on GetCurrentKeyboardLayout
static void LayoutChangeCallback(Napi::Env env, Napi::Function jsCallback) {
  // Create a handle scope for this execution context
  Napi::HandleScope scope(env);

  // Create a hard-coded string value directly here
  Napi::String layout = Napi::String::New(env, "test_direct_layout");

  // Log before calling
  fprintf(stderr, "About to call JS callback with direct layout: %s\n", "test_direct_layout");

  // Call with explicit arguments
  jsCallback.Call({layout});
}


// Runs on a background thread.
void KeyboardLayoutManager::OnNotificationReceived() {
  // Create data - even though we're not using it, it helps with debugging
  const char* marker = "LAYOUT_CHANGE_EVENT";

  // More explicit call
  napi_status status = tsfn.NonBlockingCall(
    const_cast<char*>(marker),  // "data" to pass (just a marker)
    [](Napi::Env env, Napi::Function jsCallback, char* data) {
      // Extra debug info
      fprintf(stderr, "ThreadSafeFunction callback executing. Data marker: %s\n", data);

      // Create a simple hard-coded value
      Napi::String layout = Napi::String::New(env, "test_nonblocking_call");

      // Call with this value
      jsCallback.Call({layout});
    }
  );
  // We don't need to send any arguments; we just need to signal the main
  // thread.
  // tsfn.BlockingCall(
  //     "layout_change",
  //     [](Napi::Env env, Napi::Function jsCallback, const char *event_type) {
  //       if (strcmp(event_type, "layout_change") == 0) {
  //         // Create a string argument
  //         Napi::String arg = Napi::String::New(env, "test_layout_value");
  //
  //         // Try calling with this test value first
  //         jsCallback.Call({arg});
  //       }
  //     });
  // tsfn.BlockingCall(LayoutChangeCallback);
}

void KeyboardLayoutManager::Cleanup() {
  std::cout << "Cleanup!" << std::endl;
  callback.Reset();
  if (isFinalizing) return;
  tsfn.Abort();

  PlatformTeardown();
}

KeyboardLayoutManager::~KeyboardLayoutManager() {
  std::cout << "Destructing!" << std::endl;
  isFinalizing = true;
  Cleanup();
}

void KeyboardLayoutManager::Init(Napi::Env env, Napi::Object exports) {
#ifdef DEBUG
  std::cout << "KeyboardLayoutManager::Init" << std::endl;
#endif

  Napi::Function func = DefineClass(env, "KeyboardLayoutManager", {
    InstanceMethod<&KeyboardLayoutManager::GetCurrentKeyboardLayout>("getCurrentKeyboardLayout", napi_default_method),
    InstanceMethod<&KeyboardLayoutManager::GetCurrentKeyboardLanguage>("getCurrentKeyboardLanguage", napi_default_method),
    InstanceMethod<&KeyboardLayoutManager::GetInstalledKeyboardLanguages>("getInstalledKeyboardLanguages", napi_default_method),
    InstanceMethod<&KeyboardLayoutManager::GetCurrentKeymap>("getCurrentKeymap", napi_default_method)
  });

  exports.Set("KeyboardLayoutManager", func);
}

Napi::Object Init(Napi::Env env, Napi::Object exports) {
  KeyboardLayoutManager::Init(env, exports);
  return exports;
}

NODE_API_MODULE(keyboard_layout_manager, Init)
