#ifndef WW_HELP_H_
#define WW_HELP_H_

#include <stdbool.h>
#include <stdio.h>

void help_set_color(bool color);
void help_main(FILE *out);
/* Prints help for a command or topic; false if unknown */
bool help_command(FILE *out, const char *name);
void help_version(FILE *out);

#endif // !WW_HELP_H_
