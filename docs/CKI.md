# cki UTF-8 Editor

`cki` is CKLOS's non-modal full-screen text editor. It is launched from the
shell with:

```text
CKLOS> cki /home/cklos/notes.txt
```

The editor uses the VBE linear framebuffer, GNU Unifont glyphs for non-ASCII
text and a normal UTF-8 byte stream on disk. Chinese text, Chinese paths, line
numbers, scrolling and cursor positioning are supported. CRLF input is
normalized to LF on load.

Key bindings:

- `Ctrl+S`: save
- `Ctrl+O`: open another path
- `Ctrl+F`: find
- `Ctrl+Z`: undo the most recent insertion or deletion
- `Ctrl+Q`: quit
- Arrow keys, Home, End, Page Up and Page Down
- Backspace removes a complete UTF-8 code point
- A mouse click positions the text caret

The maximum editable file size is 8 MiB. The current editor is a privileged
kernel GUI service; moving it behind a ring-3 windowing protocol remains part
of the future compositor work.

Unicode data is generated from GNU Unifont 18.0.01 and its WQY CJK bitmap
source. The license is installed at
`/usr/share/licenses/unifont/COPYING`.
