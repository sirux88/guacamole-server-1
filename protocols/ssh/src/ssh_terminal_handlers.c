
/* ***** BEGIN LICENSE BLOCK *****
 * Version: MPL 1.1/GPL 2.0/LGPL 2.1
 *
 * The contents of this file are subject to the Mozilla Public License Version
 * 1.1 (the "License"); you may not use this file except in compliance with
 * the License. You may obtain a copy of the License at
 * http://www.mozilla.org/MPL/
 *
 * Software distributed under the License is distributed on an "AS IS" basis,
 * WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License
 * for the specific language governing rights and limitations under the
 * License.
 *
 * The Original Code is libguac-client-ssh.
 *
 * The Initial Developer of the Original Code is
 * Michael Jumper.
 * Portions created by the Initial Developer are Copyright (C) 2011
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *
 * Alternatively, the contents of this file may be used under the terms of
 * either the GNU General Public License Version 2 or later (the "GPL"), or
 * the GNU Lesser General Public License Version 2.1 or later (the "LGPL"),
 * in which case the provisions of the GPL or the LGPL are applicable instead
 * of those above. If you wish to allow use of your version of this file only
 * under the terms of either the GPL or the LGPL, and not to allow others to
 * use your version of this file under the terms of the MPL, indicate your
 * decision by deleting the provisions above and replace them with the notice
 * and other provisions required by the GPL or the LGPL. If you do not delete
 * the provisions above, a recipient may use your version of this file under
 * the terms of any one of the MPL, the GPL or the LGPL.
 *
 * ***** END LICENSE BLOCK ***** */

#include <stdlib.h>

#include <guacamole/log.h>

#include "ssh_terminal.h"
#include "ssh_terminal_handlers.h"

int ssh_guac_terminal_echo(ssh_guac_terminal* term, char c) {

    int foreground = term->foreground;
    int background = term->background;

    switch (c) {

        /* Bell */
        case 0x07:
            break;

        /* Backspace */
        case 0x08:
            if (term->cursor_col >= 1)
                term->cursor_col--;
            break;

        /* Carriage return */
        case '\r':
            term->cursor_col = 0;
            break;

        /* Line feed */
        case '\n':
            term->cursor_row++;

            /* Scroll up if necessary */
            if (term->cursor_row > term->scroll_end) {
                term->cursor_row = term->scroll_end;

                /* Scroll up by one row */        
                ssh_guac_terminal_scroll_up(term, term->scroll_start, term->scroll_end, 1);

            }
            break;

        /* ESC */
        case 0x1B:
            term->char_handler = ssh_guac_terminal_escape; 
            break;

        /* Displayable chars */
        default:

            /* Wrap if necessary */
            if (term->cursor_col >= term->term_width) {
                term->cursor_col = 0;
                term->cursor_row++;
            }

            /* Scroll up if necessary */
            if (term->cursor_row > term->scroll_end) {
                term->cursor_row = term->scroll_end;

                /* Scroll up by one row */        
                ssh_guac_terminal_scroll_up(term, term->scroll_start, term->scroll_end, 1);

            }

            /* Handle reverse video */
            if (term->reverse) {
                int swap = background;
                background = foreground;
                foreground = swap;
            }

            /* Handle bold */
            if (term->bold && foreground <= 7)
                foreground += 8;

            ssh_guac_terminal_set(term,
                    term->cursor_row,
                    term->cursor_col,
                    c, foreground, background);

            /* Advance cursor */
            term->cursor_col++;

    }

    return 0;

}

int ssh_guac_terminal_escape(ssh_guac_terminal* term, char c) {

    switch (c) {

        case '(':
            term->char_handler = ssh_guac_terminal_charset; 
            break;

        case ']':
            term->char_handler = ssh_guac_terminal_osc; 
            break;

        case '[':
            term->char_handler = ssh_guac_terminal_csi; 
            break;

        default:
            guac_log_info("Unhandled ESC sequence: %c", c);
            term->char_handler = ssh_guac_terminal_echo; 

    }

    return 0;

}

int ssh_guac_terminal_charset(ssh_guac_terminal* term, char c) {
    term->char_handler = ssh_guac_terminal_echo; 
    return 0;
}

