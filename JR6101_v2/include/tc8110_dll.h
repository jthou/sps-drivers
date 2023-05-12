/********************************************************************************************************************************
Copyright (C), 2016-2021, Jingtech
File name  :   tc8110_dll.h
Author     :   Jingtech
Version    :   1.0
Date       :   2021-07-23
Description:   ���ļ��г����豸����ӿڼ�������ݽṹ�������豸ʱ����ø��ļ���
Others     :
Function List:
History    :   ��ʷ�޸ļ�¼
          <author>    <time>   <version >   <desc>
          Jingtech  2021-07-23    1.0       JR6101����һ�潨����
********************************************************************************************************************************/
#ifndef _TC8110_DLL_H_
#define _TC8110_DLL_H_
#include <linux/ioctl.h>
#include <semaphore.h>
#include <sys/socket.h>
#include <sys/mman.h>
#include <linux/netlink.h>
#include <stdint.h>
#include  "common.h"


#ifdef _USRDLL
#define DRVDLL_API __declspec(dllexport)
#else
#define DRVDLL_API //__declspec(dllimport)
#endif


/********************************************************************************************************************************
Function      : DevOpen
Description   : ���豸����ȡ�豸����Ȩ��Ӧ�ó�������豸ǰ���øú��������豸����һ��Ӧ�ó���
                ���������豸���ڱ�����Ӧ�ó�����������ش���
Calls         :
Called By     : Ӧ�ó���
Input         : DevInf_t deviceinfo
                   �豸��Ϣ���ָ�룬����Ҫ��ȡ�豸��Ϣʱ��ΪNULL
                s32_t flag
                   Ԥ��
Output        :
Return        : s32_t 0:�ɹ���
                    1:ʧ�ܡ����������Ӳ���Ƿ�װ�ɹ���
                    3:������Դʧ�ܡ�
Others        :
********************************************************************************************************************************/
DRVDLL_API s32_t DevOpen(DevInf_t *pdeviceinfo, s32_t flag);

/********************************************************************************************************************************
Function      : DevClose
Description   : �ͷŶ��豸�Ŀ��ƣ�Ӧ�ó�����ɶ�����ͨ����������ã�����ǰӦ�ȵ���DevCloseETH
                �����ر�����ͨ����
Calls         :
Called By     : Ӧ�ó���
Input         :
Output        :
Return        : s32_t 0:�ɹ���
                    1:�豸���󡣼��������Ӳ���Ƿ�װ�ɹ���
                    2:ͨ��δ�����رա�
Others        :
********************************************************************************************************************************/
DRVDLL_API s32_t DevClose(void);

/********************************************************************************************************************************
Function      : DevOpenCh
Description   : ��ͨ�����豸�򿪺�ɳɹ����á�
Calls         :
Called By     : Ӧ�ó���
Input         : u32_t chx
                    ��ͨ��ָʾ��ȡֵ��ΧΪ:(0-3)��
                s08_t mode
                    ͨ������ģʽ:0=ֻץ����1=ֻ������2=ץ�����հ�
Output        :
Return        : s32_t 0:�ɹ���
                    1:�豸���󡣼��������Ӳ���Ƿ�װ�ɹ���
                    2:��������
                    3:ͨ���Ѿ��򿪡�
Others        :
********************************************************************************************************************************/
DRVDLL_API s32_t DevOpenCh(u32_t chx, s08_t mode);

/********************************************************************************************************************************
Function      : DevCloseCh
Description   : �ر�ͨ��
Calls         :
Called By     : Ӧ�ó���
Input         : u32_t chx
                    ��ͨ��ָʾ��ȡֵ��ΧΪ:(0-3)��
Output        :
Return        : s32_t 0:�ɹ���
                    1:�豸���󡣼��������Ӳ���Ƿ�װ�ɹ���
                    2:��������
                    3:�豸æµ��
Others        :
********************************************************************************************************************************/
DRVDLL_API s32_t DevCloseCh(u32_t  chx);

/********************************************************************************************************************************
Function      : DevSendMBAlloc
Description   :���뷢���ڴ��
Calls         :
Called By     : dllģ��
Input         : u32_t chx
                    ͨ��ָʾ
               MB_Des_t *PMB
                    �ڴ��������ָ��
Output        :
Return        : s32_t 0:�ɹ���
                    1:�豸���󡣼��������Ӳ���Ƿ�װ�ɹ���
                    2:��������
                    3:û�п�����Դ��
                    4:�������ò��Ϸ������豸��ʽ��֧������������
Others        :
********************************************************************************************************************************/
DRVDLL_API s32_t DevSendMBAlloc(u32_t chx, MB_Des_t *PMB);

/********************************************************************************************************************************
Function      : DevReadMBFree
Description   :�ͷ��ڴ�
Calls         :
Called By     : dllģ��
Input         : u32_t chx
                    ͨ��ָʾ
                MB_Des_t *PMB
                    �ڴ��������ָ��
Output        :
Return        : s32_t 0:�ɹ���
                    1:�豸���󡣼��������Ӳ���Ƿ�װ�ɹ���
                    2:��������
                    3:�������ò��Ϸ������豸��ʽ��֧������������
Others        :
********************************************************************************************************************************/
DRVDLL_API s32_t DevReadMBFree(u32_t chx, MB_Des_t *PMB);

/********************************************************************************************************************************
Function      : DevMBRead
Description   : ��ȡһ���ڴ��
Calls         :
Called By     : dllģ��
Input         : u32_t chx
                    ͨ��ָʾ
                MB_Des_t *PMB
                    �ڴ��������ָ��
Output        :
Return        : s32_t 0:�ɹ���
                    1:�豸���󡣼��������Ӳ���Ƿ�װ�ɹ���
                    2:��������
                    3:û�пɶ����ݡ�
                    4:�������ò��Ϸ������豸��ʽ��֧������������
                    5:�ڴ��ָ�������
Others        :
********************************************************************************************************************************/
DRVDLL_API s32_t DevMBRead(u32_t chx, MB_Des_t *PMB);

/********************************************************************************************************************************
Function      : DevMBSend
Description   : ����һ���ڴ��
Calls         :
Called By     : dllģ��
Input         : u32_t chx
                    ͨ��ָʾ
                MB_Des_t *PMB
                    �ڴ��������ָ��
Output        :
Return        : s32_t 0:�ɹ���
                    1:�豸���󡣼��������Ӳ���Ƿ�װ�ɹ���
                    2:��������
                    4:�������ò��Ϸ������豸��ʽ��֧������������
Others        :
********************************************************************************************************************************/
DRVDLL_API s32_t DevMBSend(u32_t chx, MB_Des_t *PMB);

#endif //_DEV_H_
