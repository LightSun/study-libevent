/*
 * Compile with:
 * cc -I/usr/local/include -o event-test event-test.c -L/usr/local/lib -levent
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <sys/types.h>
#include <sys/stat.h>
#ifndef WIN32
#include <sys/queue.h>
#include <unistd.h>
#include <sys/time.h>
#else
#include <windows.h>
#endif
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

#include <event.h>
#include "and_log.h"

//#include <stdio.h>
//#include <string.h>
#include <stddef.h>
#include <sys/socket.h>
#include <sys/un.h>

#undef perror
#define perror(...) LOGW(__VA_ARGS__)

static void
fifo_read(int fd, short event, void *arg)
{
	char buf[255];
	int len;
	struct event *ev = arg;
#ifdef WIN32
	DWORD dwBytesRead;
#endif

	/* Reschedule this event */
	event_add(ev, NULL);

	LOGD("fifo_read called with fd: %d, event: %d, arg: %p\n",
		fd, event, arg);
#ifdef WIN32
	len = ReadFile((HANDLE)fd, buf, sizeof(buf) - 1, &dwBytesRead, NULL);

	// Check for end of file. 
	if(len && dwBytesRead == 0) {
		fprintf(stderr, "End Of File");
		event_del(ev);
		return;
	}

	buf[dwBytesRead] = '\0';
#else
	len = read(fd, buf, sizeof(buf) - 1);

	if (len == -1) {
		LOGW("read");
		return;
	} else if (len == 0) {
        LOGD("Connection closed\n");
		return;
	}

	buf[len] = '\0';
#endif
    LOGI("Read: %s\n", buf);
}
/*
 * Create a UNIX-domain socket address in the Linux "abstract namespace".
 *
 * The socket code doesn't require null termination on the filename, but
 * we do it anyway so string functions work.
 */
int makeAddr(const char* name, struct sockaddr_un* pAddr, socklen_t* pSockLen)
{
	int nameLen = strlen(name);
	if (nameLen >= (int) sizeof(pAddr->sun_path) -1)  /* too long? */
		return -1;
	pAddr->sun_path[0] = '\0';  /* abstract namespace */
	strcpy(pAddr->sun_path+1, name);
	pAddr->sun_family = AF_LOCAL;
	*pSockLen = 1 + nameLen + offsetof(struct sockaddr_un, sun_path);
	return 0;
}
/**
struct stat {
    dev_t     st_dev;      文件的设备编号
    ino_t     st_ino;      节点
    mode_t    st_mode;     文件的类型和权限
    nlink_t   st_nlink;    连到该文件的硬链接数目，刚建立的文件值为1
    uid_t     st_uid;      用户ID
    gid_t     st_gid;      组ID
    dev_t     st_rdev;     设备类型）若此文件为设备文件，则为其设备编号
    off_t     st_size;     文件字节数（文件大小）
    blksize_t st_blksize;  块大小（文件I/O缓冲区的大小）
    blkcnt_t  st_blocks;   块数
    time_t    st_atime;    最后一次访问时间
    time_t    st_mtime;    最后一次修改时间
    time_t    st_ctime;    最后一次改变时间（指属性）
};
 * @param argc
 * @param argv
 * @return
 */
int
main0 (int argc, const char *path)
{
	struct event evfifo;
#ifdef WIN32
	HANDLE socket;
	// Open a file. 
	socket = CreateFileA("test.txt",     // open File 
			GENERIC_READ,                 // open for reading 
			0,                            // do not share 
			NULL,                         // no security 
			OPEN_EXISTING,                // existing file only 
			FILE_ATTRIBUTE_NORMAL,        // normal file 
			NULL);                        // no attr. template 

	if(socket == INVALID_HANDLE_VALUE)
		return -1;

#else
	struct stat st;
	const char *fifo = "event.fifo";
	//const char *fifo = path;
	int fd;
    //获取文件信息。linux下一切皆为文件
	if (lstat (fifo, &st) == 0) {
		if ((st.st_mode & S_IFMT) == S_IFREG) {
			errno = EEXIST;
			perror("lstat"); //direct to 'stderr'
            return -1;
		}
	}
	unlink (fifo);

	/* Linux pipes are broken, we need O_RDWR instead of O_RDONLY */
#ifdef ANDROID
	struct sockaddr_un sockAddr;
	socklen_t sockLen;

	if (makeAddr(fifo, &sockAddr, &sockLen) < 0)
		return 1;
	fd = socket(AF_LOCAL, SOCK_STREAM, PF_UNIX);
	if (fd < 0) {
		perror("client socket()");
		return 1;
	}
#elif __linux
	fd = open (fifo, O_RDWR | O_NONBLOCK, 0);
	//mkfifo: 创建命名管道，在android中不支持. 由于sd卡是fat32
	/*if (mkfifo (fifo, 0600) == -1) {
		perror("mkfifo");
        return -1;
	}*/
#else
	fd = open (fifo, O_RDONLY | O_NONBLOCK, 0);
#endif

	if (fd == -1) {
		perror("open");
        return -1;
	}

	LOGD("Write data to %s\n", fifo);
#endif
	/* Initalize the event library */
	struct event_base *pBase = event_init();

	/* Initalize one event */
#ifdef WIN32
	event_set(&evfifo, (int)fd, EV_READ, fifo_read, &evfifo);
#else
	event_set(&evfifo, fd, EV_READ, fifo_read, &evfifo);
#endif

	/* Add it to the active events, without a timeout */
	event_add(&evfifo, NULL);
	
	event_dispatch();
#ifdef ANDROID
	close(fd); //must close at last.
#elif WIN32
	CloseHandle(fd);
#endif
	return (0);
}

