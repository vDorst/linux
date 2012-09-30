/*****************************************************************************/
/*                                                                           */
/* This program is free software; you can redistribute it and/or modify      */
/* it under the terms of the GNU General Public License as published by      */
/* the Free Software Foundation, using version 2 of the License.             */
/*                                                                           */
/* This program is distributed in the hope that it will be useful,           */
/* but WITHOUT ANY WARRANTY; without even the implied warranty of            */
/* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the              */
/* GNU General Public License for more details.                              */
/*                                                                           */
/* You should have received a copy of the GNU General Public License         */
/* along with this program; if not, write to the Free Software               */
/* Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307       */
/* USA.                                                                      */
/*                                                                           */
/*****************************************************************************/

/*============================================================================*/
/*                       INCLUDE FILES                                        */
/*============================================================================*/
/* The following includes are used by I2C access function. If                 */
/* you need to rewrite these functions for your own SW infrastructure, then   */
/* it can be removed                                                          */
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/i2c.h>
#include <linux/delay.h>

#include "tmNxTypes.h"
#include "tmdlHdmiCEC.h"
#include "tmdlHdmiCEC_cfg.h"

/* low level I2C functions */
#include "tda998x_i2c.h"

/*============================================================================*/
/*                          MACROS                                            */
/*============================================================================*/
#define RETIF(cond, rslt) if ((cond)){return (rslt);}

/*============================================================================*/
/*                       CONSTANTS DECLARATIONS                               */
/*============================================================================*/
#define COMMAND_TASK_PRIORITY_0  250
#define COMMAND_TASK_STACKSIZE_0 128
#define COMMAND_TASK_QUEUESIZE_0 8

/* I2C adress of the unit */
#ifdef TMFL_TDA9996
	#define UNIT_I2C_ADDRESS_0 0x60 /* I2C Address of TDA9996 */
#else
	#define UNIT_I2C_ADDRESS_0 0x34 /* I2C Address of TDA9950 */
#endif


/******************************************************************************
 ******************************************************************************
 *                 THIS PART CAN BE MODIFIED BY CUSTOMER                      *
 ******************************************************************************
 *****************************************************************************/

/* The following function must be rewritten by the customer to fit its own    */
/* SW infrastructure. This function allows reading through I2C bus.           */
static tmErrorCode_t cecI2cReadFunction(tmdlHdmiCecSysArgs_t *pSysArgs)
{
   struct i2c_client *client = cecGetThisI2cClient();

   RETIF(!client || !client->adapter || 
          client->addr != pSysArgs->slaveAddr, TM_ERR_NULL_DATAINFUNC)

   RETIF(tda998x_i2c_read_regs(
            client, pSysArgs->lenData, 
            pSysArgs->firstRegister, pSysArgs->pData) == 0, TM_OK)

   return TMDL_ERR_DLHDMICEC_I2C_WRITE;
}

/* The following function must be rewritten by the customer to fit its own    */
/* SW infrastructure. This function allows writing through I2C bus.           */
static tmErrorCode_t cecI2cWriteFunction(tmdlHdmiCecSysArgs_t *pSysArgs)
{
   struct i2c_client *client = cecGetThisI2cClient();

   RETIF(!client || !client->adapter ||
         client->addr != pSysArgs->slaveAddr, TM_ERR_NULL_DATAOUTFUNC)

   RETIF(tda998x_i2c_write_regs(
            client, pSysArgs->lenData, 
            pSysArgs->firstRegister, pSysArgs->pData) == 0, TM_OK)

   return TMDL_ERR_DLHDMICEC_I2C_READ;
}

/**
 * \brief Configuration Tables. This table can be modified by the customer 
            to choose its prefered configuration
 */

static tmdlHdmiCecCapabilities_t CeccapabilitiesList = {
    TMDL_HDMICEC_DEVICE_UNKNOWN, CEC_VERSION_1_3a
};

static tmdlHdmiCecDriverConfigTable_t CecdriverConfigTable[MAX_UNITS] = {
    {
    COMMAND_TASK_PRIORITY_0,
    COMMAND_TASK_STACKSIZE_0,
    COMMAND_TASK_QUEUESIZE_0,
    UNIT_I2C_ADDRESS_0,
    cecI2cReadFunction,
    cecI2cWriteFunction,
    &CeccapabilitiesList
    }
};


/**
    \brief This function allows to the main driver to retrieve its
           configuration parameters.

    \param pConfig Pointer to the config structure

    \return The call result:
            - TM_OK: the call was successful
            - TMDL_ERR_DLHDMICEC_BAD_UNIT_NUMBER: the unit number is wrong or
              the receiver instance is not initialised
            - TMDL_ERR_DLHDMICEC_INCONSISTENT_PARAMS: an input parameter is
              inconsistent

******************************************************************************/
tmErrorCode_t tmdlHdmiCecCfgGetConfig
(
    tmUnitSelect_t                 unit,
    tmdlHdmiCecDriverConfigTable_t *pConfig
)
{
    /* check if unit number is in range */
    RETIF((unit < 0) || (unit >= MAX_UNITS), TMDL_ERR_DLHDMICEC_BAD_UNIT_NUMBER)

    /* check if pointer is Null */
    RETIF(pConfig == Null, TMDL_ERR_DLHDMICEC_INCONSISTENT_PARAMS)

    *pConfig = CecdriverConfigTable[unit];

    return(TM_OK);
}

/*============================================================================*/
/*                            END OF FILE                                     */
/*============================================================================*/
