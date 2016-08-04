#if defined(AE_TEST_MAIN)
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <netdb.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include "anet.h"
#include "ae.h"
#include "std.h"
#include "alloc.h"
#include "sds.h"
#include "thread.h"
#include "queue.h"
#include "sem.h"
#include "hash.h"
#include "mutex.h"

#define C_OK                    0
#define C_ERR                   -1

#define LOG_MAX_LEN    1024 /* Default maximum length of syslog messages */
#define ANET_ERR_LEN 256
#define CONFIG_DEFAULT_TCP_BACKLOG       511     /* TCP listen backlog */
#define CONFIG_DEFAULT_SERVER_PORT       12318     /* TCP port */
#define CONFIG_BINDADDR_MAX 16

#define PROTO_MAX_QUERYBUF_LEN  (1024*1024*1024) /* 1GB max query buffer. */
#define PROTO_IOBUF_LEN         (1024*16)  /* Generic I/O buffer size */
#define PROTO_REPLY_CHUNK_BYTES (16*1024) /* 16k output buffer */
#define PROTO_INLINE_MAX_SIZE   (1024*64) /* Max size of inline reads */
#define PROTO_MBULK_BIG_ARG     (1024*32)
#define LONG_STR_SIZE      21          /* Bytes needed for long -> str + '\0' */
#define AOF_AUTOSYNC_BYTES (1024*1024*32) /* fdatasync every 32MB */
#define NET_IP_STR_LEN 46 /* INET6_ADDRSTRLEN is 46, but we need to be sure */

#define UNUSED(V) ((void) V)
/* Socket status */
#define SOCKET_IDLE -1
#define SOCKET_WORKING 1
#define SOCKET_CLOSE 0
/* Log levels */
#define LL_DEBUG 0
#define LL_VERBOSE 1
#define LL_NOTICE 2
#define LL_WARNING 3
#define LL_RAW (1<<10) /* Modifier to log without timestamp */
#define CONFIG_DEFAULT_VERBOSITY LL_NOTICE

typedef struct socketLink {
    long long ctime;            /* Link creation time */
    int fd;                     /* TCP socket file descriptor */
    sds sndbuf;                 /* Packet send buffer */
    sds rcvbuf;                 /* Packet reception buffer */
    sds tmpbuf;
    int status;
    struct nn_queue_item item;  /* Queue of task */
} socketLink;

/* Return the UNIX time in microseconds */
struct redisServer {
    /* General */
    pid_t pid;                  /* Main process pid. */
    int working_thread;         /* number of working thread */
    int working_socket;         /* number of working socket */
    aeEventLoop *el;
    /* Networking */
    int port;                   /* TCP listening port */
    int tcp_backlog;            /* TCP listen() backlog */
    char *bindaddr[CONFIG_BINDADDR_MAX]; /* Addresses we should bind to */
    int bindaddr_count;         /* Number of addresses in server.bindaddr[] */
    int ipfd[CONFIG_BINDADDR_MAX]; /* TCP socket file descriptors */
    int ipfd_count;             /* Used slots in ipfd[] */
    int send_timeout;           /* Timeout of send message*/
    int recv_timeout;           /* Timeout of recv message*/
    char neterr[ANET_ERR_LEN];  /* Error buffer for anet.c */
    int verbosity;
    char *logfile;              /* Path of log file */
    char *protocol;             /* Header of protocol */
    int protocol_len;           /* Length of protocol header */
    int client_max_querybuf_len;/* Max len of query buf */
    struct nn_queue qthreads;   /* threads queue */
    struct nn_queue qtasks;     /* task queue */
    struct nn_queue unuse;      /* idle socket queue */
    struct hash     hlist;      /* command list */
    nn_mutex_t mutex;           /* mutex */
    socketLink *sockets;
    int quit;
};

typedef struct queue_thread_info{
    struct nn_sem sem;
    socketLink *link;
    struct nn_queue_item item;
} queue_thread_info;

typedef void redisCommandProc(socketLink *c);
typedef struct redisCommand {
    char *name;
    redisCommandProc *proc;
    ssize_t  commandNum;
    long long microseconds, calls;
}redisCommand;
/////////////////////////////////////////////////////////////////////////////////
struct redisServer server; /* server global state */
long long counter = 0;

void testCommand(socketLink *link);
void quitCommand(socketLink *link);
struct redisCommand redisCommandTable[] = {
    {"test",testCommand,1,0,0},
    {"quit",quitCommand,2,0,0}
}; 

typedef struct cmd_entry { 
    struct redisCommand *cmd;
    hash_item item;    
} cmd_entry;

