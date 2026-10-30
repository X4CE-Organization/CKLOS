#include "editor.h"
#include "fs2.h"
#include "gui.h"
#include "interrupts.h"
#include "keyboard.h"
#include "memory.h"
#include "mouse.h"
#include "serial.h"
#include "string.h"
#include "unicode.h"

#define EDITOR_MAX_FILE (8U * 1024U * 1024U)
#define EDITOR_UNDO_MAX 256U
#define EDITOR_PATH_MAX 4096U
#define EDITOR_TOP 68U

typedef struct {
    uint8_t *text;
    uint32_t size;
    uint32_t cursor;
    uint32_t desired_column;
    uint32_t top_line;
    uint32_t current_line;
    uint32_t current_column;
    bool modified;
    char path[EDITOR_PATH_MAX];
    char status[160];
    uint8_t undo_data[EDITOR_UNDO_MAX];
    uint32_t undo_position;
    uint32_t undo_size;
    bool undo_delete;
    bool undo_valid;
} editor_state;

static char last_saved_path[EDITOR_PATH_MAX];
static uint32_t last_saved_size;
static bool last_save_valid;

static bool resolve_user_path(const char *path, char *output,
                              uint32_t capacity) {
    const char *prefix = path[0] == '/' ? "" : "/home/cklos/";
    uint32_t prefix_length = (uint32_t)strlen(prefix);
    uint32_t path_length = (uint32_t)strlen(path);
    if (prefix_length + path_length + 1 > capacity) return false;
    if (prefix_length) memcpy(output, prefix, prefix_length);
    memcpy(output + prefix_length, path, path_length + 1);
    return true;
}

static uint32_t line_start(const editor_state *editor, uint32_t cursor) {
    if (cursor > editor->size) cursor = editor->size;
    while (cursor && editor->text[cursor - 1] != '\n') cursor--;
    return cursor;
}

static uint32_t next_line(const editor_state *editor, uint32_t start) {
    while (start < editor->size && editor->text[start] != '\n') start++;
    if (start < editor->size) start++;
    return start;
}

static uint32_t previous_line(const editor_state *editor, uint32_t start) {
    if (!start) return 0;
    start--;
    while (start && editor->text[start - 1] != '\n') start--;
    return start;
}

static uint32_t line_end(const editor_state *editor, uint32_t start) {
    while (start < editor->size && editor->text[start] != '\n') start++;
    return start;
}

static uint32_t column_for_offset(const editor_state *editor, uint32_t start,
                                  uint32_t offset) {
    uint32_t column = 0;
    uint32_t end = line_end(editor, start);
    if (offset > end) offset = end;
    while (start < offset) {
        uint32_t consumed;
        utf8_decode((const char *)editor->text + start, &consumed);
        if (consumed > offset - start) break;
        start += consumed;
        column++;
    }
    return column;
}

static uint32_t offset_for_column(const editor_state *editor, uint32_t start,
                                  uint32_t target) {
    uint32_t end = line_end(editor, start);
    uint32_t column = 0;
    while (start < end && column < target) {
        uint32_t consumed;
        utf8_decode((const char *)editor->text + start, &consumed);
        if (start + consumed > end) break;
        start += consumed;
        column++;
    }
    return start;
}

static uint32_t next_offset(const editor_state *editor, uint32_t offset) {
    if (offset >= editor->size) return editor->size;
    uint32_t consumed;
    utf8_decode((const char *)editor->text + offset, &consumed);
    if (offset + consumed > editor->size) consumed = 1;
    return offset + consumed;
}

static uint32_t previous_offset(const editor_state *editor, uint32_t offset) {
    return utf8_previous((const char *)editor->text, offset);
}

static void refresh_position(editor_state *editor) {
    uint32_t start = line_start(editor, editor->cursor);
    editor->current_line = 0;
    for (uint32_t at = 0; at < start;) {
        at = next_line(editor, at);
        editor->current_line++;
    }
    editor->current_column = column_for_offset(editor, start, editor->cursor);
    editor->desired_column = editor->current_column;
}

