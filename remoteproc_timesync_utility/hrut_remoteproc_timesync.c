#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <time.h>
#include <sys/time.h>  
#include <unistd.h>  
#include <signal.h>  
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <pthread.h>
#include <time.h>
#include <errno.h>
#include <sys/ioctl.h>

#define MAX_TIMESTAMP_LEN 32
#define PERIOD_TIMER 10

static uint32_t currenttime_second;
static uint32_t currenttime_nanosecond;
static int32_t currenttime_second_diff;

static int32_t fd_timestamp_dsp;

static void notify_remotecores(void)
{
	int ret;
	uint32_t timestamp[3];
	timestamp[0]=currenttime_second;
	timestamp[1]=currenttime_nanosecond;
	timestamp[2]=(uint32_t)currenttime_second_diff;

	//printf("timestamp =%d %d \n",timestamp[0],timestamp[1]);

	ret = write(fd_timestamp_dsp,  timestamp, sizeof(timestamp));
	if(ret < 0)
	{	
		printf("write fd_timestamp_dsp node fail,ret %d\n",ret);
		close(fd_timestamp_dsp);
		#if 0
		ret = write(fd_timestamp_dcore0, timestamp, sizeof(timestamp));
		if(ret < 0)
		{	
			printf("write fd_timestamp_dcore0 node fail,ret %d\n",ret);
			close(fd_timestamp_dcore0);
			ret = write(fd_timestamp_dcore1, timestamp, sizeof(timestamp));
			if(ret < 0)
			{	
				printf("write fd_timestamp_dcore1 node fail,ret %d\n",ret);
				close(fd_timestamp_dcore1);
			}
		}
		#endif
	}
}
static ssize_t localreadch(int fd, char *ptr)
{
    static int          count = 0;
    static char*        read_ptr = 0;
    static char         read_buf[1024*4] = {0};

    if (count <= 0)
    {
    again:
        count = read(fd, read_buf, sizeof(read_buf));
        if (-1 == count)
            if (EINTR == errno)
                goto again;
            else
                return -1;
        else if (0 == count)
            return 0;
        read_ptr = read_buf;
    }
    count--;
    *ptr = *read_ptr++;
    return 1;
}

static ssize_t localreadline(int fd, void *vptr, size_t maxlen)
{
     ssize_t         i = 0;
     ssize_t         ret = 0;
     char            ch = '\0';
     char*           ptr = NULL;
 
     ptr = (char *)vptr;
 
     for (i = 1; i < maxlen; ++i)
     {
         ret = localreadch(fd, &ch);
         if (1 == ret)
         {
             *ptr++ = ch;
             if ('\n' == ch)
                 break;
         }
         else if (0 == ret)
         {
             *ptr = 0;
             return i-1;
         }
         else
             return -1;
     }
     *ptr = 0;
     return i;
 }

static void get_current_time(void)
{
	struct timespec current_time;
	static int32_t diff=0;
	static char  valueline[4096];
	char* p;
	char* p1;
	int32_t i;
	static int fd=0;

	if(!diff){
		if(!fd){
			fd=open("/etc/profile", O_RDONLY);
		}
		if(fd){
			while(1){
				i=localreadline(fd, valueline, 4096);
				if(i==0){
					break;
				}

				if(strstr(valueline,"TZ=")){
					printf("valueline %s\n",valueline);

					p=strchr(valueline,'-');
					if(p){
						p1=strchr(valueline,':');
						if(p1){
							*p1='\0';
							p++;
							diff+=atoi(p);
						}
					}else {
						p=strchr(valueline,'+');
						if(p){
							p1=strchr(valueline,':');
							if(p1){
								*p1='\0';
								p++;
								diff-=atoi(p);
							}
						}
					}
				}
			}
		}
	}
	printf("diff %d\n",diff);

	clock_gettime(CLOCK_REALTIME, &current_time);
	currenttime_second = current_time.tv_sec;
	currenttime_nanosecond = current_time.tv_nsec;	
	currenttime_second_diff = diff*3600;
}

static void gettime_info(int signo)  
{  
	get_current_time();
	notify_remotecores();	
} 
 
static int open_timestamp_node(void)
{
	fd_timestamp_dsp = open("/sys/class/remoteproc/remoteproc0/timesync", O_RDWR);

	if((fd_timestamp_dsp <0) )
	{
		printf("fd_timestamp_dsp: %d\n",fd_timestamp_dsp);
		return -1;
	}

	return 0;
}
static void init_sigaction(void)  
{  
    struct sigaction tact;  
    tact.sa_handler = gettime_info;  
    tact.sa_flags = 0;  
    sigemptyset(&tact.sa_mask);  
    sigaction(SIGALRM, &tact, NULL);  
}
  

static void init_time()   
{  
    struct itimerval value;  
    value.it_value.tv_sec = PERIOD_TIMER;  
    value.it_value.tv_usec = 0;  
    value.it_interval = value.it_value;  
    setitimer(ITIMER_REAL, &value, NULL);  
}


static void *gettime_thread(void *arg)
{
	
	init_sigaction();
	init_time();
	for(;;)
	pause();	
        return NULL;
}


int main ()
{
	int ret;

	ret = open_timestamp_node();
	if(ret < 0)
	{
		printf("open timestamp node fail\n");
		exit(0);
	}

	pthread_t work_time_t;
	if(pthread_create(&work_time_t, NULL, gettime_thread, NULL))
	{
              printf("create work time thread error\n");
              goto create_thread_error;

	}

	pthread_join(work_time_t, NULL);

create_thread_error:
	pthread_cancel(work_time_t);
	pthread_join(work_time_t, NULL);

	return 0;
}
