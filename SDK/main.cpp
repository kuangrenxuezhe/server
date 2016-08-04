#include <stdio.h>
#include <errno.h>
#include <fstream>
#include <string>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <stdarg.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include "alloc.h"

#if _WIN32

#include <winsock2.h>
#define strcasecmp          strcmpi
#define strncasecmp         _strnicmp
#define snprintf            _snprintf
#define strtoll             _strtoi64
#define strtoull            _strtoui64

#endif

int bOptGroup = 0;
int totalNumOfThread = 0;

int ReadFile(char *&buffer, const char *filename)
{
    struct stat sFileInfo;
    int ret = -1;

    if(filename == NULL)
    {
        printf("file name is null! \n");
        return ret;
    } 

    if(stat(filename, &sFileInfo) == -1)
    {
        printf("no find the file (%s)\n", filename);
        return ret;
    }

    FILE*fp=NULL;
    if((fp=fopen(filename,"r")) == NULL)
    {      
        printf("open (%s) error \n", filename);         
        return ret;
    }

    buffer = new char[sFileInfo.st_size+1];
    memset(buffer, 0, sFileInfo.st_size+1);

    if(fread(buffer, sFileInfo.st_size, 1, fp) != 1)
    {
        printf("fread (%s) error \n", filename);
        fclose(fp);
        return ret;
    }

    fclose(fp);    
    return sFileInfo.st_size;
}   

int cfg_process(char *path)
{
    return 0;
}

int help_process()
{
    fprintf(stdout,
            "Usage: Param [OPTIONS]\n"
            "\n"
            "Options are:\n"
            "-h, --help\t\tdisplay this help message\n"
            "-c, --config <file>\tread configuration from specified file\n"
            "-t, --threads \tset threads size\n"
            "-cmd, --command \tcall this command\n"
            "start\t start task\n"
            "stop\t stop task\n"
            "quit\t quit task\n"
           );
    return 0;
}

int start()
{
    int i;
    void *buffer[1000];
    for(i=0; i<1000; i++)
    {
        buffer[i] = nn_malloc(10000);
    }
    int size = nn_alloc_memory_state(NN_USED_MEMORY); 
    int block = nn_alloc_memory_state(NN_USED_BLOCKS);
    int rss = nn_alloc_get_rss();
    printf ("size:[%d]  block:[%d] rss:[%d]\n", size, block, rss);
    for(i=0; i<1000; i++)
    {
        nn_free(buffer[i]);
    }

    size = nn_alloc_memory_state(NN_USED_MEMORY); 
    block = nn_alloc_memory_state(NN_USED_BLOCKS);
    rss = nn_alloc_get_rss();
    printf ("size:[%d]  block:[%d] rss:[%d]\n", size, block, rss);

    return 0;
}

int cmd_process(char *cmd)
{
    if(strcasecmp(cmd,"quit") == 0)
    {
        return 0;
    }
    else if(strcasecmp(cmd,"start") == 0)
    {
        start();
        return 1;
    }
    else if(strcasecmp(cmd,"help") == 0)
    {
        help_process();
        return 1;
    }
    else
    {
        return -1;
    }
}
#ifdef TEST_MAIN
int main(int argc, char *argv[])
{
    int autoExec = 0;
    int ret = 0;
    //nn_alloc_init(1, 0);
    while(true)
    {
        for (int i=1; i<argc; i++ )
        {
            //设置配置文件参数
            if ((!strcasecmp (argv[i], "--config" ) || !strcasecmp (argv[i], "-c" )) && (i+1)<argc)
            {
                ret = cfg_process(argv[++i]);
                if (ret < 0) exit(EXIT_SUCCESS); 
            } 
            //设置帮助信息
            else if((!strcasecmp (argv[i], "--help") || !strcasecmp (argv[i], "-h")) )
            {
                ret = help_process();
                exit(EXIT_SUCCESS); 
            }
            else if((!strcasecmp (argv[i], "--group") || !strcasecmp (argv[i], "-g")) )
            {
                bOptGroup = 1;
            }
            else if((!strcasecmp (argv[i], "--threads") || !strcasecmp (argv[i], "-t")) && (i+1)<argc)
            {
                totalNumOfThread = atoi (argv[++i]);
                if (totalNumOfThread < 1)
                {
                    printf("threads count less more 1");
                    exit(EXIT_SUCCESS);  
                }
            }
            //设置自动化命令
            else if((!strcasecmp (argv[i], "--command") || !strcasecmp (argv[i], "-cmd")) && (i+1)<argc)
            {
                autoExec = ++i;
            }
        }
        //自动执行命令行命令
        if(autoExec != 0) ret = cmd_process(argv[autoExec]);

        argc = 1;
        //输入实时命令
        char command[256];
        scanf("%s",command);
        ret = cmd_process(command);
        if(0 == ret)
        {
            break;
        }
    }
    // nn_alloc_term();
    return ret;
}
#endif

