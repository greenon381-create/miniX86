#include "io.h"
#include "string.h"

#define VIDEO_ADDRESS 0xB8000
#define SCREEN_WIDTH 80
#define SCREEN_HEIGHT 25
#define BUFFER_SIZE 512

#define COLOR_PROMPT 0x0B
#define COLOR_TEXT   0x0F
#define COLOR_SYSTEM 0x0A
#define COLOR_ERROR  0x0C
#define COLOR_DEFAULT 0x07

#define MAX_FILES 64
#define FILE_NAME_SIZE 32
#define FILE_CONTENT_SIZE 1024

volatile char *video_memory = (volatile char*)VIDEO_ADDRESS;

int cursor_x = 0;
int cursor_y = 0;
int extended_key = 0;
char input_buffer[BUFFER_SIZE];
int buffer_idx = 0;

/* ---------------- FILE SYSTEM ---------------- */

typedef struct {
    char name[FILE_NAME_SIZE];
    char content[FILE_CONTENT_SIZE];
    int used;
} File;

File files[MAX_FILES];

int editor_mode = 0;
int current_file = -1;

char editor_buffer[FILE_CONTENT_SIZE];
int editor_index = 0;

/* ---------------- SAFE CLEAR ---------------- */

void clear_screen() {
    for (int i = 0; i < SCREEN_WIDTH * SCREEN_HEIGHT * 2; i += 2) {
        video_memory[i] = ' ';
        video_memory[i + 1] = COLOR_DEFAULT;
    }

    cursor_x = 0;
    cursor_y = 0;
}

void clear() {
    clear_screen();
}

/* ---------------- VGA ---------------- */

void scroll() {

    for (int y = 1; y < SCREEN_HEIGHT; y++) {

        for (int x = 0; x < SCREEN_WIDTH; x++) {

            int dst =
                ((y - 1) * SCREEN_WIDTH + x) * 2;

            int src =
                (y * SCREEN_WIDTH + x) * 2;

            video_memory[dst] = video_memory[src];
            video_memory[dst + 1] =
                video_memory[src + 1];
        }
    }

    /* clear last line */

    for (int x = 0; x < SCREEN_WIDTH; x++) {

        int offset =
            ((SCREEN_HEIGHT - 1) * SCREEN_WIDTH + x) * 2;

        video_memory[offset] = ' ';
        video_memory[offset + 1] = COLOR_DEFAULT;
    }

    cursor_y = SCREEN_HEIGHT - 1;
}

void update_cursor() {

    unsigned short pos =
        cursor_y * SCREEN_WIDTH + cursor_x;

    outb(0x3D4, 0x0F);
    outb(0x3D5, (unsigned char)(pos & 0xFF));

    outb(0x3D4, 0x0E);
    outb(0x3D5, (unsigned char)((pos >> 8) & 0xFF));
}

void put_char(char c, unsigned char color) {

    if (c == '\n') {

        cursor_x = 0;
        cursor_y++;
    }

    else if (c == '\b') {

        if (cursor_x > 0) {

            cursor_x--;

            int offset =
                (cursor_y * SCREEN_WIDTH + cursor_x) * 2;

            video_memory[offset] = ' ';
            video_memory[offset + 1] = color;
        }
    }

    else {

        int offset =
            (cursor_y * SCREEN_WIDTH + cursor_x) * 2;

        video_memory[offset] = c;
        video_memory[offset + 1] = color;

        cursor_x++;
    }

    if (cursor_x >= SCREEN_WIDTH) {

        cursor_x = 0;
        cursor_y++;
    }

    if (cursor_y >= SCREEN_HEIGHT) {

        scroll();
    }

    update_cursor();
}

void print(const char *str, unsigned char color) {

    while (*str) {

        put_char(*str++, color);
    }
}

/* ---------------- CURSOR CONTROL ---------------- */

void enable_cursor(unsigned char start,
                   unsigned char end) {

    outb(0x3D4, 0x0A);
    outb(0x3D5,
         (inb(0x3D5) & 0xC0) | start);

    outb(0x3D4, 0x0B);
    outb(0x3D5,
         (inb(0x3D5) & 0xE0) | end);
}

void disable_cursor() {

    outb(0x3D4, 0x0A);

    outb(0x3D5, 0x20);
}

/* ---------------- STRING HELPERS ---------------- */

int strlength(const char* str) {

    int len = 0;

    while (str[len]) {
        len++;
    }

    return len;
}

void copy_string(char* dest, const char* src) {

    while (*src) {

        *dest++ = *src++;
    }

    *dest = '\0';
}

