/* vim: tabstop=4 shiftwidth=4 noexpandtab
 *
 * ANSI-esque Terminal Escape Driver
 */

#include <system.h>
#include <logging.h>

/* Triggers escape mode. */
#define ANSI_ESCAPE  27
/* Escape verify */
#define ANSI_BRACKET '['
/* Anything in this range (should) exit escape mode. */
#define ANSI_LOW    'A'
#define ANSI_HIGH   'z'
/* Escape commands */
#define ANSI_CUU    'A' /* CUrsor Up                  */
#define ANSI_CUD    'B' /* CUrsor Down                */
#define ANSI_CUF    'C' /* CUrsor Forward             */
#define ANSI_CUB    'D' /* CUrsor Back                */
#define ANSI_CNL    'E' /* Cursor Next Line           */
#define ANSI_CPL    'F' /* Cursor Previous Line       */
#define ANSI_CHA    'G' /* Cursor Horizontal Absolute */
#define ANSI_CUP    'H' /* CUrsor Position            */
#define ANSI_ED     'J' /* Erase Data                 */
#define ANSI_EL     'K' /* Erase in Line              */
#define ANSI_SU     'S' /* Scroll Up                  */
#define ANSI_SD     'T' /* Scroll Down                */
#define ANSI_HVP    'f' /* Horizontal & Vertical Pos. XXX: SAME AS CUP */
#define ANSI_SGR    'm' /* Select Graphic Rendition   */
#define ANSI_DSR    'n' /* Device Status Report XXX: Push to kgets() buffer? */
#define ANSI_SCP    's' /* Save Cursor Position       */
#define ANSI_RCP    'u' /* Restore Cursor Position    */
#define ANSI_HIDE   'l' /* DECTCEM - Hide Cursor      */
#define ANSI_SHOW   'h' /* DECTCEM - Show Cursor      */
/* Display flags */
#define ANSI_BOLD      0x01
#define ANSI_UNDERLINE 0x02
#define ANSI_ITALIC    0x04
#define ANSI_FRAKTUR   0x08 /* As if I'll ever implement that */
#define ANSI_DOUBLEU   0x10
#define ANSI_OVERLINE  0x20
#define ANSI_BLINK     0x40
#define ANSI_CROSS     0x80 /* And that's all I'm going to support */

/* State machine status */
static struct _ansi_state {
	uint16_t x     ;  /* Current cursor location */
	uint16_t y     ;  /*    "      "       "     */
	uint16_t save_x;
	uint16_t save_y;
	uint32_t width ;
	uint32_t height;
	uint8_t  fg    ;  /* Current foreground color */
	uint8_t  bg    ;  /* Current background color */
	uint8_t  flags ;  /* Bright, etc. */
	uint8_t  escape;  /* Escape status */
	uint8_t  buflen;  /* Buffer Length */
	char *   buffer;  /* Previous buffer */
} state;

void (*ansi_writer)(char) = &bochs_write;

void
ansi_dump_buffer() {
	/* Assuming bochs_write() is the unprocessed output function */
	for (int i = 0; i < state.buflen; ++i) {
		ansi_writer(state.buffer[i]);
	}
}

void
ansi_buf_add(
		char c
		) {
	state.buffer[state.buflen] = c;
	state.buflen++;
	state.buffer[state.buflen] = '\0';
}

