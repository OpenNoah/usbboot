#ifndef __API_USBBOOT
#define __API_USBBOOT

typedef struct _NAND_PARA_IN
{
	unsigned char dev,max_chip;
	unsigned char *buf,*cs_map;
	unsigned int start,length,option;

	int (* Check) (unsigned char *,unsigned char *,unsigned int);
}NAND_IN;

typedef struct _NAND_PARA_OUT
{
	unsigned char *status;
}NAND_OUT;

typedef struct _SDRAM_PARA_IN
{
	unsigned char dev;
	unsigned char *buf;
	unsigned int start,length,option;
}SDRAM_IN;

//功能类API:
extern int API_Init(void);				//  init
extern int API_Get_Dev_Num(void);		//：获取当前已经连接的设备个数
extern int API_IsBoot(void);			//测试设备是否已经工作在第二阶段 1：是 other：否
//操作类API:
extern int API_Boot(int obj);				//：对选中的设备进行Boot操作
extern int API_Fconfig(int obj,char *fname);	//：用fname指定的配置文件对设备进行配置
extern int API_Go(int obj,unsigned int addr);			//：从RAM中的addr地址开始运行程序

extern int API_Nand_Program_Check(NAND_IN *,NAND_OUT *);
extern int API_Nand_Program_File(NAND_IN *,NAND_OUT *,char *fname);
extern int API_Nand_Program_File_Planes(NAND_IN *,NAND_OUT *,char *fname);
extern int API_Nand_Read(NAND_IN *,char *fname);
extern int API_Nand_Readoob(NAND_IN *,char *fname);
extern int API_Nand_Readraw(NAND_IN *,char *fname);
extern int API_Nand_Erase(NAND_IN *);
extern int API_Nand_Query(NAND_IN *);
extern int API_Nand_Make(NAND_IN *,char *fname);
extern int API_Read_Nand_To_Ram(NAND_IN *nand_in,unsigned int ram_addr);
extern int API_Nand_Markbad(NAND_IN *);
extern int API_Sdram_Load(SDRAM_IN *);
extern int API_Sdram_Load_File(SDRAM_IN *,char *);
extern int API_Debug_GPIO(int, unsigned char , unsigned char );
extern int API_Debug_Memory(int, unsigned int , unsigned int );


extern int API_SD_Program_Check(NAND_IN *,NAND_OUT *);
extern int API_SD_Program_File(NAND_IN *,NAND_OUT *,char *fname);
extern int API_SD_Read(NAND_IN *,char *fname);
#endif
