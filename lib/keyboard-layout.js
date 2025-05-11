'use strict'

const Emitter = require('event-kit').Emitter
const { KeyboardLayoutManager } = require('../build/Release/keyboard-layout-manager.node')

const emitter = new Emitter()
const keymapsByLayoutName = new Map()

const manager = new KeyboardLayoutManager(() => {
  let current = getCurrentKeyboardLayout();
  emitter.emit('did-change-current-keyboard-layout', current)
})

function getCurrentKeymap () {
  const currentLayout = getCurrentKeyboardLayout()
  let currentKeymap = keymapsByLayoutName.get(currentLayout)
  if (!currentKeymap) {
    currentKeymap = manager.getCurrentKeymap()
    keymapsByLayoutName.set(currentLayout, currentKeymap)
  }
  return currentKeymap
}

function getCurrentKeyboardLayout () {
   return manager.getCurrentKeyboardLayout()
}

function getCurrentKeyboardLanguage () {
  return manager.getCurrentKeyboardLanguage()
}

function getInstalledKeyboardLanguages () {
  var languages = new Set();

  for (var language of (manager.getInstalledKeyboardLanguages() || [])) {
    languages.add(language)
  }

  return Array.from(languages)
}

function onDidChangeCurrentKeyboardLayout (callback) {
  return emitter.on('did-change-current-keyboard-layout', callback)
}

function observeCurrentKeyboardLayout (callback) {
  callback(getCurrentKeyboardLayout())
  return onDidChangeCurrentKeyboardLayout(callback)
}

// Add this to your KeyboardLayout JS wrapper
function diagnose() {
  // Get a list of active handles
  const activeHandles = process._getActiveHandles();
  console.log('Active handles:', activeHandles.length);
  activeHandles.forEach((handle, i) => {
    console.log(`Handle ${i}:`, handle.constructor.name);
  });

  // Get active requests
  const activeRequests = process._getActiveRequests();
  console.log('Active requests:', activeRequests.length);

  // Force a garbage collection (if --expose-gc is enabled)
  if (global.gc) {
    console.log('Forcing garbage collection...');
    global.gc();
  }
}

module.exports = {
  diagnose,
  getCurrentKeymap,
  getCurrentKeyboardLayout,
  getCurrentKeyboardLanguage,
  getInstalledKeyboardLanguages,
  onDidChangeCurrentKeyboardLayout,
  observeCurrentKeyboardLayout
}
