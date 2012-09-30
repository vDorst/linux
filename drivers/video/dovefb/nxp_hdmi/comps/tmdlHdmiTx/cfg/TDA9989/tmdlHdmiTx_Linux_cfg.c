/**
 * Copyright (C) 2009 NXP N.V., All Rights Reserved.
 * This source code and any compilation or derivative thereof is the proprietary
 * information of NXP N.V. and is confidential in nature. Under no circumstances
 * is this software to be  exposed to or placed under an Open Source License of
 * any type without the expressed written permission of NXP N.V.
 *
 * \file          tmdlHdmiTx_LinuxCfg.c
 *
 * \version       Revision: 1
 *
 * \date          Date: 25/03/11 11:00
 *
 * \brief         devlib driver component API for the TDA998x HDMI Transmitters
 *
 * \section refs  Reference Documents
 * HDMI Tx Driver - FRS.doc,
 *
 * \section info  Change Information
 *
 * \verbatim

   History:       tmdlHdmiTx_LinuxCfg.c
 *
 * *****************  Version 2  ***************** 
 * User: V. Vrignaud Date: March 25th, 2011
 *
 * *****************  Version 1  *****************
 * User: A. Lepine Date: October 1st, 2009
 * initial version
 *

   \endverbatim
 *
*/

/*============================================================================*/
/*                       INCLUDE FILES                                        */
/*============================================================================*/
/* The following includes are used by I2C access function. If                 */
/* you need to rewrite these functions for your own SW infrastructure, then   */
/* it can be removed                                                          */
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/semaphore.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/i2c.h>
#include <linux/delay.h>

/* low level I2C functions */
#include "tda998x_i2c.h"

/* for CEC error constants */
#include "tmdlHdmiCEC_Types.h"

/*============================================================================*/
/*                                MACRO                                       */
/*============================================================================*/
/* macro for quick error handling */
#define RETIF(cond, rslt) if ((cond)){return (rslt);}

/******************************************************************************
 ******************************************************************************
 *                 THIS PART CAN BE MODIFIED BY CUSTOMER                      *
 ******************************************************************************
 *****************************************************************************/

/* I2C adress of the unit                                                     */
/* Put there the I2C slave adress of the Tx transmitter IC                    */
#define UNIT_I2C_ADDRESS_0 0x70

/* Intel CE 4100 I2C bus number                                               */
/* Put there the number of I2C bus handling the Rx transmitter IC             */
#define I2C_BUS_NUMBER_0 0 // initial:0

/* I2C Number of bytes in the data buffer.                                    */
#define SUB_ADDR_BYTE_COUNT_0 1

/* Priority of the command task                                               */
/* Command task is an internal task that handles incoming event from the IC   */
/* put there a value that will ensure a response time of ~20ms in your system */
#define COMMAND_TASK_PRIORITY_0  250
#define COMMAND_TASK_PRIORITY_1  250

/* Priority of the hdcp check tasks */
/* HDCP task is an internal task that handles periodical HDCP processing      */
/* put there a value that will ensure a response time of ~20ms in your system */
#define HDCP_CHECK_TASK_PRIORITY_0  250

/* Stack size of the command tasks */
/* This value depends of the type of CPU used, and also from the length of    */
/* the customer callbacks. Increase this value if you are making a lot of     */
/* processing (function calls & local variables) and that you experience      */
/* stack overflows                                                            */
#define COMMAND_TASK_STACKSIZE_0 128
#define COMMAND_TASK_STACKSIZE_1 128

/* stack size of the hdcp check tasks */
/* This value depends of the type of CPU used, default value should be enough */
/* for all configuration                                                      */
#define HDCP_CHECK_TASK_STACKSIZE_0 128

/* Size of the message queues for command tasks                               */
/* This value defines the size of the message queue used to link the          */
/* the tmdlHdmiTxHandleInterrupt function and the command task. The default   */
/* value below should fit any configuration                                   */
#define COMMAND_TASK_QUEUESIZE_0 128
#define COMMAND_TASK_QUEUESIZE_1 128

