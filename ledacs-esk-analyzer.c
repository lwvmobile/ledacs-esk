/*-------------------------------------------------------------------------------
 * 							EDACS96 ESK rtl_fm decoder
 * 
 * 
 * rtl_fm sample rate 28.8kHz (3*9600baud - 3 samples per symbol)
 * 
 * 
 * XTAL Labs
 * 3 V 2016
 * LWVMOBILE - ESK EA ANALYZER VERSION
 * Version 0.27 Build 2020.09.07
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
signed long long int senderx=0;
signed long long int groupx=0;
signed long long int sourcep=0;
signed long long int targetp=0;  

unsigned char mt1=0;
unsigned char mt2=0;
unsigned char mta=0;
unsigned char mtb=0;
unsigned char mtd1=0;
unsigned char mtd2=0;

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


//--------------------------------------------MAIN--------------------------------------
int main(int argc, char **argv)
{
	signed int avg=0;		//sample average
	
	FILE *fp; int fread;
	

	sleep(1);			//patience is a virtue	
	
	//load arguments-----------------------------------
	if(argc>1) //need to fix to prevent segmentation fault and send users to **ERROR** message when not enough arguments
	{
			
                printf("LEDACS-ESK-ANALYZER v0.27 Build 2020.09.07\n");
		
		//load AFS allocation info
		//a_len=strtol(argv[4], NULL, 10);  //changed to optional arguments, may need to be used for normal EDACS/NET without ESK //Segmentation Fault if no value entered           
		//f_len=strtol(argv[4], NULL, 10);  //changed to optional arguments, may need to be used for normal EDACS/NET without ESK //Segmentation Fault if no value entered 
                a_len=4;   //just setting these variable to arbitrary values doesnt matter grabbing SITE/GROUP/SENDER was changed; default to 4/4/3 AFS
                f_len=4;  //just setting these variable to arbitrary values doesnt matter grabbing SITE/GROUP/SENDER was changed; default to 4/4/3 AFS
		s_len=11-(a_len+f_len); //going to leave these here though in case removing them affects code later on, will clean up eventually, or re-use

                //load choice for EDACS system, 1 for ESK; 2 for Others(no ^A0 for command) 

                x_choice = strtol(argv[1], NULL, 10);
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

                d_choice = strtol(argv[2], NULL, 10);
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
                printf("LEDACS-ESK-ANALYZER v0.27 Build 2020.09.07 \n");
		printf("Not enough parameters!\n\n");
		printf("Usage: ./ledacs-esk-analyzer ESK DEBUG \n\n");
		printf("input - file with LCN frequency list\n");
		printf("         must be located in same directory as ledacs-esk\n");
		printf("\n");
		printf("ESK   - 1 - ESK enable; 2 - legacy EDACS no ESK\n");
		printf("DEBUG - 0 - off; 1-4 debug info verbosity levels\n");
		printf("        1 - Show Debug FR on IDLE and VOICE\n");
		printf("        2 - Show Debug FR on IDLE, VOICE; show PEERS, ADDS, KICKS\n");
		printf("        3 - Show Debug FR on all incoming messages\n");
		printf("        4 - Show Debug FR on all incoming messages; non Error Corrected\n");
                printf("                                                 ");
                printf("\n\n");
                printf("Example - ./ledacs-esk-analyzer 1 0            \n\n");
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
			last_sync_time = time(NULL);
			//return 0; //Let's not close the program if CC lost, give it time to come back instead.
		}

		
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
                            xstatus = (fr_1&0x7C00000)>>22;
                            //lcn = (fr_1&0xF8000000)>>(27+lshifter); //OOPS, worked by fluke, had to fix, would cause problems with higher LCN numbers
                            //lcn = (fr_1&0x7E0000000)>>(27+lshifter); //OOPS again, was grabbing 6 bits, not 5;
                            lcn = (fr_1&0x3E0000000)>>(27+lshifter);
                            mt1 = command>>3;
                            mt2 = (fr_1&0x780000000)>>31;
                            mta = (fr_4&0xF000000000)>>36;
                            mtb = (fr_4&0xE00000000)>>33;
                            mtd1 = (fr_4&0x1F0000000)>>28;
                            mtd2 = (fr_1&0x1F0000000)>>28;
			    if ((fr_1 & 0xFFF0000000) == 0x5D00000000)	//Site ID, moved into error detected portion, don't know why I didn't have it here before
			    {
                                site_id = ((fr_1 & 0x1F000) >> 12) | ((fr_1 & 0x1F000000)>>19);
			    }
			}
			if (debug>3) //This prints if nothing else is received and you need some numbers and debug 3 doesn't work, highest debug level
                        {
				printf("Non Error Corrected FR Message Blocks\n"); 
                                printf("MT-1=[0x%2X]\n", xstatus);
                                printf("MT-1 Binary = ");
                                for(unsigned short int i=0; i<5; i++)
                                {	
                                    signed short int buffer = (xstatus>>4-i)&0x1;
                                    printf("[%1d] ", buffer);
                                }
                                printf("\n");
                                printf("MT-2=[0x%2llX]\n", (fr_1&0x780000000)>>31);
                                printf("MT-2 Binary = ");
                                for(unsigned short int i=0; i<4; i++)
                                {	
                                    signed short int buffer = (mt2>>3-i)&0x1;
                                    printf("[%1d] ", buffer);
                                }
                                printf("\n");
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
			if (command==ID_CMD && print_timeri<1)                 	        //IDLE
			{
                                
				printf("Time: %s  AFC=%d \tIDLE \tMT-1=[0x%2X] \tMT-2=[0x%1X] \tSite ID=[%3lld]\n", getTime(), AFC, mt1, mt2, site_id);
                                if (debug>0)
                                {
                                    printf("MT-1 Binary = ");
                                    for(unsigned short int i=0; i<5; i++)
                                    {	
                                        signed short int buffer = (mt1>>4-i)&0x1;
                                        printf("[%1d] ", buffer);
                                    }
                                    printf("\n");
                                    printf("MT-2 Binary = ");
                                    for(unsigned short int i=0; i<4; i++)
                                    {	
                                        signed short int buffer = (mt2>>3-i)&0x1;
                                        printf("[%1d] ", buffer);
                                    }
                                    printf("\n");
                                    printf("FR_1=[%10llX]\n", fr_1);
                                    printf("FR_2=[%10llX]\n", fr_2);
                                    printf("FR_3=[%10llX]\n", fr_3);
                                    printf("FR_4=[%10llX]\n", fr_4);
                                    printf("FR_5=[%10llX]\n", fr_5);
                                    printf("FR_6=[%10llX]\n", fr_6);
                                }
                                print_timeri = 150;
			}
			//if (command==PEER_CMD && fr_1==fr_4 && ((fr_1&0xFF000)>>12)>0 && debug>1)		        //PEER LISTING
                        if ( (fr_1&0xFFF0000000)==0x5880000000 && ((fr_1&0xFF000)>>12)>0 && debug>1 && debug<3 )
			{
                            printf("PEER=[%3lld]\n", (fr_1&0xFF000)>>12);
			}
                        if (command==PATCH_CMD && debug>1 && debug<3 && (fr_1&0xFF00000000) == 0x5E00000000)         //ADD LISTING 
                        {
                        //patch_site = (sr_3&0xFFF0000000000000)>>53;
                        patch_site = ((fr_4&0xFF00000000)>>32);
                            if (debug>0)// && patch_site == site_id) 
                            {
                                targetp = ((fr_4&0xFFFF000)>>12);
                                sourcep = ((fr_1&0xFFFF000)>>12);
                                //printf("Add Source[%6lldg] Target[%6lldg] Site=[%3lld] ADD Site=[%2llX]\n", sourcep, targetp, site_id, patch_site );
                                printf("Add Source[%6lldg] Target[%6lldg] Site=[%3lld]\n", sourcep, targetp, site_id);  
                                printf("MT-1=[0x%2X]\n", xstatus);
                                printf("MT-2=[0x%2llX]\n", (fr_1&0x780000000)>>31);
                                printf("FR_1=[%10llX]\n", fr_1);
                                //printf("FR_2=[%10llX]\n", fr_2);
                                //printf("FR_3=[%10llX]\n", fr_3);
                                printf("FR_4=[%10llX]\n", fr_4);
                                //printf("FR_5=[%10llX]\n", fr_5);
                                //printf("FR_6=[%10llX]\n", fr_6);
                            }
                        }

                        if (command==DATA_CMDX && debug>3 && (fr_1&0xFF00000000) == 0x5B00000000)         //KICK LISTING?? 
                        {
                                printf("Receiving Kick Command. command=[%2X]\n", command);
                                printf("MT-1=[0x%2X]\n", xstatus);
                                printf("MT-2=[0x%2llX]\n", (fr_1&0x780000000)>>31);
                                //printf("Kicked=[%4llX]i\n", (fr_4&0xFFFFF000)>>12);
                                printf("Kicked=[%6lld]i\n", (fr_4&0xFFFFF000)>>12);
                                //printf("Site_ID_maybe=[%3llX]\n", ((sr_2&0xFFF00)>>10));
                                printf("FR_1=[%10llX]\n", fr_1);
                                //printf("FR_2=[%10llX]\n", fr_2);
                                //printf("FR_3=[%10llX]\n", fr_3);
                                printf("FR_4=[%10llX]\n", fr_4);
                                //printf("FR_5=[%10llX]\n", fr_5);
                                //printf("FR_6=[%10llX]\n", fr_6);

                        }

			if (debug>2 && debug<4) //This prints if nothing else is received and you need some numbers
                        {
			    printf("command=[%2X]\n", command); //print original command bits
                            printf("MT-1=[0x%2X]\n", mt1);
                            printf("MT-1 Binary = ");
                            for(unsigned short int i=0; i<5; i++)
                            {	
                                signed short int buffer = (mt1>>4-i)&0x1;
                                printf("[%1d] ", buffer);
                            }
                            printf("\n");
                            if (mt1 == 0x1F)
                            {
                                printf("MT-2=[0x%1X]\n", mt2);
                                printf("MT-2 Binary = ");
                                for(unsigned short int i=0; i<4; i++)
                                {	
                                    signed short int buffer = (mt2>>3-i)&0x1;
                                    printf("[%1d] ", buffer);
                                }
                                printf("\n");
                            }

                            /*printf("MT-A=[0x%1X]\n", mta); //mt-a, mt-b, mt-d1 and mt-d2 seems to apply only to inbound calls to CC; disregard on CC??
                            if (mta == 0xF)
                            {
                                printf("MT-B=[0x%1X]\n", mtb);
                            }
                            if (mtb == 0x7)
                            {
                                printf("MT-D1=[0x%2X]\n", mtd1);
                            }
                            if (fr_1 != fr_4 && mtb== 0x7)
                            {
                                printf("MT-D2=[0x%2X]\n", mtd2);
                            } */
                            printf("FR_1=[%10llX]\n", fr_1);
                            printf("FR_2=[%10llX]\n", fr_2);
                            printf("FR_3=[%10llX]\n", fr_3);
                            printf("FR_4=[%10llX]\n", fr_4);
                            printf("FR_5=[%10llX]\n", fr_5);
                            printf("FR_6=[%10llX]\n", fr_6);
                                

			}
                        else if (command==vcmd && lcn>0)
			{
                                if (fr_1 == fr_3 && fr_4 == fr_6) //error detection for groupx, senderx, xstatus variables, probably redundant
                                {
                                    groupx = (fr_1&0xFFFF000)>>12;
                                    senderx = (fr_4&0xFFFFF000)>>12;
                                }
				if (lcn==current_lcn) //lcn==current_lcn
				{
					last_voice_time = time(NULL); 
					voice_to=0;
				}
				
                                                 
				else         
                                {
                                    
                                    printf("Time: %s  AFC=%d\tACTIVE\tMT-1=[0x%2X] \tMT-2=[0x%1X] \tLCN=%d \n", getTime(), AFC, mt1, mt2, lcn);
                                    if (x_choice==1)
                                    { 
                                        printf("Sender=[%7lldi]\n", senderx);
                                        printf("Group=[%6lldg]\n", groupx);
                                    }
                                    if (x_choice==1 && mt1 == 0x3)
                                    { 
                                        printf("Digital Group Voice Channel Assignment\n");
                                    }
                                    if (x_choice==1 && mt1 == 0x2)
                                    { 
                                        printf("Group Data Channel Assignment\n");
                                    }
                                    if (x_choice==1 && mt1 == 0x1)
                                    { 
                                        printf("TDMA Group Voice Channel Assignment\n");
                                    }
                                    if (x_choice==1 && mt1 == 0x1F && mt2 == 0x0)
                                    { 
                                        printf("Initiate Test Call\n");
                                    }
                                    if (x_choice==1 && mt1 == 0x1F && mt2 == 0xD)
                                    { 
                                        printf("Serial Number Request\n");
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
                                        printf("MT-1 Binary = ");
                                        for(unsigned short int i=0; i<5; i++)
                                        {	
                                            signed short int buffer = (mt1>>4-i)&0x1;
                                            printf("[%1d] ", buffer);
                                        }
                                        printf("\n");
                                        if (mt1 == 0x1F)
                                        {
                                            printf("MT-2 Binary = ");
                                            for(unsigned short int i=0; i<4; i++)
                                            {	
                                                signed short int buffer = (mt2>>3-i)&0x1;
                                                printf("[%1d] ", buffer);
                                            }
                                            printf("\n");
                                        }
                                        printf("FR_1=[%10llX]\n", fr_1);
                                        printf("FR_2=[%10llX]\n", fr_2);
                                        printf("FR_3=[%10llX]\n", fr_3);
                                        printf("FR_4=[%10llX]\n", fr_4);
                                        printf("FR_5=[%10llX]\n", fr_5);
                                        printf("FR_6=[%10llX]\n", fr_6);

                                    }

                                }
                                    

			}
                        else
			{
			    //printf("LEDACS-ESK-ANALYZER v0.27 Build 2020.09.07 \n");	
                                
            		}
		}
	}

	return 0;
}