int starts_with(const char* str, const char* prefix) {

    while (*prefix) {

        if (*str != *prefix)
            return 0;

        str++;
        prefix++;
    }

    return 1;
}

int shift_pressed = 0;
int shell_cursor_pos = 0;
int editor_cursor_pos = 0;

/* ---------------- KEYBOARD ---------------- */

static char keymap[128] = {
    0, 27,
    '1','2','3','4','5','6','7','8','9','0','-','=',
    '\b','\t',
    'q','w','e','r','t','y','u','i','o','p','[',']',
    '\n',
    0,
    'a','s','d','f','g','h','j','k','l',';','\'','`',
    0,'\\',
    'z','x','c','v','b','n','m',',','.','/',
    0,'*',0,' '
};

static char shift_keymap[128] = {
    0, 27,
    '!','@','#','$','%','^','&','*','(',')','_','+',
    '\b','\t',
    'Q','W','E','R','T','Y','U','I','O','P','{','}',
    '\n',
    0,
    'A','S','D','F','G','H','J','K','L',':','"','~',
    0,'|',
    'Z','X','C','V','B','N','M','<','>','?',
    0,'*',0,' '
};

unsigned char get_key() {

    if ((inb(0x64) & 1) == 0)
        return 0;

    unsigned char sc = inb(0x60);

    return sc;
}

/* ---------------- FILE HELPERS ---------------- */

int find_file(const char* name) {

    for (int i = 0; i < MAX_FILES; i++) {

        if (files[i].used &&
            strcmp(files[i].name, name) == 0) {

            return i;
        }
    }

    return -1;
}

/* ---------------- FILE COMMANDS ---------------- */

void create_file(const char* name) {

    if (find_file(name) != -1) {
        print("file already exists\n", COLOR_ERROR);
        return;
    }

    for (int i = 0; i < MAX_FILES; i++) {

        if (!files[i].used) {

            files[i].used = 1;

            copy_string(files[i].name, name);

            files[i].content[0] = '\0';

            print("file created\n", COLOR_SYSTEM);

            return;
        }
    }

    print("storage full\n", COLOR_ERROR);
}

void show_file(const char* name) {

    int idx = find_file(name);

    if (idx == -1) {
        print("file not found\n", COLOR_ERROR);
        return;
    }

    print(files[idx].content, COLOR_TEXT);
    print("\n", COLOR_TEXT);
}

void delete_file(const char* name) {

    int idx = find_file(name);

    if (idx == -1) {
        print("file not found\n", COLOR_ERROR);
        return;
    }

    files[idx].used = 0;

    print("file deleted\n", COLOR_SYSTEM);
}

void rename_file(const char* oldname, const char* newname) {

    int idx = find_file(oldname);

    if (idx == -1) {
        print("file not found\n", COLOR_ERROR);
        return;
    }

    copy_string(files[idx].name, newname);

    print("file renamed\n", COLOR_SYSTEM);
}

void list_files() {

    int found = 0;

    for (int i = 0; i < MAX_FILES; i++) {

        if (files[i].used) {

            print(files[i].name, COLOR_TEXT);
            print("\n", COLOR_TEXT);

            found = 1;
        }
    }

    if (!found) {
        print("no files\n", COLOR_ERROR);
    }
}

/* ---------------- EDITOR ---------------- */

void open_editor(const char* name) {

    int idx = find_file(name);

    if (idx == -1) {
        print("file not found\n", COLOR_ERROR);
        return;
    }

    current_file = idx;
    editor_mode = 1;

    editor_index = 0;

    clear_screen();

    int i = 0;

    while (files[idx].content[i]) {

        editor_buffer[i] = files[idx].content[i];

        put_char(editor_buffer[i], COLOR_TEXT);

        i++;
    }

    editor_index = i;

    editor_buffer[editor_index] = '\0';

    print("\n\nF1 save\n", COLOR_SYSTEM);
    print("F2 discard\n", COLOR_SYSTEM);
}

void save_editor() {

    if (current_file == -1)
        return;

    copy_string(files[current_file].content, editor_buffer);

    editor_mode = 0;
    current_file = -1;

    print("\nfile saved\n", COLOR_SYSTEM);
    print(">> ", COLOR_PROMPT);
}

void discard_editor() {

    editor_mode = 0;
    current_file = -1;

    print("\nchanges discarded\n", COLOR_ERROR);
    print(">> ", COLOR_PROMPT);
}

/* ---------------- COMMANDS ---------------- */

