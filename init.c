/*************************************************************************
    > File Name: ini.c
    > Author: mbinary
    > Mail: zhuheqin1@gmail.com 
    > Blog: https://mbinary.github.io
    > Created Time: 2018-04-15  11:18
    > Function:
        implemented some shell cmds and features;
        including:
            cmds: pwd,ls, cd ,cat, env, export , unset, 
            features: multi-line input,  |  < >   >>   ;   &
 ************************************************************************/

#include <unistd.h>
#include <string.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <malloc.h>

#define MAX_CMD_LENGTH 255
#define MAX_PATH_LENGTH 255
#define MAX_BUF_SIZE  4096
#define MAX_ARG_NUM 50
#define MAX_CMD_NUM 10



struct cmd{
    struct cmd * next;
    int begin,end;  // pos in cmdStr
    int argc;
    char lredir,rredir; ////0:no redirect  1 <,>   ;  2  >>
    char toFile[MAX_PATH_LENGTH],fromFile[MAX_PATH_LENGTH];  // redirect file path
    char *args[MAX_ARG_NUM];
    char bgExec;
};

void init(struct cmd *pcmd){
    pcmd->bgExec=0;
    pcmd->argc=0;
    pcmd->lredir=pcmd->rredir=0;
    pcmd->next = NULL;
    pcmd->begin=-1;
}

struct cmd cmdinfo[MAX_CMD_NUM];
int cmdNum;
char cmdStr[MAX_CMD_LENGTH];    


int execInner(struct cmd* pcmd){  
    /*if inner cmd, {exec, return 0} else return 1  */
    if (!pcmd->args[0])
        return 0;
    if (strcmp(pcmd->args[0], "cd") == 0) {
        struct stat st;
        if (pcmd->args[1]){
            stat(pcmd->args[1],&st);
            if (S_ISDIR(st.st_mode))
                chdir(pcmd->args[1]);
            else{
                printf("[Error]: cd '%s': No such directory\n",pcmd->args[1]);
                return -1;
            }
        }
        return 0;
    }
    if (strcmp(pcmd->args[0], "pwd") == 0) {
        printf("%s\n",getcwd(pcmd->args[1] , MAX_PATH_LENGTH));
        return 0;
    }
    if (strcmp(pcmd->args[0], "unset") == 0) {
        for(int i=1;i<pcmd->argc;++i)unsetenv(pcmd->args[i]);
        return 0;
    }
    if (strcmp(pcmd->args[0], "export") == 0) {
        for(int i=1;i<pcmd->argc;++i)putenv(pcmd->args[i]);
        return 0;
    }
    if (strcmp(pcmd->args[0], "exit") == 0)
        exit(0);
    return 1;
} 
 
void setRedir(struct cmd *pcmd){
        /* settle file redirect  */
    if(pcmd->rredir){  //  >,  >>
        int flag ;
        if(pcmd->rredir==1)flag=O_WRONLY|O_TRUNC|O_CREAT;  // >
        else flag=O_WRONLY|O_APPEND|O_CREAT; //>>
        int fd = open(pcmd->fromFile,flag);
        dup2(fd,STDOUT_FILENO);
        close(fd);
    }
    if(pcmd->lredir){  //<, <<
        int fd = open(pcmd->fromFile,O_RDONLY|O_CREAT);
        dup2(fd,STDIN_FILENO);
        close(fd);
    }
}    


int fork_error(){
    printf("fork error\n");
    exit(1);
}

int execOuter(struct cmd * pcmd){
    if(!pcmd->next){
        execvp(pcmd->args[0],pcmd->args);
    }
    int fd[2];
    pipe(fd);
    pid_t pid = fork();
    if(pid<0){
        fork_error();
    }else if (pid==0){
        setRedir(pcmd);
        close(fd[0]);
        dup2(fd[1],STDOUT_FILENO);
        close(fd[1]);
        execvp(pcmd->args[0],pcmd->args);
        exit(1);
    }else{
        wait(NULL);
        close(fd[1]);
        dup2(fd[0],STDIN_FILENO);
        pcmd = pcmd->next;  
        setRedir(pcmd);        
        execOuter(pcmd);
        close(fd[0]);
    }
}
  
int getPath(char *path,int p){   
    /* get redirect file path from the cmdStr */
    int ct=0;
    while(cmdStr[++p]==' ');
    char c;
    while(c=path[ct++]=cmdStr[p++]){
        putchar(c);
        if(c==' '||c=='|'||c=='<'||c=='>')break;
    }
    path[ct]='\0';
    return p-1;
}

