const KeyboardLayout = require('./lib/keyboard-layout');

console.log('Current layout:', KeyboardLayout.getCurrentKeyboardLayout());

KeyboardLayout.onDidChangeCurrentKeyboardLayout((current) => {
  console.log('CHANGED!!!!', current);
});

setTimeout(() => {
  console.log('aha!');
}, 25000)