static bool insert_bytes(editor_state *editor, const uint8_t *bytes, uint32_t size) {
    if (size > EDITOR_MAX_FILE - editor->size) return false;
    memmove(editor->text + editor->cursor + size,
            editor->text + editor->cursor, editor->size - editor->cursor);
    memcpy(editor->text + editor->cursor, bytes, size);
    editor->undo_position = editor->cursor;
    editor->undo_size = size > EDITOR_UNDO_MAX ? EDITOR_UNDO_MAX : size;
    memcpy(editor->undo_data, bytes, editor->undo_size);
    editor->undo_delete = false;
    editor->undo_valid = true;
    editor->cursor += size;
    editor->size += size;
    editor->modified = true;
    refresh_position(editor);
    return true;
}

static bool delete_previous(editor_state *editor) {
    if (!editor->cursor) return false;
    uint32_t start = previous_offset(editor, editor->cursor);
    uint32_t size = editor->cursor - start;
    editor->undo_position = start;
    editor->undo_size = size > EDITOR_UNDO_MAX ? EDITOR_UNDO_MAX : size;
    memcpy(editor->undo_data, editor->text + start, editor->undo_size);
    editor->undo_delete = true;
    editor->undo_valid = true;
    memmove(editor->text + start, editor->text + editor->cursor,
            editor->size - editor->cursor);
    editor->size -= size;
    editor->cursor = start;
    editor->modified = true;
    refresh_position(editor);
    return true;
}

static void undo(editor_state *editor) {
    if (!editor->undo_valid) return;
    if (editor->undo_delete) {
        if (editor->undo_size > EDITOR_MAX_FILE - editor->size) return;
        memmove(editor->text + editor->undo_position + editor->undo_size,
                editor->text + editor->undo_position,
                editor->size - editor->undo_position);
        memcpy(editor->text + editor->undo_position, editor->undo_data,
               editor->undo_size);
        editor->size += editor->undo_size;
        editor->cursor = editor->undo_position + editor->undo_size;
    } else {
        memmove(editor->text + editor->undo_position,
                editor->text + editor->undo_position + editor->undo_size,
                editor->size - editor->undo_position - editor->undo_size);
        editor->size -= editor->undo_size;
        editor->cursor = editor->undo_position;
    }
    editor->undo_valid = false;
    editor->modified = true;
    refresh_position(editor);
}

static bool load_file(editor_state *editor, const char *path, bool create) {
    uint32_t size = 0;
    uint8_t type;
    int status = fs2_stat(path, &size, &type);
    if (status != 0) {
        if (!create) return false;
        size = 0;
    } else if (type != FS2_TYPE_FILE || size > EDITOR_MAX_FILE) {
        return false;
    }
    if (size) {
        uint32_t actual;
        if (fs2_read(path, editor->text, size, &actual) != 0 || actual != size) {
            return false;
        }
        uint32_t write = 0;
        for (uint32_t read = 0; read < size; read++) {
            if (editor->text[read] == '\r' && read + 1 < size
                && editor->text[read + 1] == '\n') {
                continue;
            }
            editor->text[write++] = editor->text[read];
        }
        size = write;
    }
    editor->size = size;
    editor->cursor = 0;
    editor->top_line = 0;
    editor->modified = status != 0;
    editor->undo_valid = false;
    strcpy(editor->path, path);
    refresh_position(editor);
    return true;
}

static void set_status_path(editor_state *editor, const char *prefix,
                            const char *path) {
    uint32_t length = 0;
    while (prefix[length] && length + 1 < sizeof(editor->status)) {
        editor->status[length] = prefix[length];
        length++;
    }
    uint32_t index = 0;
    while (path[index] && length + 1 < sizeof(editor->status)) {
        editor->status[length++] = path[index++];
    }
    editor->status[length] = '\0';
}

static void save_file(editor_state *editor) {
    if (fs2_write(editor->path, editor->text, editor->size, false) == 0) {
        editor->modified = false;
        last_save_valid = true;
        last_saved_size = editor->size;
        strcpy(last_saved_path, editor->path);
        set_status_path(editor, "Saved ", editor->path);
    } else {
        strcpy(editor->status, "Save failed");
    }
}

