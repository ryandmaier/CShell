#ifndef SHELL_H
#define SHELL_H

typedef struct {
   char * com;
   char * args[13];
   int argCount;
   char * input;
   char * output;
} Command;

int openFile(const char *fileName, const char *mode);
void launchShell();

#endif
