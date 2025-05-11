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

  auto fn = info[0].As<Napi::Function>();
  callback = Napi::Persistent(fn);
  std::cout << "Declaring tsfn!" << std::endl;
  tsfn = Napi::ThreadSafeFunction::New(
    env,
    callback.Value(),
    "keyboard-layout-listener",
    0,
    1
  );

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

  Napi::Object global = env.Global();
  // Create args array
  std::vector<napi_value> args = {current};

  // Call with explicit this
  callback.Call(global, args);
  // callback.Call({current});
}

// Runs on a background thread.
void KeyboardLayoutManager::OnNotificationReceived() {
  // We don't need to send any arguments; we just need to signal the main
  // thread.
  tsfn.BlockingCall(KeyboardLayoutManager::ProcessCallback);
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
