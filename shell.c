#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <ctype.h>
#include "shell.h"

void errorClose(char * msg)
{
   perror(msg);
   exit(EXIT_FAILURE);
}

char error(char * msg, char exit)
{
   fprintf(stderr,"%s\n",msg);
   return exit;
}

int openFile(const char *fileName, const char *mode)
{
   int fd, flags;
   
   if (0 == strcmp("r", mode))
      flags = O_RDONLY;
   else if (0 == strcmp("w", mode))
      flags = O_WRONLY | O_CREAT | O_TRUNC;
   else {
      fprintf(stderr, "Unknown openFile mode %s\n", mode);
      exit(EXIT_FAILURE);
   }
   if (-1 == (fd = open(fileName, flags, 0666)))
   {
      fprintf(stderr,"cshell: ");
      errorClose((char*)fileName);
   }
   return fd;
}

void parentAction(int fd[2], int * read, int i)
{
   if(close(fd[1])<0)
      errorClose(NULL);
   if(i>0)
   {
      if(close(*read)<0)
         errorClose(NULL);
   }
   *read = fd[0];
}

void setRedirection(Command com)
{
   /* red = (0,none) (1,in) (2,out) (3,both) */
   int file;
   if(com.input!=NULL)
   {
      file = openFile(com.input, "r");
      if(-1==dup2(file, STDIN_FILENO))
         errorClose(NULL);
   }
   if(com.output!=NULL)
   {
      file = openFile(com.output, "w");
      if(-1==dup2(file, STDOUT_FILENO))
         errorClose(NULL);
   }
}

void setLast(int savedOut)
{
   if(-1 == dup2(savedOut,STDOUT_FILENO))
      errorClose(NULL);
}

void childAction(Command com, int fd[2], int * read, int last, int savedOut)
{
   char c;
   if(last)
      setLast(savedOut);
   else if (-1 == dup2(fd[1],STDOUT_FILENO))
      errorClose(NULL);
   if(-1 == dup2(*read, STDIN_FILENO))
      errorClose(NULL);
   if(com.com!=NULL)
      setRedirection(com);
      
   if(close(fd[0])<0)
      errorClose(NULL);
   if(com.argCount>0)
   {
      execvp(com.com,com.args);
      fprintf(stderr,"cshell: %s: No such file or directory\n",com.com);
   }
   else {
      while (EOF != (c = getchar()))
         putchar(c);
   }
   exit(0);
}

void pipeAndFork(Command com, int i, int last, int* read, int fd[2])
{
   pid_t pid;
   int savedOut = dup(STDOUT_FILENO);
   if(pipe(fd)<0)
      errorClose(NULL);
   if(-1 == (pid = fork()))
      errorClose(NULL);
   else if(pid == 0)
      childAction(com,fd,read,last,savedOut);
   else
      parentAction(fd,read,i);
}

void pipeline(Command coms[20], int nComs, int savedIn)
{
   int i, status, fd[2];
   int read = savedIn;
   for(i=0; i<nComs; i++)
      pipeAndFork(coms[i],i,i==nComs-1,&read,fd);
   for(i=0; i<nComs; i++)
      wait(&status);
   if(close(fd[0])<0)
      errorClose(NULL);
}

char isRedirectStr(char * tok)
{
   return strcmp(tok,"<")==0 || strcmp(tok,">")==0;
}

char checkRedirect(Command * com, char * token)
{
   if(strcmp(token,"<")==0)
   {
      token = strtok(NULL, " ");
      if(token==NULL || isRedirectStr(token))
         return error("cshell: Syntax error",2);
      com->input = token;
      return 1;
   }
   if(strcmp(token,">")==0)
   {
      token = strtok(NULL, " ");
      if(token==NULL || isRedirectStr(token))
         return error("cshell: Syntax error",2);
      com->output = token;
      return 1;
   }
   return 0;
}

void firstCom(Command * com, char * token)
{
   com->com = token;
   com->args[0] = token;
   com->argCount = 1;
}

char newArg(Command * com, char * token)
{
   if(com->argCount==0)
      firstCom(com, token);
   else {
      if(com->argCount>=11)
      {
         fprintf(stderr,"cshell: %s: Too many arguments\n",com->com);
         return 1;
      }
      com->args[com->argCount] = token;
      com->argCount+=1;
   }
   return 0;
}

char readToken(Command coms[20], int i, char * str)
{
   char red;
   char * token = strtok(str, " ");
   if(i>0)
   {
      if(coms[i-1].com==NULL)
         return error("cshell: Invalid pipe",1);
   }
   coms[i].input = NULL;
   coms[i].output = NULL;
   coms[i].com = NULL;
   coms[i].argCount = 0;
   while(token != NULL)
   {
      red = checkRedirect(&(coms[i]),token);
      if(!red)
      {
         if(newArg(&(coms[i]),token))
            return 1;
      }
      else if(red==2)
         return 1;
      token = strtok(NULL, " ");
   }
   coms[i].args[coms[i].argCount] = NULL;
   return 0;
}

char onlySpaces(char * str)
{
   int i;
   int len = strlen(str);
   for(i=0; i<len; i++)
   {
      if(!isspace(str[i]))
         return 0;
   }
   return 1;
}

char parseCommands(char str[1025], int savedIn)
{
   int j;
   Command coms[20];
   char * comStrs[20];
   int i = 0;
   const char s[2] = "|";
   char * token = strtok(str, s);
   coms[i].input = "stdin";
   while(token != NULL)
   {
      if(i>=20)
         return error("cshell: Too many commands",1);
      if(onlySpaces(token))
         return error("cshell: Invalid pipe",1);
      comStrs[i] = token;
      token = strtok(NULL, s);
      i+=1;
   }
   for(j=0; j<i; j++)
   {
      if(readToken(coms,j,comStrs[j]))
         return 1;
   }
   pipeline(coms, i, savedIn);
   return 1;
}

char getLine(char line[1025], int savedIn)
{
   char str[4096];
   int len;
   printf(":-) ");
   if(-1==dup2(savedIn,STDIN_FILENO))
      errorClose(NULL);
   fgets(str,4096,stdin);
   if(feof(stdin))
   {
      printf("exit\n");
      return 0;
   }
   if(strlen(str)>1024)
      return error("cshell: Command line too long",1);
   len = strlen(str);
   if(str[len-1]=='\n')
      str[len-1]='\0';
   if(strcmp(str,"exit")==0)
      return 0;
   strcpy(line, str);
   return parseCommands(line, savedIn);
}

char getCommands(int savedIn)
{
   char line[1025];
   return getLine(line,savedIn);
}

void launchShell()
{
   char running = 1;
   int savedIn = dup(STDIN_FILENO);
   setbuf(stdout, NULL);
   while(running)
      running = getCommands(savedIn);
}
