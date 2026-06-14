#ifndef SCANCODE_TABLES_H
#define SCANCODE_TABLES_H

#define SCANCODE_A       0x1E
#define SCANCODE_B       0x30
#define SCANCODE_C       0x2E
#define SCANCODE_D       0x20
#define SCANCODE_E       0x12
#define SCANCODE_F       0x21
#define SCANCODE_G       0x22
#define SCANCODE_H       0x23
#define SCANCODE_I       0x17
#define SCANCODE_J       0x24
#define SCANCODE_K       0x25
#define SCANCODE_L       0x26
#define SCANCODE_M       0x32
#define SCANCODE_N       0x31
#define SCANCODE_O       0x18
#define SCANCODE_P       0x19
#define SCANCODE_Q       0x10
#define SCANCODE_R       0x13
#define SCANCODE_S       0x1F
#define SCANCODE_T       0x14
#define SCANCODE_U       0x16
#define SCANCODE_V       0x2F
#define SCANCODE_W       0x11
#define SCANCODE_X       0x2D
#define SCANCODE_Y       0x15
#define SCANCODE_Z       0x2C

#define SCANCODE_1       0x02
#define SCANCODE_2       0x03
#define SCANCODE_3       0x04
#define SCANCODE_4       0x05
#define SCANCODE_5       0x06
#define SCANCODE_6       0x07
#define SCANCODE_7       0x08
#define SCANCODE_8       0x09
#define SCANCODE_9       0x0A
#define SCANCODE_0       0x0B
#define SCANCODE_MINUS   0x0C
#define SCANCODE_EQUAL   0x0D

#define SCANCODE_LEFT_SHIFT  0x2A
#define SCANCODE_RIGHT_SHIFT 0x36
#define SCANCODE_CTRL        0x1D
#define SCANCODE_ALT         0x38

#define SCANCODE_ENTER       0x1C
#define SCANCODE_ESC         0x01
#define SCANCODE_BACKSPACE   0x0E
#define SCANCODE_TAB         0x0F
#define SCANCODE_SPACE       0x39
#define SCANCODE_CAPSLOCK    0x3A

#define SCANCODE_NUMPAD_0    0x52
#define SCANCODE_NUMPAD_1    0x4F
#define SCANCODE_NUMPAD_2    0x50
#define SCANCODE_NUMPAD_3    0x51
#define SCANCODE_NUMPAD_4    0x4B
#define SCANCODE_NUMPAD_5    0x4C
#define SCANCODE_NUMPAD_6    0x4D
#define SCANCODE_NUMPAD_7    0x47
#define SCANCODE_NUMPAD_8    0x48
#define SCANCODE_NUMPAD_9    0x49

/* --- US (QWERTY) --- */
static const char scancode_ascii_us[128] = {
    0, 27, '1','2','3','4','5','6','7','8','9','0','-','=','\b','\t',
    'q','w','e','r','t','y','u','i','o','p','[',']','\n',0,'a','s',
    'd','f','g','h','j','k','l',';','\'','`',0,'\\','z','x','c','v',
    'b','n','m',',','.','/',0,'*',0,' ', 0,0,0,0,0,0
};
static const char scancode_ascii_shift_us[128] = {
    0, 27, '!','@','#','$','%','^','&','*','(',')','_','+','\b','\t',
    'Q','W','E','R','T','Y','U','I','O','P','{','}','\n',0,'A','S',
    'D','F','G','H','J','K','L',':','"','~',0,'|','Z','X','C','V',
    'B','N','M','<','>','?',0,'*',0,' ', 0,0,0,0,0,0
};