static void move_vertical(editor_state *editor, int direction) {
    uint32_t start = line_start(editor, editor->cursor);
    uint32_t target = direction < 0 ? previous_line(editor, start)
                                    : next_line(editor, start);
    if (direction < 0 && start == 0) return;
    if (direction > 0 && start == editor->size) return;
    editor->cursor = offset_for_column(editor, target, editor->desired_column);
    uint32_t desired = editor->desired_column;
    refresh_position(editor);
    editor->desired_column = desired;
}

static void draw_editor(editor_state *editor) {
    uint32_t width = gui_width();
    uint32_t height = gui_height();
    uint32_t top = EDITOR_TOP;
    uint32_t rows = (height - top - 64) / 16;
    uint32_t number_width = 72;
    uint32_t text_width = width - number_width - 24;
    gui_fill_rect(0, 0, width, height, 0);
    gui_fill_rect(0, 0, width, 32, 1);
    gui_draw_text(12, 8, "CKI", 15, 0xFF);
    gui_draw_text(52, 8, "CKLOS UTF-8 Editor", 7, 0xFF);
    gui_draw_text(width > 520 ? width - 470 : 210, 8,
                  editor->path, 11, 0xFF);
    gui_fill_rect(width - 42, 4, 30, 24, 12);
    gui_draw_text(width - 31, 8, "X", 0, 0xFF);

    gui_fill_rect(0, 32, width, 36, 8);
    uint32_t button_x = 12;
    const char *labels[5] = {"SAVE", "OPEN", "NEW", "FIND", "UNDO"};
    const uint8_t colors[5] = {10, 11, 14, 1, 9};
    for (uint32_t index = 0; index < 5; index++) {
        gui_fill_rect(button_x, 38, 64, 22, colors[index]);
        gui_draw_text(button_x + 12, 42, labels[index], 0, 0xFF);
        button_x += 74;
    }
    gui_fill_rect(width - 76, 38, 64, 22, 12);
    gui_draw_text(width - 64, 42, "QUIT", 0, 0xFF);
    uint32_t line = 0;
    uint32_t start = 0;
    while (line < editor->top_line && start < editor->size) {
        start = next_line(editor, start);
        line++;
    }
    for (uint32_t visible = 0; visible < rows; visible++) {
        uint32_t y = top + visible * 16;
        char number[16];
        uint_to_string(line + 1, number, 10);
        gui_draw_text(12, y, number, 8, 0);
        if (start >= editor->size && line > editor->current_line) break;
        uint32_t end = line_end(editor, start);
        gui_draw_text_n(number_width, y, (const char *)editor->text + start,
                        end - start, 15, 0);
        if (line == editor->current_line) {
            uint32_t caret = number_width;
            uint32_t at = start;
            while (at < editor->cursor && at < end) {
                uint32_t consumed;
                uint32_t codepoint =
                    utf8_decode((const char *)editor->text + at, &consumed);
                caret += codepoint < 128 ? 8 : 16;
                at += consumed;
            }
            if (caret < number_width + text_width) {
                gui_fill_rect(caret, y, 2, 16, 15);
            }
        }
        line++;
        if (start >= editor->size) break;
        start = end < editor->size ? end + 1 : editor->size;
    }
    gui_fill_rect(0, height - 28, width, 28, 1);
    char position[80];
    char number[16];
    uint_to_string(editor->current_line + 1, number, 10);
    strcpy(position, "Line ");
    strcpy(position + 5, number);
    uint32_t position_length = 5 + (uint32_t)strlen(number);
    strcpy(position + position_length, "  Col ");
    position_length += 6;
    uint_to_string(editor->current_column + 1, number, 10);
    strcpy(position + position_length, number);
    gui_draw_text(10, height - 21, position, 15, 0xFF);
    gui_draw_text(180, height - 21, editor->path, 11, 0xFF);
    gui_draw_text(width - 180, height - 21,
                  editor->modified ? "MODIFIED" : "READY", 12, 0xFF);
    if (editor->status[0]) {
        gui_draw_text(10, height - 44, editor->status, 0, 14);
    }
}

