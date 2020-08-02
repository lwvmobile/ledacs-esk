/*-------------------------------------------------------------------------------
 * 							EDACS96 ESK rtl_fm decoder
 * 
 * 
 * rtl_fm sample rate 28.8kHz (3*9600baud - 3 samples per symbol)
 * 
 * 
 * XTAL Labs
 * 3 V 2016
 * LWVMOBILE - ESK VERSION
 * 2020-08 Current Version 0.1a
 *-----------------------------------------------------------------------------*/
#define _GNU_SOURCE
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
#include <time.h>

#define UDP_BUFLEN		5				//maximum UDP buffer length
#define SRV_IP 			"127.0.0.1"		        //IP
#define UDP_PORT 		6020			        //UDP port

#define	SAMP_NUM		(48+6*40) *2*3			//EDACS96 288-bit cycle
#define	SYNC_FRAME		0x555557125555<<(64-48)		//EDACS96 synchronization frame (12*4=48bit)
#define	SYNC_MASK		0xFFFFFFFFFFFF<<(64-48)		//EDACS96 synchronization frame mask

#define CMD_SHIFT		8				//data location in sr_0
#define CMD_MASK		0xFF<<CMD_SHIFT			//DON'T TOUCH

#define LCN_SHIFT		3				//for other SR-s these values are hardcoded below
#define LCN_MASK		0xF8
#define STATUS_MASK		0x07

#define	SYNC_TIMEOUT	3		//maximum time between sync frames in seconds
#define VOICE_TIMEOUT	1		//maximum time of unsquelched audio after last voice channel assignment command in seconds
//New CC Commands added
#define DATA_CMD                0xFA    //0xA0    
#define DATA_CMDX               0xFB    //0xA1
#define ID_CMD                  0xFD    //0xFD
#define PATCH_CMD               0xFE    //0xEC
#define VOICE_CMDX              0x18    //0x18 is VOICE_CMDX
#define	IDLE_CMD		0xFC	//CC commands
#define	VOICE_CMD		0xEE	//

#define	MAX_LCN_NUM		32	//maximum number of Logical Channels

unsigned char samples[SAMP_NUM];				        //8-bit samples from rtl_fm (or rtl_udp)
signed short int raw_stream[SAMP_NUM/2];		                //16-bit signed int samples

signed int AFC=0;							//Auto Frequency Control -> DC offset
signed int min=SHRT_MAX, max=SHRT_MIN;			                //min and max sample values
signed short int avg_arr[SAMP_NUM/2/3];			                //array containing 16-bit samples
unsigned int avg_cnt=0;							//avg array index variable

unsigned long long sr_0=0;						//64-bit shift registers for pushing decoded binary data
unsigned long long sr_1=0;						//288/64=4.5
unsigned long long sr_2=0;						//
unsigned long long sr_3=0;						//
unsigned long long sr_4=0;						//

unsigned short a_len=4;							//AFS allocation type (see readme.txt)
unsigned short f_len=4;							//bit lengths
unsigned short s_len=3;							//
unsigned short a_mask=0x780;					        //and corresponding masks
unsigned short f_mask=0x078;					       //
unsigned short s_mask=0x007;

unsigned short x_mask=0xA0;
unsigned short x_choice=1;
									//
unsigned long long afs=0;						//AFS 11-bit info

unsigned int vcmd=0x00;
unsigned char xcommand=0;                                                //XOR of command variable
unsigned char command=0;						//read from control channel
unsigned char xlcn=0;
unsigned char lcn=0;
unsigned char xstatus=0;						//
unsigned char status=0;							//
unsigned char agency=0, fleet=0, subfleet=0;	//
unsigned char CRC=0;							//

unsigned long long int last_sync_time=0;		         //last received sync timestamp
unsigned long long int last_voice_time=0;		         //last received voice channel assignment timestamp

unsigned char current_lcn=0;					//current LCN
unsigned long long int LCN_list[MAX_LCN_NUM];	//LCN list
unsigned char lcn_num=0;						//number of logical channels
unsigned char cc=1;
								//control channel LCN
unsigned int debug=0;                                           //debug value for printing out status and data on different command codes, etc
unsigned short d_choice=0;                                      //verbosity levels so users can see debug information etc                                   
unsigned int iddebug=0;
unsigned int datadebug=0;
unsigned int patchdebug=0;
unsigned short int print_timer=1;                                //print timer for not printing nonstop
unsigned short int print_timerb=1;
signed short int print_timeri=25;

