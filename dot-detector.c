/*-------------------------------------------------------------------------------
 * 								Dotting sequence detector
 * 
 * 
 * 
 * 
 * 
 * XTAL Labs
 * 30 IV 2016
 * LWVMOBILE - Tweaks
 * 2020-08
 *-----------------------------------------------------------------------------*/
#define _DEFAULT_SOURCE  //_BSD_SOURCE
#include <stdio.h>
#include <unistd.h>
#include <limits.h>

#include <string.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <errno.h>

#define UDP_BUFLEN		5				//maximum UDP buffer length
#define SRV_IP 			"127.0.0.1"		//IP
#define UDP_PORT 		6020			//UDP port

#define	SAMP_NUM		1000*3*2
#define	DOTTING			0xAAAAAA		//C71C71
#define	D_MASK			0xFFFFFF

unsigned char samples[SAMP_NUM];				//8-bit samples from rtl_fm (or rtl_udp)
signed short int raw_stream[SAMP_NUM/2];		//16-bit signed int samples

signed int AFC=0;								//Auto Frequency Control -> DC offset
signed int min=SHRT_MAX, max=SHRT_MIN;			//min and max sample values
unsigned int avg_cnt=0;							//avg array index variable
signed short int avg_arr[SAMP_NUM/2/3];

unsigned long long sr=0;						//shift register for pushing decoded binary data

int handle;						//for UDP
unsigned short port = UDP_PORT;	//
char data[UDP_BUFLEN]={0};		//
struct sockaddr_in address;		//

//--------------------------------------------
int init_udp()		//UDP init
{
	handle = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

	if (handle <= 0)
	{
			printf("Failed to create socket\n");
			return 1;
	}

	printf("Sockets successfully initialized\n");

	memset((char *) &address, 0, sizeof(address));
	
	address.sin_family = AF_INET;
	address.sin_addr.s_addr = inet_addr(SRV_IP); //address of host
	address.sin_port = htons(port);

	return 0;
}

//--------------------------------
void squelchSet(unsigned long long int sq)		//squelch
{
	data[0]=2;
	data[1]=sq&0xFF;
	data[2]=(sq>>8)&0xFF;
	data[3]=(sq>>16)&0xFF;
	data[4]=(sq>>24)&0xFF;
	
	sendto(handle, data, UDP_BUFLEN, 0, (const struct sockaddr*) &address, sizeof(struct sockaddr_in));
}

int main(void)
{
	signed int avg=0;		//sample average
	FILE *fp; int fread;
	
	init_udp();
	
	sleep(2);
	fp = fopen("/tmp/squelch", "w");	//squelch init
	fputc('1', fp);
	fclose(fp);
	
	//AFC=getAFC(SAMP_NUM);
	
	for(int i=0; i<SAMP_NUM/2/3-1; i++)	//zero array
	{
		avg_arr[i]=0;
	}
	
	while(1)
	{		
		read(0, samples, 3*2);		//read samples
		raw_stream[0]=(signed short int)((samples[0+1]<<8)|(samples[0]&0xFF));
		raw_stream[1]=(signed short int)((samples[2+1]<<8)|(samples[2]&0xFF));
		raw_stream[2]=(signed short int)((samples[4+1]<<8)|(samples[4]&0xFF));
		avg=(raw_stream[0]+raw_stream[1]+raw_stream[2])/3;
		
		//AFC recomputing using averaged samples
		avg_arr[avg_cnt]=avg;
		avg_cnt++;
		if (avg_cnt>=SAMP_NUM/2/3-1)	//reset after filling avg_array
		{
			avg_cnt=0;
			min=SHRT_MAX;
			max=SHRT_MIN;
			
			for(int i=0; i<SAMP_NUM/2/3-1; i++)	//simple min/max detector
			{
				if (avg_arr[i]>max)
					max=avg_arr[i];
				if (avg_arr[i]<min)
					min=avg_arr[i];
			}
		AFC=(min+max)/2;
		//if(AFC!=0)printf("AFC=%d\n",AFC);
		}
		//--------------------------------------
		
		sr=sr<<1;

		if (avg<AFC)
			sr|=1;
	
		if ((sr&0xFFFFFF)==0xC71C71 || (sr&0xFFFFFFFFFFFFFFFF)==0xAAAAAAAAAAAAAAAA || (sr&0xFFFFFFFFFFFFFFFF)==0x5555555555555555)
		{
			fp = fopen("/tmp/squelch", "r+");
			fread=fgetc(fp);
			
			if(fread=='0')
			{
				squelchSet(5000);
				usleep(200*1000);
				fp = freopen("/tmp/squelch", "w", fp);
				fputc('1', fp);
				
				printf("AFC=%d\n", AFC);
			}
			
			fclose(fp);
		}
		
		else /* ((sr&0xFFFFFF)==0xC71C71 || (sr&0xFFFFFF)==0xAAAAAA || (sr&0xFFFFFF)==0x555555){ */
		{
                    if (sr != 0x0000000000000000)
                    {
                	//printf("%016llX\n", sr);
                    }
		}
	}

	return 0;
}