/* --- DE (QWERTZ) --- */
static const char scancode_ascii_de[128] = {
    0, 27, '1','2','3','4','5','6','7','8','9','0','\xDF','\xB4','\b','\t',
    'q','w','e','r','t','z','u','i','o','p','\xFC','+','\n',0,'a','s',
    'd','f','g','h','j','k','l','\xF6','\xE4','^',0,'\\','y','x','c','v',
    'b','n','m',',','.','-',0,'*',0,' ', 0,0,0,0,0,0
};
static const char scancode_ascii_shift_de[128] = {
    0, 27, '!','"','\xA7','$','%','&','/','(',')','=','?','`','\b','\t',
    'Q','W','E','R','T','Z','U','I','O','P','\xDC','*','\n',0,'A','S',
    'D','F','G','H','J','K','L','\xD6','\xC4','\xB0',0,'|','Y','X','C','V',
    'B','N','M',';',':','_',0,'*',0,' ', 0,0,0,0,0,0
};

/* --- FR (AZERTY) --- */
static const char scancode_ascii_fr[128] = {
    0, 27, '&','\xE9','"','\'','(','-','\xE8','_','\xE7','\xE0',')','=','\b','\t',
    'a','z','e','r','t','y','u','i','o','p','^','$','\n',0,'q','s',
    'd','f','g','h','j','k','l','m','\xF9','*',0,'\xB5','w','x','c','v',
    'b','n',',',';',':','!',0,'*',0,' ', 0,0,0,0,0,0
};
static const char scancode_ascii_shift_fr[128] = {
    0, 27, '1','2','3','4','5','6','7','8','9','0','\xB0','+','\b','\t',
    'A','Z','E','R','T','Y','U','I','O','P','\xA8','\xA3','\n',0,'Q','S',
    'D','F','G','H','J','K','L','M','\xD9','\xB5',0,'\xB5','W','X','C','V',
    'B','N','?','.','/','\xA7',0,'*',0,' ', 0,0,0,0,0,0
};

/* --- ES --- */
static const char scancode_ascii_es[128] = {
    0, 27, '1','2','3','4','5','6','7','8','9','0','\'','\xBF','\b','\t',
    'q','w','e','r','t','y','u','i','o','p','`','+','\n',0,'a','s',
    'd','f','g','h','j','k','l','\xF1','\xE7','\xBA',0,'\xE7','z','x','c','v',
    'b','n','m',',','.','-',0,'*',0,' ', 0,0,0,0,0,0
};
static const char scancode_ascii_shift_es[128] = {
    0, 27, '!','"','\xB7','$','%','&','/','(',')','=','?','\xBF','\b','\t',
    'Q','W','E','R','T','Y','U','I','O','P','^','*','\n',0,'A','S',
    'D','F','G','H','J','K','L','\xD1','\xC7','\xAA',0,'\xC7','Z','X','C','V',
    'B','N','M',';',':','_',0,'*',0,' ', 0,0,0,0,0,0
};

/* --- IT --- */
static const char scancode_ascii_it[128] = {
    0, 27, '1','2','3','4','5','6','7','8','9','0','\'','\xEC','\b','\t',
    'q','w','e','r','t','y','u','i','o','p','\xE8','+','\n',0,'a','s',
    'd','f','g','h','j','k','l','\xF2','\xE0','\xFA',0,'\\','z','x','c','v',
    'b','n','m',',','.','-',0,'*',0,' ', 0,0,0,0,0,0
};
static const char scancode_ascii_shift_it[128] = {
    0, 27, '!','"','\xA3','$','%','&','/','(',')','=','?','^','\b','\t',
    'Q','W','E','R','T','Y','U','I','O','P','\xE9','*','\n',0,'A','S',
    'D','F','G','H','J','K','L','\xC7','\xC9','\xE7',0,'|','Z','X','C','V',
    'B','N','M',';',':','_',0,'*',0,' ', 0,0,0,0,0,0
};