static void ensure_cursor_visible(editor_state *editor) {
    uint32_t rows = (gui_height() - EDITOR_TOP - 64) / 16;
    if (editor->current_line < editor->top_line) {
        editor->top_line = editor->current_line;
    } else if (editor->current_line >= editor->top_line + rows) {
        editor->top_line = editor->current_line - rows + 1;
    }
}

static void prompt_path(editor_state *editor, bool create) {
    char path[EDITOR_PATH_MAX];
    uint32_t length = 0;
    path[0] = '\0';
    while (1) {
        gui_fill_rect(0, gui_height() - 44, gui_width(), 24, 11);
        gui_draw_text(10, gui_height() - 38, create ? "New: " : "Open: ",
                      0, 0xFF);
        gui_draw_text(58, gui_height() - 38, path, 0, 0xFF);
        uint16_t key = input_getchar();
        if (key == '\n') break;
        if (key == 3) return;
        if (key == '\b') {
            length = utf8_previous(path, length);
            path[length] = '\0';
        } else if (key >= 32 && key <= 0xFF && length + 2 < sizeof(path)) {
            path[length++] = (char)key;
            path[length] = '\0';
        }
    }
    if (length) {
        char resolved[EDITOR_PATH_MAX];
        if (resolve_user_path(path, resolved, sizeof(resolved))
            && load_file(editor, resolved, create)) {
            set_status_path(editor, create ? "New " : "Opened ", resolved);
        } else {
            strcpy(editor->status, create ? "New file failed" : "Open failed");
        }
    }
}

static void find_text(editor_state *editor) {
    char query[256];
    uint32_t length = 0;
    query[0] = '\0';
    while (1) {
        gui_fill_rect(0, gui_height() - 44, gui_width(), 24, 11);
        gui_draw_text(10, gui_height() - 38, "Find: ", 0, 0xFF);
        gui_draw_text(58, gui_height() - 38, query, 0, 0xFF);
        uint16_t key = input_getchar();
        if (key == '\n') break;
        if (key == 3) return;
        if (key == '\b') {
            length = utf8_previous(query, length);
            query[length] = '\0';
        } else if (key >= 32 && key <= 0xFF && length + 2 < sizeof(query)) {
            query[length++] = (char)key;
            query[length] = '\0';
        }
    }
    if (!length) return;
    editor->text[editor->size] = '\0';
    char *match = strstr((char *)editor->text + editor->cursor, query);
    if (!match && editor->cursor) match = strstr((char *)editor->text, query);
    if (match) {
        editor->cursor = (uint32_t)((uint8_t *)match - editor->text);
        refresh_position(editor);
        strcpy(editor->status, "Match found");
    } else {
        strcpy(editor->status, "Not found");
    }
}