void process_command() {

    input_buffer[buffer_idx] = '\0';

    /* empty command */
    if (buffer_idx == 0) {
        return;
    }

    if (strcmp(input_buffer, "help") == 0) {

        print("nfile ", COLOR_SYSTEM);
        print("<name>", COLOR_DEFAULT);
        print("     :    Makes new files\n", COLOR_PROMPT);

        print("ofile ", COLOR_SYSTEM);
        print("<name>", COLOR_DEFAULT);
        print("     :    Opens the file to the screen\n", COLOR_PROMPT);

        print("efile ", COLOR_SYSTEM);
        print("<name>", COLOR_DEFAULT);
        print("     :    Gets you the the file editor\n", COLOR_PROMPT);

        print("dfile ", COLOR_SYSTEM);
        print("<name>", COLOR_DEFAULT);
        print("     :    To delete a file\n", COLOR_PROMPT);

        print("rfile ", COLOR_SYSTEM);
        print("old ? new", COLOR_DEFAULT);
        print("  :    Renames a file\n", COLOR_PROMPT);

        print("afile", COLOR_SYSTEM);
        print("            :    Shows all saved files\n", COLOR_PROMPT);

        print("enbcursor", COLOR_SYSTEM);
        print("        : To turn on the Cursor\n",COLOR_PROMPT);

        print("discursor", COLOR_SYSTEM);
        print("        : To turn off the Cursor\n",COLOR_PROMPT);

        print("clr", COLOR_SYSTEM);
        print("              :    Clears the screen\n", COLOR_PROMPT);

        print("exit/kill", COLOR_ERROR);
        print("        :    Shutdown computer\n", COLOR_PROMPT);
    }

    else if (strcmp(input_buffer, "clr") == 0) {

        clear_screen();
    }

    else if (strcmp(input_buffer, "afile") == 0) {

        list_files();
    }

    else if (strcmp(input_buffer, "discursor") == 0) {
        disable_cursor();
        print("Cursor disabled\n", COLOR_ERROR);
    }

    else if (strcmp(input_buffer, "enbcursor") == 0) {
        enable_cursor(14,15);
        print("Cursor enabled\n", COLOR_SYSTEM);
    }
    

    else if (strcmp(input_buffer, "exit") == 0 ||
             strcmp(input_buffer, "kill") == 0) {

        clear_screen();

        print("shutting down...\n", COLOR_ERROR);

        /* QEMU */
        outw(0x604, 0x2000);

        /* Bochs */
        outw(0xB004, 0x2000);

        /* VirtualBox */
        outw(0x4004, 0x3400);

        /* fallback */
        while (1) {
            __asm__ volatile ("hlt");
        }
    }

    else if (starts_with(input_buffer, "nfile ")) {

        create_file(input_buffer + 6);
    }

    else if (starts_with(input_buffer, "ofile ")) {

        show_file(input_buffer + 6);
    }

    else if (starts_with(input_buffer, "dfile ")) {

        delete_file(input_buffer + 6);
    }

    else if (starts_with(input_buffer, "efile ")) {

        open_editor(input_buffer + 6);

        buffer_idx = 0;

        return;
    }

    else if (starts_with(input_buffer, "rfile ")) {

        char oldname[32];
        char newname[32];

        int i = 6;
        int j = 0;

        while (input_buffer[i] != '?' &&
               input_buffer[i] != '\0') {

            oldname[j++] = input_buffer[i++];
        }

        oldname[j] = '\0';

        if (input_buffer[i] == '?')
            i++;

        while (input_buffer[i] == ' ')
            i++;

        j = 0;

        while (input_buffer[i]) {

            newname[j++] = input_buffer[i++];
        }

        newname[j] = '\0';

        rename_file(oldname, newname);
    }

    else {

        print(input_buffer, COLOR_ERROR);
        print(" is an unknown command, try 'help'\n", COLOR_ERROR);
    }

    buffer_idx = 0;
}
/* ---------------- INPUT ---------------- */

void redraw_editor() {

    clear_screen();

    int x = 0;
    int y = 3;

    /* DRAW EDITOR BUFFER */

    for (int i = 0; i < editor_index; i++) {

        char c = editor_buffer[i];

        if (c == '\n') {

            x = 0;
            y++;
        }

        else {

            int offset =
                (y * SCREEN_WIDTH + x) * 2;

            video_memory[offset] = c;

            video_memory[offset + 1] =
                COLOR_TEXT;

            x++;

            if (x >= SCREEN_WIDTH) {

                x = 0;
                y++;
            }
        }
    }

    /* REBUILD CURSOR POSITION */

    x = 0;
    y = 3;

    for (int i = 0;
         i < editor_cursor_pos;
         i++) {

        if (editor_buffer[i] == '\n') {

            x = 0;
            y++;
        }

        else {

            x++;

            if (x >= SCREEN_WIDTH) {

                x = 0;
                y++;
            }
        }
    }

    cursor_x = x;
    cursor_y = y;

    /* DRAW HELP TEXT */

    int help_y = 0;
    y = 2;

    char *help1 = "F1 save";
    char *help2 = "F2 discard";

    for (int i = 0; help1[i]; i++) {

        int offset =
            (help_y * SCREEN_WIDTH + i) * 2;

        video_memory[offset] = help1[i];
        video_memory[offset + 1] =
            COLOR_SYSTEM;
    }

    for (int i = 0; help2[i]; i++) {

        int offset =
            ((help_y + 1) * SCREEN_WIDTH + i) * 2;

        video_memory[offset] = help2[i];
        video_memory[offset + 1] =
            COLOR_SYSTEM;
    }

    update_cursor();
}