int parseArgs(){
    /* get args of each cmd and  create cmd-node seperated by pipe */
    int ct=0;
    char beginItem=0;
    for(int p=0;p<cmdNum;++p){
        struct cmd * pcmd=&cmdinfo[p];
        int begin = pcmd->begin,end = pcmd->end;
        init(pcmd);// initalize 
        for(int i=begin;i<end;++i){
            if(cmdStr[i]==' '||cmdStr[i]=='\0'){
                if(beginItem){
                    beginItem=0;
                    cmdStr[i]='\0';
                }
            }else if(cmdStr[i]=='<'){
                if(cmdStr[i+1]=='<'){
                    pcmd->lredir+=2;  //<<
                    cmdStr[i+1]=cmdStr[i]=' ';
                }else{
                    pcmd->lredir+=1;  //<
                    cmdStr[i]=' ';
                }
                i = getPath(pcmd->fromFile,i);
            }else if(cmdStr[i]=='>'){
                if(cmdStr[i+1]=='>'){
                    pcmd->rredir+=2;  //>>
                    cmdStr[i+1]=cmdStr[i]=' ';
                }else{
                    pcmd->rredir+=1;  //>
                    cmdStr[i]=' ';
                }
                i = getPath(pcmd->toFile,i);
            }else if (cmdStr[i]=='|'){
                /*when encountering pipe | , create new cmd node chained after the fommer one   */
                beginItem=0;
                cmdStr[i]='\0';
                pcmd->end = i;
                pcmd->next = (struct cmd*)malloc(sizeof(struct cmd));
                pcmd = pcmd->next;
                init(pcmd);
            }else{
                if(pcmd->begin==-1)pcmd->begin=i;
                if(!beginItem){
                    beginItem=1;
                    pcmd->args[pcmd->argc++]=cmdStr+i;
                }
            }
        }
        pcmd->end=end;
    }
}

int  parseCmds(){
    /* clean the cmdStr and get pos of each cmd in the cmdStr */
    int pCmdStr=0;
    char beginCmd=0;
    char newline = 1;
    /* multi line input */
    while(newline){
        int cur = MAX_CMD_LENGTH-pCmdStr;
        if(cur<=0){
            printf("[Error]: You cmdStr is too long to exec.\n");
            return -1;// return -1 if cmdStr size is bigger than LENGTH
        }
        fgets(cmdStr+pCmdStr,cur,stdin);
        newline = 0;
        while(1){
            if(cmdStr[pCmdStr]=='\\'&&cmdStr[pCmdStr+1]=='\n'){
                newline=1;
                cmdStr[pCmdStr++]='\0';
                break;
            }
            else if(cmdStr[pCmdStr]=='\n'){
                break;
            }
            ++pCmdStr;
        }
    }
    /* begin parsing  (OoO) */
    struct cmd * head; // use head cmd to mark background.
    for( int i=0;i<=pCmdStr;++i){
        switch(cmdStr[i]){
            case '&':{
                if(cmdStr[i+1]=='\n'||cmdStr[i+1]==';'){
                    cmdStr[i]=' ';
                    head->bgExec=1;
                }
            }
            case '\"':{
                cmdStr[i]=' ';
                break;
            }
            case ';':{//including  ';'  a new cmdStr
                beginCmd = 0;
                cmdStr[i]='\0';  
                cmdinfo[cmdNum++].end=i;
                break;
            }
            case '\n':{
                cmdStr[i]='\0';
                cmdinfo[cmdNum++].end =i;
                return 0;
            }
            case ' ':break;
            default:if(!beginCmd){
                beginCmd=1;
                head = cmdinfo+cmdNum;
                cmdinfo[cmdNum].begin =  i;
            }
        }
    }
}


int main(){
    while (1){
        cmdNum =0;
        printf("# ");
        fflush(stdin);
        parseCmds();
        parseArgs();
        for(int i=0;i<cmdNum;++i){
            struct cmd *pcmd=cmdinfo+i, * tmp;
            //pcmd = reverse(pcmd);
            int status = execInner(pcmd);
            if(status==1){
                /*use child proc to  execute outer cmd, avioding exiting after executing  */
                pid_t pid = fork();
                if(pid==0)execOuter(pcmd);
                else if(pid<0)fork_error();
                if(!pcmd->bgExec)wait(NULL);  //background exec
            }
            /*free malloced piep-cmd-node   */
            pcmd=pcmd->next; // the first one is static , no need to free;
            while(pcmd){
                tmp = pcmd->next;
                free(pcmd);
                pcmd=tmp;
            } 
        }

    }
    return 0; 
}




/* useless  in this lab*/
struct cmd * reverse(struct cmd* p){
    struct cmd * q=p->next,*r;
    p->next = NULL;
    while(q){
        r = q->next;
        q->next = p;
        p = q;
        q = r;
    }
    return p;
}
 int LS(char *path){
    DIR *dirp;
    struct dirent d,*dp = &d;
    dirp  = opendir(path);
    int ct=0;
    while((dp=readdir(dirp))!=NULL){
        printf("%s\n",dp->d_name);//,++ct%5==0?' \n':' ');
    }
    closedir(dirp);
    return 0;
} 

int CAT(char * path){
    char buf[MAX_BUF_SIZE];
    int fd = open(path,'r'),n;
    while((n=read(fd,buf,MAX_BUF_SIZE))>0){
        write(STDOUT_FILENO,buf,n);
    }
    return 0;
}
int ENV(){
    extern char **environ;
    char **env=environ;
    while(*env){
        printf("%s\n",*(env++));
    }
    return 0;
}
