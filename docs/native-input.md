# Native input observation

The frontend uses WinUI 3 with Windows App SDK 2.6.4 and seven native
ScrollViewer containers. Version 0.5.38 still accepted pen clicks without
scrolling on the test machine, although the same pen could pan Windows
Settings. Native ScrollView in 0.5.37 had the same observed limitation.
Upgrading the runtime and switching controls did not establish a fix.

Version 0.5.39 can observe the real application input path. Diagnostics are
disabled unless `%LOCALAPPDATA%/OpenEGoHub/Cache/trace-native-input` exists at
frontend startup. The local output is `native-input.jsonl` in that directory,
limited to 80 records per frontend process. The marker is never packaged.
Remove it and restart the frontend after the observation session.

The observer records XAML device types, contact/button flags, original-source
runtime classes and ancestor manipulation settings, scroll ranges and native
manipulation events. It does not record coordinates, content text, device names
or addresses. Pointer movement only updates in-memory counters. File writing
runs on a separate temporary worker.

The observer does not change event handling, capture pointers, inject input,
call ScrollTo or change driver/system settings. WinUI remains responsible for
all gestures. An independent diagnostic helper failed during startup and was
not used as evidence of any physical input behavior.