/* --- PL (Programmer) --- */
static const char scancode_ascii_pl[128] = {
    0, 27, '1','2','3','4','5','6','7','8','9','0','-','=','\b','\t',
    'q','w','e','r','t','z','u','i','o','p','[',']','\n',0,'a','s',
    'd','f','g','h','j','k','l',';','\'','`',0,'\\','y','x','c','v',
    'b','n','m',',','.','/',0,'*',0,' ', 0,0,0,0,0,0
};
static const char scancode_ascii_shift_pl[128] = {
    0, 27, '!','@','#','$','%','^','&','*','(',')','_','+','\b','\t',
    'Q','W','E','R','T','Z','U','I','O','P','{','}','\n',0,'A','S',
    'D','F','G','H','J','K','L',':','"','~',0,'|','Y','X','C','V',
    'B','N','M','<','>','?',0,'*',0,' ', 0,0,0,0,0,0
};

/* --- RU (JCUKEN) --- */
static const char scancode_ascii_ru[128] = {
    0, 27, '1','2','3','4','5','6','7','8','9','0','-','=','\b','\t',
    '\xE9','\xE6','\xE5','\xF0','\xF2','\xFB','\xF3','\xE8','\xF8','\xF9','\xF7','\xFA','\n',0,'\xF4','\xFB',
    '\xE2','\xE0','\xEF','\xF0','\xEE','\xEB','\xE4','\xE6','\xFD','\xE7','\xF5','\xEA','\xFE','\xF1','\xE3','\xF5',
    '\xF6','\xF2','\xFC','\xE1','\xE2','\xEF',0,'*',0,' ', 0,0,0,0,0,0
};
static const char scancode_ascii_shift_ru[128] = {
    0, 27, '!','"','\x2116',';','%',':','?','*','(',')','_','+','\b','\t',
    '\xDD','\xD6','\xD5','\xD0','\xD2','\xDB','\xD3','\xD8','\xD8','\xD9','\xD7','\xDA','\n',0,'\xD4','\xDB',
    '\xD2','\xD0','\xCF','\xD0','\xCE','\xCB','\xC4','\xD6','\xDD','\xD7','\xD5','\xCA','\xDE','\xD1','\xD3','\xD5',
    '\xD6','\xD2','\xDC','\xC1','\xC2','\xCF',0,'*',0,' ', 0,0,0,0,0,0
};

/* --- TR (Turkish Q) --- */
static const char scancode_ascii_tr[128] = {
    0, 27, '1','2','3','4','5','6','7','8','9','0','*','-','\b','\t',
    'q','w','e','r','t','y','u','i','o','p','\xFC','\xE7','\n',0,'a','s',
    'd','f','g','h','j','k','l','\xF6','\xE9','\xED',0,'\\','z','x','c','v',
    'b','n','m','\xF6','\xE7','.',0,'*',0,' ', 0,0,0,0,0,0
};
static const char scancode_ascii_shift_tr[128] = {
    0, 27, '!','\'','^','+','%','&','/','(',')','=','?','_','\b','\t',
    'Q','W','E','R','T','Y','U','I','O','P','\xDC','\xC7','\n',0,'A','S',
    'D','F','G','H','J','K','L','\xD6','\xC9','\xCD',0,'\\','Z','X','C','V',
    'B','N','M','\xD6','\xC7',':',0,'*',0,' ', 0,0,0,0,0,0
};

typedef struct {
    const char* name;
    const char* table;
    const char* table_shift;
} scancode_layout_t;

#define LAYOUT(n) { #n, scancode_ascii_##n, scancode_ascii_shift_##n }

static const scancode_layout_t g_scancode_layouts[] = {
    LAYOUT(us),
    LAYOUT(de),
    LAYOUT(fr),
    LAYOUT(es),
    LAYOUT(it),
    LAYOUT(pl),
    LAYOUT(ru),
    LAYOUT(tr),
};

static const int g_scancode_layout_count = sizeof(g_scancode_layouts) / sizeof(g_scancode_layouts[0]);

static int scancode_layout_index(const char* name) {
    if (!name) return -1;
    for (int i = 0; i < g_scancode_layout_count; ++i) {
        const char* a = g_scancode_layouts[i].name;
        const char* b = name;
        while (*a && *b && *a == *b) { ++a; ++b; }
        if (*a == '\0' && *b == '\0') return i;
    }
    return -1;
}

#endif