bool editor_open(const char *path, bool create) {
    last_save_valid = false;
    last_saved_size = 0;
    last_saved_path[0] = '\0';
    editor_state *editor =
        (editor_state *)kmalloc(sizeof(editor_state));
    if (!editor) return false;
    memset(editor, 0, sizeof(*editor));
    editor->text = (uint8_t *)kmalloc(EDITOR_MAX_FILE + 1);
    if (!editor->text) {
        kfree(editor);
        return false;
    }
    if (!load_file(editor, path, create)) {
        strcpy(editor->status, "Cannot open file");
        draw_editor(editor);
        timer_sleep_ms(800);
        kfree(editor->text);
        kfree(editor);
        return false;
    }
    gui_set_editor_mode(true);
    serial_write("\x1B[2J\x1B[HCKI: graphical editor active. "
                 "Use the QEMU window; Ctrl+Q returns to the shell.\n");

    uint8_t encoded[4];
    uint32_t pending_length = 0;
    uint32_t pending_expected = 0;
    uint8_t pending[4];
    while (1) {
        ensure_cursor_visible(editor);
        draw_editor(editor);
        gui_invalidate_cursor();
        bool click = mouse_take_click();
        int32_t click_x = mouse_x();
        int32_t click_y = mouse_y();
        if (click && click_y < 32 && click_x >= (int32_t)gui_width() - 160) {
            break;
        }
        if (click && click_y >= 38 && click_y < 60) {
            if (click_x >= 12 && click_x < 76) {
                save_file(editor);
                continue;
            }
            if (click_x >= 86 && click_x < 150) {
                prompt_path(editor, false);
                continue;
            }
            if (click_x >= 160 && click_x < 224) {
                prompt_path(editor, true);
                continue;
            }
            if (click_x >= 234 && click_x < 298) {
                find_text(editor);
                continue;
            }
            if (click_x >= 308 && click_x < 372) {
                undo(editor);
                continue;
            }
            if (click_x >= (int32_t)gui_width() - 76
                && click_x < (int32_t)gui_width() - 12) {
                break;
            }
        }
        if (click && click_y >= (int32_t)EDITOR_TOP
            && click_y < (int32_t)gui_height() - 64) {
            uint32_t wanted_line = editor->top_line
                                 + (uint32_t)(click_y - EDITOR_TOP) / 16;
            uint32_t start = 0;
            for (uint32_t line = 0; line < wanted_line && start < editor->size;
                 line++) {
                start = next_line(editor, start);
            }
            uint32_t column = click_x > 72
                            ? (uint32_t)(click_x - 72) / 8 : 0;
            editor->cursor = offset_for_column(editor, start, column);
            refresh_position(editor);
        }
        uint16_t key = input_getchar();
        editor->status[0] = '\0';
        if (key == 17 || key == KEY_ESCAPE) break;
        if (key == 19) {
            save_file(editor);
        } else if (key == 15) {
            prompt_path(editor, false);
        } else if (key == 14) {
            prompt_path(editor, true);
        } else if (key == 6) {
            find_text(editor);
        } else if (key == 26) {
            undo(editor);
        } else if (key == KEY_LEFT) {
            editor->cursor = previous_offset(editor, editor->cursor);
            refresh_position(editor);
        } else if (key == KEY_RIGHT) {
            editor->cursor = next_offset(editor, editor->cursor);
            refresh_position(editor);
        } else if (key == KEY_UP) {
            move_vertical(editor, -1);
        } else if (key == KEY_DOWN) {
            move_vertical(editor, 1);
        } else if (key == KEY_PAGE_UP) {
            for (uint32_t count = 0; count < 20; count++) move_vertical(editor, -1);
        } else if (key == KEY_PAGE_DOWN) {
            for (uint32_t count = 0; count < 20; count++) move_vertical(editor, 1);
        } else if (key == KEY_HOME) {
            editor->cursor = line_start(editor, editor->cursor);
            refresh_position(editor);
        } else if (key == KEY_END) {
            editor->cursor = line_end(editor, line_start(editor, editor->cursor));
            refresh_position(editor);
        } else if (key == '\b') {
            delete_previous(editor);
        } else if (key == '\n') {
            const uint8_t newline = '\n';
            insert_bytes(editor, &newline, 1);
        } else if (key >= 32 && key <= 0xFF) {
            if ((uint8_t)key >= 0xC0) {
                pending[0] = (uint8_t)key;
                pending_length = 1;
                pending_expected = (uint8_t)key >= 0xF0 ? 4
                                  : (uint8_t)key >= 0xE0 ? 3 : 2;
            } else if (pending_length) {
                pending[pending_length++] = (uint8_t)key;
                if (pending_length == pending_expected) {
                    insert_bytes(editor, pending, pending_length);
                    pending_length = 0;
                }
            } else {
                encoded[0] = (uint8_t)key;
                insert_bytes(editor, encoded, 1);
            }
        }
    }
    kfree(editor->text);
    kfree(editor);
    gui_set_editor_mode(false);
    gui_init();
    return true;
}

bool editor_last_saved(char *path, uint32_t capacity, uint32_t *size) {
    if (!last_save_valid || !path || !capacity) return false;
    uint32_t length = (uint32_t)strlen(last_saved_path);
    if (length + 1 > capacity) return false;
    memcpy(path, last_saved_path, length + 1);
    if (size) *size = last_saved_size;
    return true;
}
