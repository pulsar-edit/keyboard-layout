const KeyboardLayout = require('./lib/keyboard-layout')


let listener = KeyboardLayout.onDidChangeCurrentKeyboardLayout((current) => {
  console.log('AHA!', current);
});

console.log('Current layout:', KeyboardLayout.getCurrentKeyboardLayout());

setTimeout(() => {
  console.log('Done!');
  listener.dispose();
}, 30000);