/* HDCP key seed                                                              */
/* HDCP key are stored encrypted into the IC, this value allows the IC to     */
/* decrypt them. This value is provided to the customer by NXP customer       */
/* support team.                                                              */
#define KEY_SEED 0x1234

/* Video port configuration for YUV444 input                                  */
/* You can specify in this table how are connected video ports in case of     */
/* YUV444 input signal. Each line of the array corresponds to a quartet of    */
/* pins of one video port (see comment on the left to identify them). Just    */
/* change the enum to specify which signal you connected to it. See file      */
/* tmdlHdmiTx_cfg.h to get the list of possible values                        */
const tmdlHdmiTxCfgVideoSignal444 videoPortMapping_YUV444[MAX_UNITS][6] = {
    {
        TMDL_HDMITX_VID444_BU_0_TO_3,   /* Signals connected to VPA[0..3] */
        TMDL_HDMITX_VID444_BU_4_TO_7,   /* Signals connected to VPA[4..7] */
        TMDL_HDMITX_VID444_GY_0_TO_3,   /* Signals connected to VPB[0..3] */
        TMDL_HDMITX_VID444_GY_4_TO_7,   /* Signals connected to VPB[4..7] */
        TMDL_HDMITX_VID444_VR_0_TO_3,   /* Signals connected to VPC[0..3] */
        TMDL_HDMITX_VID444_VR_4_TO_7    /* Signals connected to VPC[4..7] */
    }
};

/* Video port configuration for RGB444 input                                  */
/* You can specify in this table how are connected video ports in case of     */
/* RGB444 input signal. Each line of the array corresponds to a quartet of    */
/* pins of one video port (see comment on the left to identify them). Just    */
/* change the enum to specify which signal you connected to it. See file      */
/* tmdlHdmiTx_cfg.h to get the list of possible values                        */
#if 0
const tmdlHdmiTxCfgVideoSignal444 videoPortMapping_RGB444[MAX_UNITS][6] = {
    {
        TMDL_HDMITX_VID444_VR_0_TO_3,   /* Signals connected to VPB[0..3] */
        TMDL_HDMITX_VID444_VR_4_TO_7,   /* Signals connected to VPB[4..7] */
        TMDL_HDMITX_VID444_GY_0_TO_3,   /* Signals connected to VPA[0..3] */
        TMDL_HDMITX_VID444_GY_4_TO_7,   /* Signals connected to VPA[4..7] */
        TMDL_HDMITX_VID444_BU_0_TO_3,   /* Signals connected to VPC[0..3] */
        TMDL_HDMITX_VID444_BU_4_TO_7    /* Signals connected to VPC[4..7] */
    }
};
#else
const tmdlHdmiTxCfgVideoSignal444 videoPortMapping_RGB444[MAX_UNITS][6] = {
    {
        TMDL_HDMITX_VID444_VR_0_TO_3,   /* Signals connected to VPB[0..3] */
        TMDL_HDMITX_VID444_VR_4_TO_7,   /* Signals connected to VPB[4..7] */
        TMDL_HDMITX_VID444_BU_0_TO_3,   /* Signals connected to VPC[0..3] */
        TMDL_HDMITX_VID444_BU_4_TO_7,   /* Signals connected to VPC[4..7] */
        TMDL_HDMITX_VID444_GY_0_TO_3,   /* Signals connected to VPA[0..3] */
        TMDL_HDMITX_VID444_GY_4_TO_7    /* Signals connected to VPA[4..7] */
    }
};
#endif
/* Video port configuration for YUV422 input                                  */
/* You can specify in this table how are connected video ports in case of     */
/* YUV422 input signal. Each line of the array corresponds to a quartet of    */
/* pins of one video port (see comment on the left to identify them). Just    */
/* change the enum to specify which signal you connected to it. See file      */
/* tmdlHdmiTx_cfg.h to get the list of possible values                        */
const tmdlHdmiTxCfgVideoSignal422 videoPortMapping_YUV422[MAX_UNITS][6] = {
    {
        TMDL_HDMITX_VID422_Y_4_TO_7,           /* Signals connected to VPA[0..3] */    
        TMDL_HDMITX_VID422_Y_8_TO_11,          /* Signals connected to VPA[4..7] */    
        TMDL_HDMITX_VID422_UV_4_TO_7,          /* Signals connected to VPB[0..3] */
        TMDL_HDMITX_VID422_UV_8_TO_11,         /* Signals connected to VPB[4..7] */
        TMDL_HDMITX_VID422_NOT_CONNECTED,      /* Signals connected to VPC[0..3] */
        TMDL_HDMITX_VID422_NOT_CONNECTED       /* Signals connected to VPC[4..7] */
    }
};

