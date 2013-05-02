/*
 * util.h
 *
 * common utilities for C programs
 *
 */

void * emalloc(size_t n);
void * erealloc(void *p, size_t n);
void fatal(char *s1, char *s2, int n);
