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
 * Version 0.24 Build 2020.08.22
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
#define PEER_CMD                0xF8
#define VOICE_CMDX              0x18    //0x18 is VOICE_CMDX (B8 xor A0)
#define	IDLE_CMD		0xFC	//CC commands
#define	VOICE_CMD		0xEE	//

#define	MAX_LCN_NUM		32	//maximum number of Logical Channels
#define MAX_ALLOW_NUM		32
#define MAX_DENY_NUM		32

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

unsigned long long fr_6=0;						//40-bit shift registers for pushing decoded binary data
unsigned long long fr_1=0;						//each is a 40 bit message that repeats 3 times
unsigned long long fr_2=0;						//two messages per frame
unsigned long long fr_3=0;						//
unsigned long long fr_4=0;						//These are the human readable versions for debugging etc
unsigned long long fr_5=0;

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
unsigned long long int patch_site=0;
unsigned long long int site_id=0;
unsigned long long int site_id2=0;
signed long long int senderx=0;
signed long long int groupx=0;
signed long long int sourcep=0;
signed long long int targetp=0;  

signed int lshifter=0;                                                     //LCN bit shifter
unsigned int vcmd=0x00;                                                   //voice command variable set by argument
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
unsigned long long int Allow_list[MAX_ALLOW_NUM];
unsigned char allow_num=0;
unsigned int allow_total=0;
unsigned long long int Deny_list[MAX_DENY_NUM];
unsigned char deny_num=0;
unsigned int deny_total=0;
signed int deny_flag=0;
								//control channel LCN
signed int debug=0;                                           //debug value for printing out status and data on different command codes, etc
unsigned short d_choice=0;                                      //verbosity levels so users can see debug information etc                                   

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

void loadALLOW(char* filename)		//load LCN frequencies from file
{
	FILE *fl;
	char *line = NULL;
	size_t len = 0;
	
	char* path = NULL;

        asprintf(&path, "%s", filename); 
	
	fl = fopen(path, "r+");
	
	if (fl == NULL)
		printf("Error opening Allow file: %s", filename);
	
	allow_num=0;
	
	for(short int a=0; a<MAX_ALLOW_NUM-1; a++)
	{
		if (getline(&line, &len, fl) != -1)
			Allow_list[a]=atoi(line);
			if(Allow_list[a]!=0)
			{
				printf("Allow[%d]=%lld Group/Sender\n", a+1, Allow_list[a]);
				allow_num++;
			}
        allow_total = allow_num;
    }
}