void populateCommandTable(void) {
    int j;
    cmd_entry *item;
    int numcommands = sizeof(redisCommandTable)/sizeof(struct redisCommand);

    for (j = 0; j < numcommands; j++) {
        struct redisCommand *cmd = redisCommandTable+j;
        item = (cmd_entry *)nn_malloc(sizeof(*item));
        nn_hash_item_init(&item->item);
        item->cmd = cmd;

        nn_hash_insert(&server.hlist, (void *)cmd->commandNum, &item->item);
    }
}

void cleanCommandTable(void) {
    hash_item *it; 
    cmd_entry *item;
    int j, numcommands;

    numcommands= sizeof(redisCommandTable)/sizeof(struct redisCommand);
    for (j = 0; j < numcommands; j++) {
        struct redisCommand *cmd = redisCommandTable+j;

        it = nn_hash_get(&server.hlist, (void *)cmd->commandNum);
        nn_hash_erase(&server.hlist, it);
        item = nn_cont (it, struct cmd_entry, item);
        nn_free(item);
    }
}

long long ustime(void) {
    struct timeval tv;
    long long ust;

    gettimeofday(&tv, NULL);
    ust = ((long long)tv.tv_sec)*1000000;
    ust += tv.tv_usec;
    return ust;
}

long long mstime(void) {
    return ustime()/1000;
}

void daemonize(void) {
    int fd;

    if (fork() != 0) exit(0); /* parent exits */
    setsid(); /* create a new session */

    /* Every output goes to /dev/null. If Redis is daemonized but
     * the 'logfile' is set to 'stdout' in the configuration file
     * it will not log at all. */
    if ((fd = open("/dev/null", O_RDWR, 0)) != -1) {
        dup2(fd, STDIN_FILENO);
        dup2(fd, STDOUT_FILENO);
        dup2(fd, STDERR_FILENO);
        if (fd > STDERR_FILENO) close(fd);
    }
}

void serverLogRaw(int level, const char *msg) {
    const char *c = ".-*#";
    FILE *fp;
    char buf[64];
    int rawmode = (level & LL_RAW);
    int log_to_stdout = server.logfile[0] == '\0';

    level &= 0xff; /* clear flags */
    if (level < server.verbosity) return;

    fp = log_to_stdout ? stdout : fopen(server.logfile,"a");
    if (!fp) return;

    if (rawmode) {
        fprintf(fp,"%s",msg);
    } else {
        int off;
        struct timeval tv;
        int role_char;

        gettimeofday(&tv,NULL);
        off = strftime(buf,sizeof(buf),"%d %b %H:%M:%S.",localtime(&tv.tv_sec));
        snprintf(buf+off,sizeof(buf)-off,"%03d",(int)tv.tv_usec/1000);
        fprintf(fp,"%d %s %c %s\n",
                (int)getpid(), buf,c[level],msg);
    }
    fflush(fp);

    if (!log_to_stdout) fclose(fp);
}

void serverLog(int level, const char *fmt, ...) {
    va_list ap;
    char msg[LOG_MAX_LEN];

    if ((level&0xff) < server.verbosity) return;

    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);
    serverLogRaw(level,msg);
}

void socketLink_init(socketLink *link) {
    link->ctime = mstime();
    link->rcvbuf = sds_empty();
    link->sndbuf = sds_empty();
    link->tmpbuf = sds_empty();
    link->tmpbuf = sds_make_room_for(link->tmpbuf, 1<<20);
    link->fd = -1;
    link->status = SOCKET_IDLE;
    nn_queue_item_init(&link->item);
}

void socketLink_term(socketLink *link) {
    if(link->fd != -1){
        aeDeleteFileEvent(server.el, link->fd, AE_WRITABLE);
        aeDeleteFileEvent(server.el, link->fd, AE_READABLE);
    }
    sds_free(link->rcvbuf);
    sds_free(link->sndbuf);
    sds_free(link->tmpbuf);
    close(link->fd);
    link->fd = SOCKET_CLOSE;
}

