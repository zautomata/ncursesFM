## TODO

**DEVICE MODE**:  

- [ ] Add available space for mounted fs?
- [ ] Move available space (and other info, may be, such as mount opts) to show_stat?

**SYSINFO MONITOR**:  

- [x] make layout configurable, eg: CPB -> clock, processes, batteries, or BPC -> batteries, processes, clock, etc etc

**HELPER SCREEN**:  

- [x] fix when fullname_win is shown
- [x] fix: bookmarks mode will leave with change_dir if enter is pressed
- [x] fix: switch helper screen when changing active tab mode

**UNICODE**:  

- [x] fix string printing after resize
- [x] fix creating file/folders after ask_user
- [x] fix 2 valgrind errors...
- [ ] make ask_user printed question a wchar* (initial work for internationalization: in other languages, that string can be a multibyte string too)
- [x] implements insert mode too (KEY_IC) when pressing insert
- [x] implement http://man7.org/linux/man-pages/man3/wcwidth.3.html (in some lang, some chars may fill more than one column...)
- [x] fix issue with other threads moving away input cursor
- [x] add support for key_left/right/del buttons to ask_user

**IMAGE PREVIEW**:  

- [x] fix first line while scrolling down (xterm only)
- [ ] understand why konsole is perfect only when scrolling...

**TEXT/PDF PREVIEW**:  

- [ ] understand how that can be implemented

**VARIOUS**:  

- [ ] make strings tranlatable
- [x] switch to KEY_ESC to leave ncursesFM/current mode/leave input mode in ask_user
- [ ] vi keybindings/keybinding profiles
- [ ] switch back to libmagic mimetype support? (it is buggy...as of now it freezes on folders. Plus, i cannot use it for distro packages detection, neither for search_inside_archive because it is so slooooooooow)
- [x] per-user config
- [ ] add some wiki to lighten readme
- [ ] cleanup code (lots of features added recently)
- [ ] update doc