void handle_input(char c) {

    /* ---------------- EDITOR MODE ---------------- */

    if (editor_mode) {

        /* CTRL+6 SAVE */
        if (c == 30) {

            save_editor();
            return;
        }

        /* CTRL+- DISCARD */
        if (c == 31) {

            discard_editor();
            return;
        }

        /* BACKSPACE */
        if (c == '\b') {

            if (editor_cursor_pos > 0) {

                int delete_pos =
                    editor_cursor_pos - 1;

                /* SHIFT LEFT */
                for (int i = delete_pos;
                     i < editor_index - 1;
                     i++) {

                    editor_buffer[i] =
                        editor_buffer[i + 1];
                }

                editor_index--;
                editor_cursor_pos--;

                editor_buffer[editor_index] = '\0';

                redraw_editor();
            }

            return;
        }

        /* ENTER */
        if (c == '\n') {

            if (editor_index <
                FILE_CONTENT_SIZE - 1) {

                /* SHIFT RIGHT */
                for (int i = editor_index;
                     i > editor_cursor_pos;
                     i--) {

                    editor_buffer[i] =
                        editor_buffer[i - 1];
                }

                editor_buffer[editor_cursor_pos] =
                    '\n';

                editor_index++;
                editor_cursor_pos++;

                editor_buffer[editor_index] = '\0';

                redraw_editor();
            }

            return;
        }

        /* NORMAL INPUT */
        if (editor_index <
            FILE_CONTENT_SIZE - 1) {

            /* SHIFT RIGHT */
            for (int i = editor_index;
                 i > editor_cursor_pos;
                 i--) {

                editor_buffer[i] =
                    editor_buffer[i - 1];
            }

            /* INSERT */
            editor_buffer[editor_cursor_pos] = c;

            editor_index++;
            editor_cursor_pos++;

            editor_buffer[editor_index] = '\0';

            redraw_editor();
        }

        return;
    }

    /* ---------------- SHELL MODE ---------------- */

    /* ENTER */
    if (c == '\n') {

        put_char('\n', COLOR_TEXT);

        process_command();

        buffer_idx = 0;
        shell_cursor_pos = 0;

        print(">> ", COLOR_PROMPT);

        return;
    }

    /* BACKSPACE */
    if (c == '\b') {

        if (shell_cursor_pos > 0) {

            int delete_pos =
                shell_cursor_pos - 1;

            /* SHIFT LEFT */
            for (int i = delete_pos;
                 i < buffer_idx - 1;
                 i++) {

                input_buffer[i] =
                    input_buffer[i + 1];
            }

            buffer_idx--;
            shell_cursor_pos--;

            input_buffer[buffer_idx] = '\0';

            int start_x = 3;

            /* CLEAR LINE */
            for (int i = 0;
                 i < SCREEN_WIDTH - start_x;
                 i++) {

                int offset =
                    (cursor_y * SCREEN_WIDTH +
                     start_x + i) * 2;

                video_memory[offset] = ' ';
                video_memory[offset + 1] =
                    COLOR_TEXT;
            }

            /* REDRAW BUFFER */
            for (int i = 0;
                 i < buffer_idx;
                 i++) {

                int offset =
                    (cursor_y * SCREEN_WIDTH +
                     start_x + i) * 2;

                video_memory[offset] =
                    input_buffer[i];

                video_memory[offset + 1] =
                    COLOR_TEXT;
            }

            cursor_x =
                start_x + shell_cursor_pos;

            update_cursor();
        }

        return;
    }

    /* NORMAL INPUT */
    if (buffer_idx < BUFFER_SIZE - 1) {

        /* SHIFT RIGHT */
        for (int i = buffer_idx;
             i > shell_cursor_pos;
             i--) {

            input_buffer[i] =
                input_buffer[i - 1];
        }

        /* INSERT */
        input_buffer[shell_cursor_pos] = c;

        buffer_idx++;
        shell_cursor_pos++;

        input_buffer[buffer_idx] = '\0';

        int start_x = 3;

        /* CLEAR LINE */
        for (int i = 0;
             i < SCREEN_WIDTH - start_x;
             i++) {

            int offset =
                (cursor_y * SCREEN_WIDTH +
                 start_x + i) * 2;

            video_memory[offset] = ' ';
            video_memory[offset + 1] =
                COLOR_TEXT;
        }

        /* DRAW BUFFER */
        for (int i = 0;
             i < buffer_idx;
             i++) {

            int offset =
                (cursor_y * SCREEN_WIDTH +
                 start_x + i) * 2;

            video_memory[offset] =
                input_buffer[i];

            video_memory[offset + 1] =
                COLOR_TEXT;
        }

        cursor_x =
            start_x + shell_cursor_pos;

        update_cursor();
    }
}