void initServerConfig(void) {
    int j;
    server.pid = 0;
    server.working_thread = 16;
    server.working_socket = 32;
    server.port = CONFIG_DEFAULT_SERVER_PORT;
    server.tcp_backlog = CONFIG_DEFAULT_TCP_BACKLOG;
    server.bindaddr_count = 0;
    server.ipfd_count = 0;
    server.verbosity = CONFIG_DEFAULT_VERBOSITY;
    server.logfile = nn_strdup("");
    server.protocol = "MERGE3.0";
    server.protocol_len = 8;
    server.client_max_querybuf_len = 1<<20;
    server.send_timeout = 5000;
    server.recv_timeout = 5000;
    server.quit = 0;
    nn_queue_init(&server.qthreads);
    nn_queue_init(&server.qtasks);
    nn_queue_init(&server.unuse);
    nn_hash_init(&server.hlist);
    nn_mutex_init(&server.mutex);
    server.sockets = nn_malloc(sizeof(socketLink)*server.working_socket);
    if(server.sockets == 0)
    {
        serverLog(LL_WARNING, "malloc sockets error %s \n", "!!!!!");
        return;
    }
    for(j=0; j<server.working_socket; j++) {
        socketLink_init(&server.sockets[j]);
        nn_queue_push(&server.unuse, &server.sockets[j].item);
    }
}

void termServerConfig(void) {
    int j;

    nn_queue_term(&server.qthreads);
    nn_queue_term(&server.qtasks);
    nn_queue_term(&server.unuse);
    nn_hash_term(&server.hlist);
    nn_mutex_term(&server.mutex);
    for(j=0; j<server.working_socket; j++) {
        socketLink_term(&server.sockets[j]);
    }
    nn_free(server.sockets);
}

socketLink *createSocketLink() {
    struct nn_queue_item *it;
    socketLink *link = 0;
    it = nn_queue_pop(&server.unuse); 
    if(it != 0)
    {
        link = nn_cont(it, struct socketLink,  item);
        link->ctime = mstime();
        link->status = SOCKET_IDLE;
        sds_set_len(link->rcvbuf, 0);
        sds_set_len(link->sndbuf, 0);
        sds_set_len(link->tmpbuf, 0);
    }
    return link;
}

void freeSocketLink(socketLink *link) {
    if(link->fd != -1){
        aeDeleteFileEvent(server.el, link->fd, AE_WRITABLE);
        aeDeleteFileEvent(server.el, link->fd, AE_READABLE);
    }
    close(link->fd);
    if(!nn_queue_item_isinqueue(&link->item))
        nn_queue_push(&server.unuse, &link->item);
}

void queue_thread_info_init(queue_thread_info *thread)
{
    nn_sem_init(&thread->sem);
    thread->link = 0;
    nn_queue_item_init(&thread->item);
}

void queue_thread_info_term(queue_thread_info *thread)
{
    nn_sem_term(&thread->sem);
    nn_queue_item_term(&thread->item);
}

void writeMessageToClient(aeEventLoop *el, int fd, void *privdata, int mask) {
    socketLink *link = (socketLink*) privdata;
    ssize_t nwritten;
    UNUSED(el);
    UNUSED(mask);

    if (sds_len(link->sndbuf) == 0) {
        aeDeleteFileEvent(server.el, link->fd, AE_WRITABLE);
        if(fd == link->fd)freeSocketLink(link);
        return;
    }
    nwritten = write(fd, link->sndbuf, sds_len(link->sndbuf));
    if (nwritten <= 0) {
        serverLog(LL_WARNING,"write I/O error writing to node link: %s",
                strerror(errno));
        if(fd == link->fd)freeSocketLink(link);
        return;
    }
    sds_range(link->sndbuf,nwritten,-1);
    if (sds_len(link->sndbuf) == 0) {
        aeDeleteFileEvent(server.el, link->fd, AE_WRITABLE);
        if(fd == link->fd)freeSocketLink(link);
    }
}

void sendMessageToClient(socketLink *link)
{
    ssize_t nwritten;
    nwritten = write(link->fd, link->sndbuf, sds_len(link->sndbuf));
    if (nwritten > 0) {
        sds_range(link->sndbuf,nwritten,-1);
    }
    aeCreateFileEvent(server.el,link->fd, AE_WRITABLE, writeMessageToClient,link);
}

