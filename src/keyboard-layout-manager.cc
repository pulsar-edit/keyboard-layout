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
    env,                        // environment
    callback.Value(),           // js_callback
    "keyboard-layout-listener", // resource_name
    0,                          // max_queue_size
    1,                          // initial_thread_count
    this,
    [](Napi::Env, void *) {     // finalize_cb
                                // No-op finalize callback
    },
    this
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


void KeyboardLayoutManager::OnNotificationReceived() {

  napi_status status = tsfn.NonBlockingCall(
    this,
    [](Napi::Env env, Napi::Function jsCallback, KeyboardLayoutManager* that) {
      fprintf(stderr, "ThreadSafeFunction callback executing.\n");

      // Try to get the instance data
      // auto that = env.GetInstanceData<KeyboardLayoutManager>();
      if (that) {
        fprintf(stderr, "Successfully retrieved instance data\n");

        Napi::Value current = that->GetCurrentKeyboardLayout(env);

        // Still use a hard-coded string for now
        // Napi::String layout = Napi::String::New(env, "test_with_instance_data");
        jsCallback.Call({current});
      } else {
        fprintf(stderr, "Failed to get instance data\n");

        // Fall back to a different string so we can tell the difference
        Napi::String layout = Napi::String::New(env, "no_instance_data");
        jsCallback.Call({layout});
      }
    }
  );

  fprintf(stderr, "ThreadSafeFunction call status: %s\n",
          status == napi_ok ? "OK" : "ERROR");
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
