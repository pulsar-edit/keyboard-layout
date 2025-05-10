const KeyboardLayout = require('./lib/keyboard-layout');
console.log('LOADED!');

console.log('Current layout:', KeyboardLayout.getCurrentKeyboardLayout());

setTimeout(() => {
  console.log('aha!');
}, 5000)