void readQueryFromClient(aeEventLoop *el, int fd, void *privdata, int mask) 
{
    ssize_t nread;
    socketLink *link = (socketLink*) privdata;
    unsigned int readlen, rcvbuflen;
    UNUSED(el);
    UNUSED(mask);

    readlen = PROTO_IOBUF_LEN;
    rcvbuflen = sds_len(link->rcvbuf);
    if (sds_avail(link->rcvbuf) < readlen)
        link->rcvbuf = sds_make_room_for(link->rcvbuf, readlen);

    nread = read(fd, link->rcvbuf+rcvbuflen,readlen);
    if (nread == -1 && errno == EAGAIN) return; /* No more data ready. */

    if (nread <= 0) {
        /* I/O error... */
        serverLog(LL_WARNING,"I/O error reading from node link: %s",
                (nread == 0) ? "connection closed" : strerror(errno));

        if(fd == link->fd && link->status == SOCKET_IDLE)freeSocketLink(link);
        else link->status = SOCKET_CLOSE;
        return;
    } 
    sds_inc_len(link->rcvbuf,nread);

    if (sds_len(link->rcvbuf) > server.client_max_querybuf_len) {
        serverLog(LL_WARNING,"Closing client that reached max query buffer length");
        if(fd == link->fd && link->status == SOCKET_IDLE)freeSocketLink(link);
        else link->status = SOCKET_CLOSE;
        return;
    }

    link->status = SOCKET_WORKING;
    if(!nn_queue_item_isinqueue(&link->item))
        nn_queue_push(&server.qtasks, &link->item);
}

void quitCommand(socketLink *link)
{
    server.quit = 1;
    aeStop(server.el);
}

void testCommand(socketLink *link)
{
    link->sndbuf = sds_copy(link->sndbuf, link->rcvbuf);
    counter ++;
    printf("recvbuf :%s counter: %lld\n", link->rcvbuf, counter);
}

void thread_process(void *this)
{
    struct queue_thread_info *thread;
    struct socketLink *link;
    struct cmd_entry *command;
    struct hash_item *it;
    ssize_t cmdnum = 1;

    thread = (queue_thread_info *)this; 
    while(!server.quit)
    {
        thread->link = 0;
        if(!nn_queue_item_isinqueue(&thread->item))
        {
            nn_mutex_lock(&server.mutex);
            nn_queue_push(&server.qthreads, &thread->item);
            nn_mutex_unlock(&server.mutex);
        }
        nn_sem_wait(&thread->sem);

        if((link = thread->link) == NULL)
            continue;

        if(link->status != SOCKET_CLOSE) 
        {
            ////////////////////////////////
            it = nn_hash_get(&server.hlist, (void *)cmdnum);
            command = nn_cont (it, struct cmd_entry, item);
            command->cmd->proc(link);
        }
        sendMessageToClient(link);
    }
}

void queue_task_exec()
{
    struct queue_thread_info *thread;
    long long ntime;
    socketLink *link;
    ntime = mstime();
    /*任务分发 超时检查  */
    while(1) {
        struct nn_queue_item *titem = nn_queue_pop (&server.qtasks);
        if(titem == 0)
            return;

        link = nn_cont(titem, struct socketLink,  item);
        if(ntime-link->ctime > server.send_timeout || link->status == SOCKET_CLOSE) {
            freeSocketLink(link);
            continue;
        }

        nn_mutex_lock(&server.mutex);
        struct nn_queue_item *item = nn_queue_pop (&server.qthreads);
        nn_mutex_unlock(&server.mutex);
        if(item != 0) {
            thread = nn_cont(item, struct queue_thread_info, item);
            thread->link = link;
            nn_sem_post(&thread->sem);  
        } else {
            nn_queue_item_init(titem);
            nn_queue_push(&server.qtasks, titem);
            return;
        }
    }
}

int check_timeout(struct aeEventLoop *eventLoop, long long id, void *clientData) 
{
    socketLink *link;
    long long ntime;
    int j;

    ntime = mstime();
    for(j=0; j<server.working_socket; j++) {
        link = &server.sockets[j];

        if(!nn_queue_item_isinqueue(&link->item) 
                &&(ntime-link->ctime > server.send_timeout *2))
        {
            freeSocketLink(link);
        }
    }   /*任务分发 超时检查  */
    return server.send_timeout;
}

void beforeSleep(struct aeEventLoop *eventLoop) {
    //printf("beforeSleep\n");
    //
    queue_task_exec();
}

int serverCron(struct aeEventLoop *eventLoop, long long id, void *clientData) {
    //printf("hello server \n");
    //retun AE_NOMORE -1 stop the task >0 间隔时间
    return 1;
}

