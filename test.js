const KeyboardLayout = require('./lib/keyboard-layout');
console.log('LOADED!');

// Force immediate exit
// process.exit(0);
// console.log('Current layout:', KeyboardLayout.getCurrentKeyboardLayout());
//
// console.log('Process should exit now...');
// // Give it a moment to try to exit naturally
// setTimeout(() => {
//   console.log('Process is still running after 1 second');
//   // Use internal Node.js API to check what might be keeping it alive
//   try {
//     const handles = process._getActiveHandles();
//     console.log(`${handles.length} active handles`);
//     handles.forEach((h, i) => console.log(`Handle ${i} type:`, h.constructor.name));
//
//     const requests = process._getActiveRequests();
//     console.log(`${requests.length} active requests`);
//
//     // Try forcing exit
//     process.exit(0);
//   } catch (e) {
//     console.error('Error in diagnostics:', e);
//     process.exit(1);
//   }
// }, 1000);