int handle;						         //for UDP
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

char* getTime(void)		//get pretty hh:mm:ss timestamp
{
	time_t t = time(NULL);

	char* curr;
	char* stamp = asctime(localtime(&t));

	curr = strtok(stamp, " ");
	curr = strtok(NULL, " ");
	curr = strtok(NULL, " ");
	curr = strtok(NULL, " ");

	return curr;
}

//--------------------------------------------
void tune(unsigned long long int freq)		//tuning to freq
{
	data[0]=0;
	data[1]=freq&0xFF;
	data[2]=(freq>>8)&0xFF;
	data[3]=(freq>>16)&0xFF;
	data[4]=(freq>>24)&0xFF;
	
	sendto(handle, data, UDP_BUFLEN, 0, (const struct sockaddr*) &address, sizeof(struct sockaddr_in));
}

void squelchSet(unsigned long long int sq)		//squelch
{
	data[0]=2;
	data[1]=sq&0xFF;
	data[2]=(sq>>8)&0xFF;
	data[3]=(sq>>16)&0xFF;
	data[4]=(sq>>24)&0xFF;
	
	sendto(handle, data, UDP_BUFLEN, 0, (const struct sockaddr*) &address, sizeof(struct sockaddr_in));
}

void loadLCN(char* filename)		//load LCN frequencies from file
{
	FILE *fl;
	char *line = NULL;
	size_t len = 0;
	
	char* path = NULL;

        asprintf(&path, "%s", filename); 
	
	fl = fopen(path, "r+");
	
	if (fl == NULL)
		printf("Error opening LCN file: %s", filename);
	
	lcn_num=0;
	
	for(short int i=0; i<MAX_LCN_NUM-1; i++)
	{
		if (getline(&line, &len, fl) != -1)
			LCN_list[i]=atoi(line);
			if(LCN_list[i]!=0)
			{
				printf("LCN[%d]=%lldHz\n", i+1, LCN_list[i]);
				lcn_num++;
			}
    }
}