void loadDENY(char* filename)		//load LCN frequencies from file
{
	FILE *fl;
	char *line = NULL;
	size_t len = 0;
	
	char* path = NULL;

        asprintf(&path, "%s", filename); 
	
	fl = fopen(path, "r+");
	
	if (fl == NULL)
		printf("Error opening Deny file: %s", filename);
	
	deny_num=0;
	
	for(short int d=0; d<MAX_DENY_NUM-1; d++)
	{
		if (getline(&line, &len, fl) != -1)
			Deny_list[d]=atoi(line);
			if(Deny_list[d]!=0)
			{
				printf("Deny[%d]=%lld Group/Sender \n", d+1, Deny_list[d]);
				deny_num++;
			}
        deny_total = deny_num;
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
	if(argc>3) //need to fix to prevent segmentation fault and send users to **ERROR** message when not enough arguments
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

		loadALLOW(argv[5]);	//load LCN freq list file
		if(allow_num>MAX_ALLOW_NUM)
		{
			printf("****************************ERROR*****************************\n");
			printf("Too many Allowed!\n");
			printf("**************************************************************\n");
			
			return 1;
		}
		loadDENY(argv[6]);	//load LCN freq list file
		if(deny_num>MAX_DENY_NUM)
		{
			printf("****************************ERROR*****************************\n");
			printf("Too many Denied!\n");
			printf("**************************************************************\n");
			
			return 1;
		}				
                printf("LEDACS-ESK v0.24 Build 2020.08.22\n");
		
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
                    lshifter=2;
                }
                if (x_choice==2)
                {
                    x_mask=0x0; //Use original in hope other EDACS variants will still work
                    vcmd=0xEE;
                    lshifter=0;
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
                printf("LEDACS-ESK v0.24 Build 2020.08.22 \n");
		printf("Not enough parameters!\n\n");
		printf("Usage: ./ledacs-esk input CC ESK DEBUG allow deny \n\n");
		printf("input - file with LCN frequency list\n");
		printf("         must be located in same directory as ledacs-esk\n");
		printf("cc    - control channel number in LCN frequency list\n");
		printf("ESK   - 1 - ESK enable; 2 - legacy EDACS no ESK\n");
		printf("DEBUG - 0 - off; 1-4 debug info verbosity levels\n");
		printf("        1 - Show Debug FR on IDLE and VOICE\n");
		printf("        2 - Show Debug FR on IDLE, VOICE; show PEERS\n");
		printf("        3 - Show Debug FR on all incoming messages\n");
		printf("        4 - Show Debug FR on all incoming messages; non Error Corrected\n");
                printf("allow   file with allowed group list\n");
                printf("deny    file with denied  group list\n\n");
                printf("Example - ./ledacs-esk site243 1 1 0 allow deny             \n\n");
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
			printf("Control Channel not found/lost. Timeout. Waiting...\n");
			sleep(1);
			fp = fopen("/tmp/squelch", "w");
			fputc('1', fp);
			squelchSet(5000);	//mute, may remove if condition of control channel lost during voice active
			fclose(fp);
			last_sync_time = time(NULL);
			//return 0; //Let's not close the program if CC lost, give it time to come back instead.
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
                        //put sr data in human readable/easier to work with fr 40 bit (10 hex) messages
                        fr_1 = ((sr_0 & 0xFFFF)<<24)|((sr_1 & 0xFFFFFF0000000000)>>40);
                        fr_2 = sr_1 & 0xFFFFFFFFFF;
                        fr_3 = (sr_2 & 0xFFFFFFFFFF000000)>>24;
                        fr_4 = ((sr_2 & 0xFFFFFF)<<16)|((sr_3 & 0xFFFF000000000000)>>48);
                        fr_5 = ((sr_3 & 0xFFFFFFFFFF00)>>8);
                        fr_6 = ((sr_3 & 0xFF)<<32)|((sr_4 & 0xFFFFFFFF00000000)>>32);
			if (fr_1 == fr_3 && fr_4 == fr_6) //error detection up top to trickle down
                        {
                            command = ((fr_1&0xFF00000000)>>32)^x_mask;
                            xstatus = (fr_1&0x1E00000)>>21;
                            lcn = (fr_1&0xF8000000)>>29;
			}
			if (debug>3) //This prints if nothing else is received and you need some numbers and debug 3 doesn't work, highest debug level
                        {
				printf("Non Error Corrected FR Message Blocks\n"); 
                                printf("Status=[0x%1X]\n", xstatus);
                                printf("FR_1=[%10llX]\n", fr_1);
                                printf("FR_2=[%10llX]\n", fr_2);
                                printf("FR_3=[%10llX]\n", fr_3);
                                printf("FR_4=[%10llX]\n", fr_4);
                                printf("FR_5=[%10llX]\n", fr_5);
                                printf("FR_6=[%10llX]\n", fr_6);
                        }
			//primitive error detection
			unsigned short int data, _data;
                        unsigned long long int sender; 

			//extract AFS - Leaving it in the way it was for legacy sake for now
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
			
			last_sync_time = time(NULL);	                                   //set timestamp
                        print_timeri = print_timeri - 1;                                  //primitive timer for printing out IDLE status updates
                        deny_flag = 0;                                                   //reset deny flag back to 0 for start of each loop
			if (command==IDLE_CMD && print_timeri<1 && voice_to==1)		//IDLE
			{
                                
                                site_id = ((sr_2&0xFFF00)>>10);
				printf("Time: %s  AFC=%d \tIDLE \tStatus=[0x%1X] \tSite ID=[%3lld]\n", getTime(), AFC, xstatus, site_id);
                                if (debug>0)
                                {
                                    printf("FR_1=[%10llX]\n", fr_1);
                                    printf("FR_2=[%10llX]\n", fr_2);
                                    printf("FR_3=[%10llX]\n", fr_3);
                                    printf("FR_4=[%10llX]\n", fr_4);
                                    printf("FR_5=[%10llX]\n", fr_5);
                                    printf("FR_6=[%10llX]\n", fr_6);
                                }
                                print_timeri = 150;
			}
			if (command==PEER_CMD && fr_1==fr_4 && ((fr_1&0xFF000)>>12)>0 && debug>1)		        //PEER LISTING
			{
                            printf("PEER=[%3lld]\n", (fr_1&0xFF000)>>12);
			}
			if (debug>2) //This prints if nothing else is received and you need some numbers
                        {
				printf("command=[%2X]\n", command); //print original command bits
                                printf("Status=[0x%1X]\n", xstatus);
                                printf("FR_1=[%10llX]\n", fr_1);
                                printf("FR_2=[%10llX]\n", fr_2);
                                printf("FR_3=[%10llX]\n", fr_3);
                                printf("FR_4=[%10llX]\n", fr_4);
                                printf("FR_5=[%10llX]\n", fr_5);
                                printf("FR_6=[%10llX]\n", fr_6);
                                

			}
                        else if (command==vcmd && lcn>0 && lcn!=cc)
			{
                                if (fr_1 == fr_3 && fr_4 == fr_6) //error detection for groupx, senderx, xstatus variables, probably redundant
                                {
                                    groupx = (fr_1&0x7FFF000)>>12;
                                    senderx = (fr_4&0xFFFFF000)>>12;
                                    xstatus = (fr_1&0x1E00000)>>21; //may be redundant as well here and up top
                                }

				if (lcn==current_lcn) //lcn==current_lcn
				{
					last_voice_time = time(NULL); 
					voice_to=0;
				}
            
				else         
                                {
                                    
                                    printf("Time: %s  AFC=%d\tVOICE\tStatus=[0x%1X] \tLCN=%d \n", getTime(), AFC, xstatus, lcn);
                                    if (x_choice==1)
                                    { 
                                        printf("Sender=[%7lldi]\n", senderx);
                                        printf("Group=[%6lldg]\n", groupx);
                                    }
                                    if (x_choice==2)
                                    {
                                        printf("AFS=[%3llX]\n", afs);
                                        printf("agency=[%1X]\n", agency);
                                        printf("fleet=[%1X]\n", fleet);
                                        printf("subfleet=[%1X]\n", subfleet);
                                    }                                    
                                    if (debug>0)
                                    {
                                        printf("FR_1=[%10llX]\n", fr_1);
                                        printf("FR_2=[%10llX]\n", fr_2);
                                        printf("FR_3=[%10llX]\n", fr_3);
                                        printf("FR_4=[%10llX]\n", fr_4);
                                        printf("FR_5=[%10llX]\n", fr_5);
                                        printf("FR_6=[%10llX]\n", fr_6);

                                    }

                                    for(short int l=0; l<deny_total; l++)
                                    {
                                        if (groupx == Deny_list[l])

                                        {
                                            printf("Group[%lld]g is DENIED!\n", Deny_list[l]);
                                            //squelchSet(5000); //remove if group denial is fully functional                                            
                                            deny_flag=1;
                                            if (lcn==current_lcn) //adding this so that if we accidentally tune a denied group, we can cut it off early
                                            {
                                                squelchSet(5000);
                                            }
                                        }
 
                                    }

                                    for(short int o=0; o<deny_total; o++)
                                    {
                                        if (senderx == Deny_list[o])

                                        {
                                            printf("Sender[%lld]i is DENIED!\n", Deny_list[o]);
                                            //squelchSet(5000); //remove if sender denial is fully functional
                                            deny_flag=1;
                                            if (lcn==current_lcn) //adding this so that if we accidentally tune a denied sender, we can cut it off early
                                            {
                                                squelchSet(5000);
                                            }
                                        }
 
                                    }
                                }
				
                                
		                if(allow_num==0 && deny_num==0) // if there are neither allow or deny, then allow all calls
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

		                if(allow_num==0 && deny_flag != 1) // if nothing in allow file and if deny flag is not tripped, open voice call
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

                                for(short int j=0; j<allow_total; j++)
                                {
                                    if(allow_num>0 && groupx == Allow_list[j] || allow_num>0 && senderx == Allow_list[j]) //now allows for groups and senders
                                       
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
                                    

			}
                        else
			{
			    //printf("LEDACS-ESK v0.24 Build 2020.08.22 \n");	
                                
            		}
		}
	}

	return 0;
}