int ssh_guac_terminal_csi(ssh_guac_terminal* term, char c) {

    /* CSI function arguments */
    static int argc = 0;
    static int argv[16] = {0};

    /* Argument building counter and buffer */
    static int argv_length = 0;
    static char argv_buffer[256];

    /* FIXME: "The sequence of parameters may be preceded by a single question mark. */
    if (c == '?')
        return 0;

    /* Digits get concatenated into argv */
    if (c >= '0' && c <= '9') {

        /* Concatenate digit if there is space in buffer */
        if (argv_length < sizeof(argv_buffer)-1)
            argv_buffer[argv_length++] = c;

    }

    /* Any non-digit stops the parameter, and possibly the sequence */
    else {

        int i, row, col, amount;

        /* At most 16 parameters */
        if (argc < 16) {

            /* Finish parameter */
            argv_buffer[argv_length] = 0;
            argv[argc++] = atoi(argv_buffer);

            /* Prepare for next parameter */
            argv_length = 0;

        }

        /* Handle CSI functions */ 
        switch (c) {

            /* A: Move up */
            case 'A':

                /* Get move amount */
                amount = argv[0];
                if (amount == 0) amount = 1;

                /* Move cursor */
                term->cursor_row -= amount;
                if (term->cursor_row < 0)
                    term->cursor_row = 0;

                break;

            /* B: Move down */
            case 'B':

                /* Get move amount */
                amount = argv[0];
                if (amount == 0) amount = 1;

                /* Move cursor */
                term->cursor_row += amount;
                if (term->cursor_row >= term->term_height)
                    term->cursor_row = term->term_height - 1;

                break;

            /* D: Move left */
            case 'D':

                /* Get move amount */
                amount = argv[0];
                if (amount == 0) amount = 1;

                /* Move cursor */
                term->cursor_col -= amount;
                if (term->cursor_col < 0)
                    term->cursor_col = 0;

                break;

            /* C: Move right */
            case 'C':

                /* Get move amount */
                amount = argv[0];
                if (amount == 0) amount = 1;

                /* Move cursor */
                term->cursor_col += amount;
                if (term->cursor_col >= term->term_width)
                    term->cursor_col = term->term_width - 1;

                break;




            /* m: Set graphics rendition */
            case 'm':

                for (i=0; i<argc; i++) {

                    int value = argv[i];

                    /* Reset attributes */
                    if (value == 0) {
                        term->foreground = term->default_foreground;
                        term->background = term->default_background;
                        term->reverse = 0;
                        term->underscore = 0;
                        term->bold = 0;
                    }

                    /* Bold */
                    else if (value == 1)
                        term->bold = 1;

                    /* Underscore on */
                    else if (value == 4)
                        term->underscore = 1;

                    /* Foreground */
                    else if (value >= 30 && value <= 37)
                        term->foreground = value - 30;

                    /* Background */
                    else if (value >= 40 && value <= 47)
                        term->background = value - 40;

                    /* Underscore on, default foreground */
                    else if (value == 38) {
                        term->underscore = 1;
                        term->foreground = term->default_foreground;
                    }

                    /* Underscore off, default foreground */
                    else if (value == 39) {
                        term->underscore = 0;
                        term->foreground = term->default_foreground;
                    }

                    /* Reset background */
                    else if (value == 49)
                        term->background = term->default_background;

                    /* Reverse video */
                    else if (value == 7)
                        term->reverse = 1;

                    /* Reset reverse video */
                    else if (value == 27)
                        term->reverse = 0;

                    /* Reset intensity */
                    else if (value == 27)
                        term->bold = 0;

                    else
                        guac_log_info("Unhandled graphics rendition: %i", value);

                }

                break;

            /* r: Set scrolling region */
            case 'r':
                term->scroll_start = argv[0]-1;
                term->scroll_end   = argv[1]-1;
                break;

            /* H: Move cursor */
            case 'H':

                row = argv[0]; if (row != 0) row--;
                col = argv[1]; if (col != 0) col--;

                term->cursor_row = row;
                term->cursor_col = col;
                break;

            /* G: Move cursor, current row */
            case 'G':
                col = argv[0]; if (col != 0) col--;
                term->cursor_col = col;
                break;

            /* d: Move cursor, current col */
            case 'd':
                row = argv[0]; if (row != 0) row--;
                term->cursor_row = row;
                break;


            /* J: Erase display */
            case 'J':
 
                /* Erase from cursor to end of display */
                if (argv[0] == 0)
                    ssh_guac_terminal_clear_range(term,
                            term->cursor_row, term->cursor_col,
                            term->term_height-1, term->term_width-1,
                            term->background);
                
                /* Erase from start to cursor */
                else if (argv[0] == 1)
                    ssh_guac_terminal_clear_range(term,
                            0, 0,
                            term->cursor_row, term->cursor_col,
                            term->background);

                /* Entire screen */
                else if (argv[0] == 2)
                    ssh_guac_terminal_clear(term,
                            0, 0, term->term_height, term->term_width,
                            term->background);

                break;

            /* K: Erase line */
            case 'K':

                /* Erase from cursor to end of line */
                if (argv[0] == 0)
                    ssh_guac_terminal_clear(term,
                            term->cursor_row, term->cursor_col,
                            1, term->term_width - term->cursor_col,
                            term->background);


                /* Erase from start to cursor */
                else if (argv[0] == 1)
                    ssh_guac_terminal_clear(term,
                            term->cursor_row, 0,
                            1, term->cursor_col + 1,
                            term->background);

                /* Erase line */
                else if (argv[0] == 2)
                    ssh_guac_terminal_clear(term,
                            term->cursor_row, 0,
                            1, term->term_width,
                            term->background);

                break;

            /* L: Insert blank lines (scroll down) */
            case 'L':

                amount = argv[0];
                if (amount == 0) amount = 1;

                ssh_guac_terminal_scroll_down(term,
                        term->cursor_row, term->scroll_end, amount);

                break;

            /* M: Delete lines (scroll up) */
            case 'M':

                amount = argv[0];
                if (amount == 0) amount = 1;

                ssh_guac_terminal_scroll_up(term,
                        term->cursor_row, term->scroll_end, amount);

                break;

            /* P: Delete characters (scroll left) */
            case 'P':

                amount = argv[0];
                if (amount == 0) amount = 1;

                /* Scroll left by amount */
                if (term->cursor_col + amount < term->term_width)
                    ssh_guac_terminal_copy(term,
                            term->cursor_row, term->cursor_col + amount,
                            1,
                            term->term_width - term->cursor_col - amount, 
                            term->cursor_row, term->cursor_col);

                /* Clear right */
                ssh_guac_terminal_clear(term,
                        term->cursor_row, term->term_width - amount,
                        1, amount,
                        term->background);

                break;

            /* @: Insert characters (scroll right) */
            case '@':

                amount = argv[0];
                if (amount == 0) amount = 1;

                /* Scroll right by amount */
                if (term->cursor_col + amount < term->term_width)
                    ssh_guac_terminal_copy(term,
                            term->cursor_row, term->cursor_col,
                            1, term->term_width - term->cursor_col - amount, 
                            term->cursor_row, term->cursor_col + amount);

                /* Clear left */
                ssh_guac_terminal_clear(term,
                        term->cursor_row, term->cursor_col,
                        1, amount,
                        term->background);

                break;

            /* Warn of unhandled codes */
            default:
                if (c != ';')
                    guac_log_info("Unhandled CSI sequence: %c", c);

        }

        /* If not a semicolon, end of CSI sequence */
        if (c != ';') {
            term->char_handler = ssh_guac_terminal_echo;

            /* Reset parameters */
            for (i=0; i<argc; i++)
                argv[i] = 0;

            /* Reset argument counters */
            argc = 0;
            argv_length = 0;
        }

    }

    return 0;

}

int ssh_guac_terminal_osc(ssh_guac_terminal* term, char c) {
    /* TODO: Implement OSC */
    if (c == 0x9C || c == 0x5C || c == 0x07) /* ECMA-48 ST (String Terminator */
       term->char_handler = ssh_guac_terminal_echo; 
    return 0;
}