//--------------------------------------------MAIN--------------------------------------
int main(int argc, char **argv)
{
	signed int avg=0;		//sample average
	
	FILE *fp; int fread;
	
	init_udp();
	sleep(1);			//patience is a virtue	
	
	//load arguments-----------------------------------
	if(argc>4) //original is -- if(argc>4) 
	{
		loadLCN(argv[1]);	//load LCN freq list file
		if(lcn_num>MAX_LCN_NUM)
		{
			printf("****************************ERROR*****************************\n");
			printf("Too many LCNs!\n");
			printf("**************************************************************\n");
			
			return 1;
		}
		
		cc=strtol(argv[2], NULL, 10);
		printf("CC=LCN[%d]\n", cc);
		
		//load AFS allocation info
		//a_len=strtol(argv[4], NULL, 10);  //changed to optional arguments, may need to be used for normal EDACS/NET without ESK //Segmentation Fault if no value entered           
		//f_len=strtol(argv[4], NULL, 10);  //changed to optional arguments, may need to be used for normal EDACS/NET without ESK //Segmentation Fault if no value entered 
                a_len=4;   //just setting these variable to arbitrary values doesnt matter grabbing SITE/GROUP/SENDER was changed; default to 4/4/3 AFS
                f_len=4;  //just setting these variable to arbitrary values doesnt matter grabbing SITE/GROUP/SENDER was changed; default to 4/4/3 AFS
		s_len=11-(a_len+f_len); //going to leave these here though in case removing them affects code later on, will clean up eventually, or re-use

                //load choice for EDACS system, 1 for ESK; 2 for Others(no ^A0 for command) 

                x_choice = strtol(argv[3], NULL, 10);
                if (x_choice==1)
                {
                    x_mask=0xA0; //XOR for ESK
                    vcmd=0x18;
                }
                if (x_choice==2)
                {
                    x_mask=0x0; //Use original in hope other EDACS variants will still work
                    vcmd=0xEE;
                }

                d_choice = strtol(argv[4], NULL, 10);
                if (d_choice>0)
                {
                    debug=d_choice;
                }			


		a_mask=0; f_mask=0; s_mask=0;
		
		for(unsigned short int i=0; i<a_len; i++)	//A
		{
			a_mask=a_mask<<1;
			a_mask|=1;
		}
		a_mask=a_mask<<(11-a_len);
		
		for(unsigned short int i=0; i<f_len; i++)	//F
		{
			f_mask=f_mask<<1;
			f_mask|=1;
		}
		f_mask=f_mask<<s_len;
		
		for(unsigned short int i=0; i<s_len; i++)	//S
		{
			s_mask=s_mask<<1;
			s_mask|=1;
		}
		
	} else {
		printf("****************************ERROR*****************************\n");
		printf("Not enough parameters!\n\n");
		printf("Usage: ./ledacs-esk input CC ESK DEBUG \n\n");
		printf("input - file with LCN frequency list\n");
		printf("         must be located in same directory as ledacs-esk\n");
		printf("cc    - control channel number in LCN frequency list\n");
		printf("ESK   - 1 - ESK enable; 2 - legacy EDACS no ESK\n");
		printf("DEBUG - 0 - off; 1-4 debug info verbosity levels\n");
		printf("        Needs More information on Debug levels\n\n");
                printf("Example - ./ledacs site243 1 1 0               \n\n");
		printf("Exiting.\n");
		printf("**************************************************************\n");
		
		return 1;
	}
	//-------------------------------------------------
	
	last_sync_time = time(NULL);
	last_voice_time = time(NULL);
	unsigned char voice_to=0;		//0 - no timeout, 1 - last voice channel assignment more than VOICE_TIMEOUT seconds ago
	
	for(int i=0; i<SAMP_NUM/2/3-1; i++)	//zero array
	{
		avg_arr[i]=0;
	}
	
	//let's get the party started
	while(1)
	{
		if ((time(NULL)-last_sync_time)>SYNC_TIMEOUT)	//check if the CC is still there
		{
			printf("CC lost. Timeout %llds. Exiting.\n", time(NULL)-last_sync_time);
			
			fp = fopen("/tmp/squelch", "w");
			fputc('1', fp);
			squelchSet(5000);	//mute
			fclose(fp);
			
			return 0;
		}
		
		
		if ((time(NULL)-last_voice_time)>VOICE_TIMEOUT)	//mute if theres no more voice channel assignments
		{
			//fp = fopen("/tmp/squelch", "r+");
			//fread=fgetc(fp);
			
			if (voice_to==0)
			{
				squelchSet(5000);	//mute
				//usleep(500*1000);
				fp = fopen("/tmp/squelch", "w");
				fputc('1', fp);
				
				fclose(fp);
				voice_to=1;
			}
		} else
			voice_to=0;
		
		read(0, samples, 3*2);	//read 3 samples (6 unsigned chars)
		raw_stream[0]=(signed short int)((samples[0+1]<<8)|(samples[0]&0xFF));
		raw_stream[1]=(signed short int)((samples[2+1]<<8)|(samples[2]&0xFF));
		raw_stream[2]=(signed short int)((samples[4+1]<<8)|(samples[4]&0xFF));
		avg=(raw_stream[0]+raw_stream[1]+raw_stream[2])/3;	//
		
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
		}
		//--------------------------------------
		
		//pushing data into shift registers
		sr_0=(sr_0<<1)|(sr_1>>63); 
		sr_1=(sr_1<<1)|(sr_2>>63);
		sr_2=(sr_2<<1)|(sr_3>>63);
		sr_3=(sr_3<<1)|(sr_4>>63);
		sr_4=sr_4<<1;

		if (avg<AFC)
			sr_4|=1;
		//---------------------------------
		
		if ((sr_0&SYNC_MASK)==SYNC_FRAME)	//extract data after receiving the sync frame
		{
			//primitive error detection
			unsigned short int data, _data;
                        unsigned long long int sender; 
			//The result of AND is 1 only if both bits are 1. //The result of OR is 1 if any of the two bits is 1. 
			//extract CMD
			data=((sr_0&CMD_MASK)>>CMD_SHIFT)&0xFF;       //performs AND operation on sr_0 and CMD_MASK<<8 or (0xFF00), then right shifts those bits by CMD_SHIFT (8). Then performs AND on that value with 0xFF
                                                                      //CMD_MASK = 0xFF<<CMD_SHIFT. 
			_data=(~((sr_1&(0xFF00000000))>>32))&0xFF;
			if ( data == _data )
                                
                                command=data^x_mask;           // <--going to go this route so perhaps other variants of ESK will work, defaults to ESK


			//extract LCN
			data=((sr_0&LCN_MASK)>>LCN_SHIFT)&0x1F;
			_data=(~((sr_1&(0xF8000000))>>27))&0x1F;
			if ( data == _data )
				//lcn=data;
                                lcn=data>>2; //Needed to shift an extra 2 to the right, 5 total instead of 3. Need to make sure this is true of all ESK, or if it varies.
                                
			
			//extract STATUS
			data=(((sr_0&STATUS_MASK)<<1)|(sr_1>>63))&0x0F; //STATUS_MASK = 0x07
			_data=(~((sr_1&(0x3800000))>>23))&0x0F;
			if ( data == _data )
				status=data;

			
			//extract AFS
			data=((sr_1&(0x7FF0000000000000))>>52)&0x7FF;
			_data=(~((sr_1&(0x7FF000))>>12))&0x7FF;




                       
			if ( _data == _data )
				afs=data;
                                

                        a_mask=0x780;					
                        f_mask=0x078;					
                        s_mask=0x007;

			agency=(afs&a_mask)>>(11-a_len);
			fleet=(afs&f_mask)>>s_len;
			subfleet=(afs&s_mask);
			//-------------------------------------------------
			
			last_sync_time = time(NULL);	                                  //set timestamp
                        print_timeri = print_timeri - 1;                                 //primitive timer for printing out IDLE status updates
			if (command==IDLE_CMD && debug==0 && print_timeri<1)		//IDLE, if tuning or tracking seems sluggish, may need to remove && print_timer condition
			{
				printf("Time: %s  AFC=%d \tIDLE \tStatus=[0x%1X] \tSite ID=[%3lld]\n", getTime(), AFC, status, ((sr_2&0xFFF00)>>10)); 
                                print_timeri = 150;
                       
 
                                
			}
			
			if (command==IDLE_CMD && debug>1)		//IDLE
			{

                                printf("Time: %s  \tAFC=%d \tIDLE \tStatus=[0x%1X]\n", getTime(), AFC, status);
                                printf("SR_0=[%16llX]\n", sr_0);
                                printf("SR_1=[%16llX]\n", sr_1);
                                printf("SR_2=[%16llX]\n", sr_2);
                                printf("SR_3=[%16llX]\n", sr_3);
                                printf("SR_4=[%16llX]\n", sr_4);
                                
                                
			}
                        if (command==ID_CMD && debug>1)  //ID Command
                        {
                                printf("Receiving ID Command. command=[%2X]\n", command);
                                printf("Status=[0x%1X]\n", status);
                                printf("SR_0=[%16llX]\n", sr_0);
                                printf("SR_1=[%16llX]\n", sr_1);
                                printf("SR_2=[%16llX]\n", sr_2);
                                printf("SR_3=[%16llX]\n", sr_3);
                                printf("SR_4=[%16llX]\n", sr_4);


                        }
                        if (command==DATA_CMD && debug>1)         //Data Command
                        {
                                printf("Receiving Data Command. command=[%2X]\n", command);
                                printf("Status=[0x%1X]\n", status);
                                printf("SR_0=[%16llX]\n", sr_0);
                                printf("SR_1=[%16llX]\n", sr_1);
                                printf("SR_2=[%16llX]\n", sr_2);
                                printf("SR_3=[%16llX]\n", sr_3);
                                printf("SR_4=[%16llX]\n", sr_4);
                                

                        }
                        if (command==DATA_CMDX && debug>1)         //Data Command X
                        {
                                printf("Receiving DataX Command. command=[%2X]\n", command);
                                printf("Status=[0x%1X]\n", status);
                                printf("SR_0=[%16llX]\n", sr_0);
                                printf("SR_1=[%16llX]\n", sr_1);
                                printf("SR_2=[%16llX]\n", sr_2);
                                printf("SR_3=[%16llX]\n", sr_3);
                                printf("SR_4=[%16llX]\n", sr_4);
                                

                        }
                        if (command==PATCH_CMD && debug>0)         //Patch Command
                        {

                                printf("Patch Source[%5lldg] Target[%5lldg]\n", ((sr_4&0xFFF00000000000)>>44), ((sr_4&0xFFF00000000)>>34)); //Working, but shows A LOT MORE than current site on Uni, may also show Peer Patches?? 

                                

                        }
                        if (command==PATCH_CMD && debug>2)         //Patch Command with SR info
                        {

                                printf("Patch Source[%5lldg] Target[%5lldg]\n", ((sr_4&0xFFF00000000000)>>44), ((sr_4&0xFFF00000000)>>34)); //Working, but shows A LOT MORE than current site on Uni, may also show Peer Patches?? 
                                printf("SR_0=[%16llX]\n", sr_0);
                                printf("SR_1=[%16llX]\n", sr_1);
                                printf("SR_2=[%16llX]\n", sr_2);
                                printf("SR_3=[%16llX]\n", sr_3);
                                printf("SR_4=[%16llX]\n", sr_4);
                                

                        }
			if (debug>3) //This prints if nothing else is received and you need some numbers, highest debug level
                        {
				printf("command=[%2X]\n", command); //print original command bits
                                printf("Status=[0x%1X]\n", status);
                                printf("SR_0=[%16llX]\n", sr_0);
                                printf("SR_1=[%16llX]\n", sr_1);
                                printf("SR_2=[%16llX]\n", sr_2);
                                printf("SR_3=[%16llX]\n", sr_3);
                                printf("SR_4=[%16llX]\n", sr_4);
                                

			}

                        
                        else if (command==vcmd && lcn>0 && lcn!=cc)
			{
                                print_timer = print_timer-1;  //very experimental and primitive print timeout
                                print_timerb= print_timerb-1;
				if (lcn==current_lcn) //lcn==current_lcn
				{
					last_voice_time = time(NULL); 
					voice_to=0;
				}
				
				if ((status%2)==1 && print_timer==0) //remove print_timer condition if not satisfatory output
                                {         

                                    printf("Time: %s  AFC=%d\tVOICE\tStatus=[0x%1X] \tLCN=%d\n", getTime(), AFC, status, lcn);
                                    printf("Sender=[%7lldi]\n", (sr_4&0xFFFFF00000000000)>>44);
                                    printf("Group=[%6lldg]\n", ((sr_0&0x000000000000000F)<<12)|(sr_1&0xFFF0000000000000)>>52);
                                    print_timer = 5;
                                    if (debug>1)
                                    {
                                            printf("SR_0=[%16llX]\n", sr_0);
                                            printf("SR_1=[%16llX]\n", sr_1);
                                            printf("SR_2=[%16llX]\n", sr_2);
                                            printf("SR_3=[%16llX]\n", sr_3);
                                            printf("SR_4=[%16llX]\n", sr_4);
                                            printf("Data=[%16X]\n", data);
                                            printf("AFS=[%16llX]\n", afs);
                                            printf("agency=[%4X]\n", agency);
                                            printf("fleet=[%4X]\n", fleet);
                                            printf("subfleet=[%4X]\n", subfleet);
                                    }

                                }                                                    
				//else             //change back to this if PVT no longer prints or tunes too slowly/not at all
                                if (print_timerb==0)
                                {
                                    
                                    printf("Time: %s  AFC=%d\tVOICE\tStatus=[0x%1X] \tLCN=%d\tPVT \n", getTime(), AFC, status, lcn);
                                    printf("Sender=[%7lldi]\n", (sr_4&0xFFFFF00000000000)>>44);
                                    printf("Group=[%6lldg]\n", ((sr_0&0x000000000000000F)<<12)|(sr_1&0xFFF0000000000000)>>52);
                                    print_timerb = 5;
                                    if (debug>1)
                                    {
                                            printf("SR_0=[%16llX]\n", sr_0);
                                            printf("SR_1=[%16llX]\n", sr_1);
                                            printf("SR_2=[%16llX]\n", sr_2);
                                            printf("SR_3=[%16llX]\n", sr_3);
                                            printf("SR_4=[%16llX]\n", sr_4);
                                            printf("Data=[%16X]\n", data);
                                            printf("AFS=[%16llX]\n", afs);
                                            printf("agency=[%4X]\n", agency);
                                            printf("fleet=[%4X]\n", fleet);
                                            printf("subfleet=[%4X]\n", subfleet);
                                    }
                                    
                                }
				
				if(argc<=6 || (argc>7 && agency==strtol(argv[6], NULL, 10) &&  fleet==strtol(argv[7], NULL, 10) && subfleet==strtol(argv[8], NULL, 10)))
				{
					fp = fopen("/tmp/squelch", "r+");
					fread=fgetc(fp);
					
					if(fread=='1')	//if we are currently NOT listening to anything else
					{
						if (lcn!=current_lcn)		//tune to LCN
						{
							tune(LCN_list[lcn-1]);
							current_lcn=lcn;
						}
					
						fp = freopen("/tmp/squelch", "w", fp);
						fputc('0', fp);
						squelchSet(0);	//unmute

					}
			
					fclose(fp);
				}
			}
                        
			else
			{
			    //printf("LEDACS-ESK version 20202-08 LWVMOBILE. Current Version: 0.1a \n");	
                                
            		}
		}
	}

	return 0;
}