void
ansi_put(
		char c
		) {
	switch (state.escape) {
		case 0:
			/* We are not escaped, check for escape character */
			if (c == ANSI_ESCAPE) {
				/*
				 * Enable escape mode, setup a buffer,
				 * fill the buffer, get out of here.
				 */
				state.escape    = 1;
				state.buffer    = malloc(sizeof(char) * 100);
				state.buflen    = 0;
				ansi_buf_add(c);
				return;
			} else {
				ansi_writer(c);
			}
			break;
		case 1:
			/* We're ready for [ */
			if (c == ANSI_BRACKET) {
				state.escape = 2;
				ansi_buf_add(c);
			} else {
				/* This isn't a bracket, we're not actually escaped!
				 * Get out of here! */
				ansi_dump_buffer();
				ansi_writer(c);
				state.escape = 0;
				free(state.buffer);
				state.buffer = NULL;
				state.buflen = 0;
				return;
			}
			break;
		case 2:
			if (c >= ANSI_LOW && c <= ANSI_HIGH) {
				/* Woah, woah, let's see here. */
				char * pch;  /* tokenizer pointer */
				char * save; /* strtok_r pointer */
				char * argv[1024]; /* escape arguments */
				/* Get rid of the front of the buffer */
				strtok_r(state.buffer,"[",&save);
				pch = strtok_r(NULL,";",&save);
				/* argc = Number of arguments, obviously */
				int argc = 0;
				while (pch != NULL) {
					argv[argc] = (char *)pch;
					++argc;
					pch = strtok_r(NULL,";",&save);
				}
				argv[argc] = NULL;
				/* Alright, let's do this */
				switch (c) {
					case ANSI_SGR:
						/* Set Graphics Rendition */
						if (argc == 0) {
							/* Default = 0 */
							argv[0] = "0";
							argc    = 1;
						}
						for (int i = 0; i < argc; ++i) {
							int arg = atoi(argv[i]);
							if (arg >= 100 && arg < 110) {
								/* Bright background */
								state.bg = 8 + (arg - 100);
							} else if (arg >= 90 && arg < 100) {
								/* Bright foreground */
								state.fg = 8 + (arg - 90);
							} else if (arg >= 40 && arg < 49) {
								/* Set background */
								state.bg = arg - 40;
							} else if (arg == 49) {
								state.bg = 0;
							} else if (arg >= 30 && arg < 39) {
								/* Set Foreground */
								state.fg = arg - 30;
							} else if (arg == 39) {
								/* Default Foreground */
								state.fg = 7;
							} else if (arg == 20) {
								/* FRAKTUR: Like old German stuff */
								state.flags |= ANSI_FRAKTUR;
							} else if (arg == 9) {
								/* X-OUT */
								state.flags |= ANSI_CROSS;
							} else if (arg == 7) {
								/* INVERT: Swap foreground / background */
								uint8_t temp = state.fg;
								state.fg = state.bg;
								state.bg = temp;
							} else if (arg == 5) {
								/* BLINK: I have no idea how I'm going to make this work! */
								state.flags |= ANSI_BLINK;
								if (i == 0) { break; }
								if (i < argc) {
									if (atoi(argv[i-1]) == 48) {
										/* Background to i+1 */
										state.bg = atoi(argv[i+1]);
									} else if (atoi(argv[i-1]) == 38) {
										/* Foreground to i+1 */
										state.fg = atoi(argv[i+1]);
									}
									++i;
								}
							} else if (arg == 4) {
								/* UNDERLINE */
								state.flags |= ANSI_UNDERLINE;
							} else if (arg == 3) {
								/* ITALIC: Oblique */
								state.flags |= ANSI_ITALIC;
							} else if (arg == 1) {
								/* BOLD/BRIGHT: Brighten the output color */
								state.flags |= ANSI_BOLD;
							} else if (arg == 0) {
								/* Reset everything */
								state.fg = 7;
								state.bg = 0;
								state.flags = 0;
							}
						}
						break;
					case ANSI_SHOW:
						if (!strcmp(argv[0], "?1049")) {
							cls();
							bochs_set_csr(0,0);
						}
						break;
					case ANSI_CUF:
						{
							int i = 1;
							if (argc) {
								i = atoi(argv[0]);
							}
							bochs_set_csr(bochs_get_csr_x() + i, bochs_get_csr_y());
						}
						break;
					case ANSI_CUU:
						{
							int i = 1;
							if (argc) {
								i = atoi(argv[0]);
							}
							bochs_set_csr(bochs_get_csr_x(), bochs_get_csr_y() - i);
						}
						break;
					case ANSI_CUD:
						{
							int i = 1;
							if (argc) {
								i = atoi(argv[0]);
							}
							bochs_set_csr(bochs_get_csr_x(), bochs_get_csr_y() + i);
						}
						break;
					case ANSI_CUB:
						{
							int i = 1;
							if (argc) {
								i = atoi(argv[0]);
							}
							bochs_set_csr(bochs_get_csr_x() - i, bochs_get_csr_y());
						}
						break;
					case ANSI_CUP:
						if (argc < 2) {
							bochs_set_csr(0,0);
							break;
						}
						bochs_set_csr(atoi(argv[1]) - 1, atoi(argv[0]) - 1);
						break;
					case ANSI_ED:
						cls();
						break;
					case ANSI_EL:
						{
							int what = 0, x = 0, y = 0;
							if (argc >= 1) {
								what = atoi(argv[0]);
							}
							if (what == 0) {
								x = bochs_get_csr_x();
								y = bochs_get_width();
							} else if (what == 1) {
								x = 0;
								y = bochs_get_csr_x();
							} else if (what == 2) {
								x = 0;
								y = bochs_get_width();
							}
							for (int i = x; i < y; ++i) {
								bochs_set_cell(i, bochs_get_csr_y(), ' ');
							}
						}
						break;
					case 'X':
						{
						int how_many = 1;
						if (argc >= 1) {
							how_many = atoi(argv[0]);
						}
						for (int i = 0; i < how_many; ++i) {
							ansi_writer(' ');
						}
						}
						break;
					case 'd':
						if (argc < 1) {
							bochs_set_csr(bochs_get_csr_x(), 0);
						} else {
							bochs_set_csr(bochs_get_csr_x(), atoi(argv[0]) - 1);
						}
						break;
					default:
						/* Meh */
						break;
				}
				/* Set the states */
				if (state.flags & ANSI_BOLD && state.fg < 9) {
					bochs_set_colors(state.fg % 8 + 8, state.bg);
				} else {
					bochs_set_colors(state.fg, state.bg);
				}
				/* Clear out the buffer */
				free(state.buffer);
				state.buffer = NULL;
				state.buflen = 0;
				state.escape = 0;
				return;
			} else {
				/* Still escaped */
				ansi_buf_add(c);
			}
			break;
	}
}

void
ansi_init(void (*writer)(char), int w, int y) {
	LOG(INFO,"Initializing ANSI console, writer=0x%x, size=%dx%d", (uint32_t)writer, (uint32_t)w, (uint32_t)y);
	/* Terminal Defaults */
	state.fg     = 7; /* Light grey */
	state.bg     = 0; /* Black */
	state.flags  = 0; /* Nothing fancy*/
	state.width  = w; /* 1024 / 8  */
	state.height = y; /* 768  / 12 */
	ansi_ready   = 1;
	ansi_writer  = writer;
}

void
ansi_print(char * c) {
	uint32_t len = strlen(c);
	for (uint32_t i = 0; i < len; ++i) {
		ansi_put(c[i]);
		/* Assume our serial output is also kinda smart */
		serial_send(c[i]);
	}
}