void acceptTcpHandler(aeEventLoop *el, int fd, void *privdata, int mask) 
{
    int cport, cfd;
    char cip[NET_IP_STR_LEN];
    UNUSED(el);
    UNUSED(mask);
    UNUSED(privdata);

    cfd = anetTcpAccept(server.neterr, fd, cip, sizeof(cip), &cport);
    if (cfd == ANET_ERR) {
        if (errno != EWOULDBLOCK)
            serverLog(LL_WARNING,
                    "Accepting client connection: %s", server.neterr);
        return;
    }

    anetNonBlock(NULL,cfd);
    anetSendTimeout(NULL,cfd,server.send_timeout);

    anetEnableTcpNoDelay(NULL,cfd);
    serverLog(LL_VERBOSE,"Accepted cluster node %s:%d", cip, cport);

    socketLink *link =createSocketLink();
    if(link == 0)
    {
        serverLog(LL_WARNING,
                "Accepting client connection server no idle socket");
        close(cfd);
        return;
    }
    link->fd = cfd;
    if (aeCreateFileEvent(server.el, cfd, AE_READABLE, readQueryFromClient, link) == AE_ERR)
    {
        freeSocketLink(link);
        return;
    }
}

int listenToPort(int port, int *fds, int *count) {
    int j;

    /* Force binding of 0.0.0.0 if no bind address is specified, always
     * entering the loop if j == 0. */
    if (server.bindaddr_count == 0) server.bindaddr[0] = NULL;
    for (j = 0; j < server.bindaddr_count || j == 0; j++) {
        if (server.bindaddr[j] == NULL) {
            /* Bind * for both IPv6 and IPv4, we enter here only if
             * server.bindaddr_count == 0. */
            fds[*count] = anetTcp6Server(server.neterr,port,NULL,
                    server.tcp_backlog);
            if (fds[*count] != ANET_ERR) {
                anetNonBlock(NULL,fds[*count]);
                (*count)++;

                /* Bind the IPv4 address as well. */
                fds[*count] = anetTcpServer(server.neterr,port,NULL,
                        server.tcp_backlog);
                if (fds[*count] != ANET_ERR) {
                    anetNonBlock(NULL,fds[*count]);
                    (*count)++;
                }
            }
            /* Exit the loop if we were able to bind * on IPv4 and IPv6,
             * otherwise fds[*count] will be ANET_ERR and we'll print an
             * error and return to the caller with an error. */
            if (*count == 2) break;
        } else if (strchr(server.bindaddr[j],':')) {
            /* Bind IPv6 address. */
            fds[*count] = anetTcp6Server(server.neterr,port,server.bindaddr[j],
                    server.tcp_backlog);
        } else {
            /* Bind IPv4 address. */
            fds[*count] = anetTcpServer(server.neterr,port,server.bindaddr[j],
                    server.tcp_backlog);
        }
        if (fds[*count] == ANET_ERR) {
            serverLog(LL_WARNING,
                    "Creating Server TCP listening socket %s:%d: %s",
                    server.bindaddr[j] ? server.bindaddr[j] : "*",
                    port, server.neterr);
            return C_ERR;
        }
        anetNonBlock(NULL,fds[*count]);
        (*count)++;
    }
    return C_OK;
}

int aeTest(void) {
    int j, sfd;
    struct nn_thread *threads;
    struct queue_thread_info *thread_info;
    initServerConfig();
    nn_alloc_init(1,0);
    populateCommandTable();

    threads = nn_malloc(sizeof(*threads)*server.working_thread);
    if(threads == 0)
        return -1;

    thread_info = nn_malloc(sizeof(*thread_info)*server.working_thread);
    if(thread_info == 0)
        goto clean_threads;

    for(j=0; j<server.working_thread; j++) {
        queue_thread_info_init(&thread_info[j]);
        nn_thread_init(&threads[j], thread_process, &thread_info[j]);
    }

    server.el = aeCreateEventLoop(1000);
    if (server.port != 0 && listenToPort(server.port,server.ipfd,&server.ipfd_count) == C_ERR)
        return -1;

    if(aeCreateTimeEvent(server.el, 1, serverCron, NULL, NULL) == AE_ERR) 
        return -1;

    if(aeCreateTimeEvent(server.el, server.send_timeout, check_timeout, NULL, NULL) == AE_ERR) 
        return -1;

    for (j = 0; j < server.ipfd_count; j++) {
        if (aeCreateFileEvent(server.el, server.ipfd[j], AE_READABLE, acceptTcpHandler,NULL) == AE_ERR)
        {
            printf("Unrecoverable error creating server.ipfd file event.");
        }
    }

    aeSetBeforeSleepProc(server.el,beforeSleep);
    aeMain(server.el);
    aeDeleteEventLoop(server.el);

    for(j=0; j<server.working_thread; j++) {
        nn_thread_term(&threads[j]);
        queue_thread_info_term(&thread_info[j]);
    }
    nn_free(thread_info);
clean_threads:
    nn_free(threads);
    cleanCommandTable();
    termServerConfig();
    return 0;
}

int main(void) {
    return  aeTest();
}
#endif

