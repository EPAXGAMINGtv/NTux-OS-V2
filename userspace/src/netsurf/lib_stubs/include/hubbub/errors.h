#ifndef hubbub_errors_h_
#define hubbub_errors_h_

typedef enum {
	HUBBUB_OK               = 0,
	HUBBUB_PAUSED           = 1,
	HUBBUB_ENCODINGCHANGE   = 2,
	HUBBUB_NOMEM            = 3,
	HUBBUB_BADPARM          = 4,
	HUBBUB_INVALID          = 5,
	HUBBUB_FILENOTFOUND     = 6,
	HUBBUB_NEEDDATA         = 7,
	HUBBUB_BADENCODING      = 8,
	HUBBUB_UNKNOWN          = 9,
} hubbub_error;

#endif