/*--------------BOOT SCREEN-------------*/

void boot_screen() {

    clear_screen();

    print("\n\n\n\n\n\n\n\n\n\n\n", COLOR_DEFAULT);

    print("                                  miniX86\n", COLOR_SYSTEM);
    print("                            experimental kernel\n", COLOR_PROMPT);

    /* simple delay */
    for (volatile int i = 0;
         i < 700000000;
         i++) {
    }

    clear_screen();

    print("loading memory manager...\n", COLOR_SYSTEM);

    for (volatile int i = 0;
         i < 200000000;
         i++) {
    }

    print("initializing keyboard...\n", COLOR_SYSTEM);

    for (volatile int i = 0;
         i < 300000000;
         i++) {
    }

    print("mounting filesystem...\n", COLOR_SYSTEM);
    
    for (volatile int i = 0;
         i < 300000000;
         i++) {
    }

    print("starting shell...\n\n", COLOR_SYSTEM);

    for (volatile int i = 0;
         i < 300000000;
         i++) {
    }

    clear_screen();

    for (volatile int i = 0;
         i < 100000000;
         i++) {
    }

}

/* ---------------- KERNEL ---------------- */
void kernel_main() {

    clear_screen();

    enable_cursor(14, 15);

    boot_screen();

    print("                          ----------miniX86----------\n", COLOR_SYSTEM);
    print("                                  version-1.0\n", COLOR_SYSTEM);
    print("                               miniX86 copyright\n\n", COLOR_SYSTEM);

    print(">> ", COLOR_PROMPT);

    while (1) {

        unsigned char sc = get_key();

        if (sc) {

            /* EXTENDED KEY PREFIX */
            if (sc == 0xE0) {

                extended_key = 1;
            }

            /* LEFT SHIFT / RIGHT SHIFT PRESS */
            else if (sc == 0x2A || sc == 0x36) {

                shift_pressed = 1;
            }

            /* LEFT SHIFT / RIGHT SHIFT RELEASE */
            else if (sc == 0xAA || sc == 0xB6) {

                shift_pressed = 0;
            }

            else {

                /* ---------------- LEFT ARROW ---------------- */

                if (extended_key && sc == 0x4B) {

                    if (editor_mode) {

                        if (editor_cursor_pos > 0) {

                            editor_cursor_pos--;

                            cursor_x--;

                            update_cursor();
                        }
                    }

                    else {

                        if (shell_cursor_pos > 0) {

                            shell_cursor_pos--;

                            cursor_x--;

                            update_cursor();
                        }
                    }
                }

                /* ---------------- RIGHT ARROW ---------------- */

                else if (extended_key && sc == 0x4D) {

                    if (editor_mode) {

                        if (editor_cursor_pos < editor_index) {

                            editor_cursor_pos++;

                            cursor_x++;

                            update_cursor();
                        }
                    }

                    else {

                        if (shell_cursor_pos < buffer_idx) {

                            shell_cursor_pos++;

                            cursor_x++;

                            update_cursor();
                        }
                    }
                }

                /* ---------------- NORMAL KEYS ---------------- */

                else if (!(sc & 0x80)) {

                    /* F1 = SAVE */
                    if (editor_mode && sc == 0x3B) {

                        save_editor();
                    }

                    /* F2 = DISCARD */
                    else if (editor_mode && sc == 0x3C) {

                        discard_editor();
                    }

                    else {

                        char c = 0;

                        if (sc < 128) {

                            if (shift_pressed)
                                c = shift_keymap[sc];
                            else
                                c = keymap[sc];
                        }

                        if (c)
                            handle_input(c);
                    }
                }

                extended_key = 0;
            }
        }
    }
}