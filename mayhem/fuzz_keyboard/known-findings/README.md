`Keyboard::virtualKeyToString()` (a debug helper) indexes its name table with an unchecked `VirtualKey`
value, so a `VirtualKey` outside the table reads out of bounds. The harness deliberately does not call
the helper; the scancode decoder itself fuzzes clean. No reproducer is kept: the helper is not on the
untrusted-input path.
