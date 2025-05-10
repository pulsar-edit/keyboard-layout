const KeyboardLayout = require('./lib/keyboard-layout')


let listener = KeyboardLayout.onDidChangeCurrentKeyboardLayout((current) => {
  console.log('AHA!', current);
});

setTimeout(() => {
  console.log('Done!');
  listener.dispose();
}, 5000);

console.log('Current layout:', KeyboardLayout.getCurrentKeyboardLayout());