/* Video port configuration for CCIR656 input                                 */
/* You can specify in this table how are connected video ports in case of     */
/* CCIR656 input signal. Each line of the array corresponds to a quartet of   */
/* pins of one video port (see comment on the left to identify them). Just    */
/* change the enum to specify which signal you connected to it. See file      */
/* tmdlHdmiTx_cfg.h to get the list of possible values                        */
const tmdlHdmiTxCfgVideoSignalCCIR656 videoPortMapping_CCIR656[MAX_UNITS][6] = {
    {
        TMDL_HDMITX_VIDCCIR_4_TO_7,         /* Signals connected to VPA[0..3] */
        TMDL_HDMITX_VIDCCIR_8_TO_11,        /* Signals connected to VPA[4..7] */
        TMDL_HDMITX_VIDCCIR_NOT_CONNECTED,  /* Signals connected to VPB[0..3] */
        TMDL_HDMITX_VIDCCIR_NOT_CONNECTED,  /* Signals connected to VPB[4..7] */
        TMDL_HDMITX_VIDCCIR_NOT_CONNECTED,  /* Signals connected to VPC[0..3] */
        TMDL_HDMITX_VIDCCIR_NOT_CONNECTED   /* Signals connected to VPC[4..7] */
    }
}; 


/* The following function must be rewritten by the customer to fit its own    */
/* SW infrastructure. This function allows reading through I2C bus.           */
/* tmbslHdmiTxSysArgs_t definition is located into tmbslHdmiTx_type.h file.   */
static tmErrorCode_t TxI2cReadFunction(tmbslHdmiTxSysArgs_t *pSysArgs)
{
   struct i2c_client *client = (pSysArgs->slaveAddr == UNIT_I2C_ADDRESS_0) ? 
				txGetThisI2cClient() : cecGetThisI2cClient();

   RETIF(!client || !client->adapter || 
         client->addr != pSysArgs->slaveAddr, TM_ERR_NULL_DATAINFUNC)

   RETIF(tda998x_i2c_read_regs(
	    client, pSysArgs->lenData, 
	    pSysArgs->firstRegister, pSysArgs->pData) == 0, TM_OK)

   return (pSysArgs->slaveAddr == UNIT_I2C_ADDRESS_0) ?
      TMDL_ERR_DLHDMITX_I2C_WRITE : TMDL_ERR_DLHDMICEC_I2C_WRITE;
}

/* The following function must be rewritten by the customer to fit its own    */
/* SW infrastructure. This function allows writing through I2C bus.           */
/* tmbslHdmiTxSysArgs_t definition is located into tmbslHdmiTx_type.h file.   */
static tmErrorCode_t TxI2cWriteFunction(tmbslHdmiTxSysArgs_t *pSysArgs)
{
   struct i2c_client *client = (pSysArgs->slaveAddr == UNIT_I2C_ADDRESS_0) ? 
				txGetThisI2cClient() : cecGetThisI2cClient();

   RETIF(!client || !client->adapter ||
         client->addr != pSysArgs->slaveAddr, TM_ERR_NULL_DATAOUTFUNC)

   RETIF(tda998x_i2c_write_regs(
            client, pSysArgs->lenData, 
            pSysArgs->firstRegister, pSysArgs->pData) == 0, TM_OK)

   return (pSysArgs->slaveAddr == UNIT_I2C_ADDRESS_0) ?
      TMDL_ERR_DLHDMITX_I2C_READ : TMDL_ERR_DLHDMICEC_I2C_READ;
}


