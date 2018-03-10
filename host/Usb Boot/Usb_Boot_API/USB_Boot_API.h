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

//������API:
extern int API_Init(void);				//  init
extern int API_Get_Dev_Num(void);		//����ȡ��ǰ�Ѿ����ӵ��豸����
extern int API_IsBoot(void);			//�����豸�Ƿ��Ѿ������ڵڶ��׶� 1���� other����
//������API:
extern int API_Boot(int obj);				//����ѡ�е��豸����Boot����
extern int API_Fconfig(int obj,char *fname);	//����fnameָ���������ļ����豸��������
extern int API_Go(int obj,unsigned int addr);			//����RAM�е�addr��ַ��ʼ���г���

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
