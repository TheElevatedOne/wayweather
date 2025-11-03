#ifndef LOGGING_H_
#define LOGGING_H_

/* Prints a message to stderr with a
 * specified type.
 * 0 -> LOG,
 * 1 -> ERROR,
 * 2 -> WARNING,
 * 3 -> INFO.
 */
void logger(const int type, const char *message);

#endif // !LOGGING_H_