/******************************************************************************
    \brief  This function blocks the current task for the specified amount time. 
            This is a passive wait.

    \param  Duration    Duration of the task blocking in milliseconds.

    \return The call result:
            - TM_OK: the call was successful
            - TMDL_ERR_DLHDMITX_NO_RESOURCES: the resource is not available

******************************************************************************/
tmErrorCode_t tmdlHdmiTxIWWait
(
    UInt16 duration
)
{
	mdelay((unsigned long)duration);

    return(TM_OK);
}

/******************************************************************************
    \brief  This function creates a semaphore.

    \param  pHandle Pointer to the handle buffer.

    \return The call result:
            - TM_OK: the call was successful
            - TMDL_ERR_DLHDMITX_NO_RESOURCES: the resource is not available
            - TMDL_ERR_DLHDMITX_INCONSISTENT_PARAMS: an input parameter is
              inconsistent

******************************************************************************/
tmErrorCode_t tmdlHdmiTxIWSemaphoreCreate
(
    tmdlHdmiTxIWSemHandle_t *pHandle
)
{
    struct semaphore * mutex;
    
    /* check that input pointer is not NULL */
    RETIF(pHandle == Null, TMDL_ERR_DLHDMITX_INCONSISTENT_PARAMS)

    mutex = (struct semaphore *)kmalloc(sizeof(struct semaphore),GFP_KERNEL);
    if (!mutex) {
       printk(KERN_ERR "malloc failed in %s\n",__func__);
       return TMDL_ERR_DLHDMITX_NO_RESOURCES;
    }
    
    sema_init(mutex,1);
    *pHandle = (tmdlHdmiTxIWSemHandle_t)mutex;

    RETIF(pHandle == NULL, TMDL_ERR_DLHDMITX_NO_RESOURCES)

    return(TM_OK);
}

/******************************************************************************
    \brief  This function destroys an existing semaphore.

    \param  Handle  Handle of the semaphore to be destroyed.

    \return The call result:
            - TM_OK: the call was successful
            - TMDL_ERR_DLHDMITX_BAD_HANDLE: the handle number is wrong

******************************************************************************/
tmErrorCode_t tmdlHdmiTxIWSemaphoreDestroy
(
    tmdlHdmiTxIWSemHandle_t handle
)
{
   RETIF(handle == False, TMDL_ERR_DLHDMITX_BAD_HANDLE);
   
   if (atomic_read((atomic_t*)&((struct semaphore *)handle)->count) < 1) {
      printk(KERN_ERR "release catched semaphore");
   }
   
   kfree((void*)handle);
   
   return(TM_OK);
}

/******************************************************************************
    \brief  This function acquires the specified semaphore.

    \param  Handle  Handle of the semaphore to be acquired.

    \return The call result:
            - TM_OK: the call was successful
            - TMDL_ERR_DLHDMITX_BAD_HANDLE: the handle number is wrong

******************************************************************************/
tmErrorCode_t tmdlHdmiTxIWSemaphoreP
(
    tmdlHdmiTxIWSemHandle_t handle
)
{
    down((struct semaphore *)handle);

    return(TM_OK);
}

/******************************************************************************
    \brief  This function releases the specified semaphore.

    \param  Handle  Handle of the semaphore to be released.

    \return The call result:
            - TM_OK: the call was successful
            - TMDL_ERR_DLHDMITX_BAD_HANDLE: the handle number is wrong

******************************************************************************/
tmErrorCode_t tmdlHdmiTxIWSemaphoreV
(
    tmdlHdmiTxIWSemHandle_t handle
)
{
    up((struct semaphore *)handle);

    return(TM_OK);
}

/*============================================================================*/
/*                            END OF FILE                                     */
/*============================================================================*/

