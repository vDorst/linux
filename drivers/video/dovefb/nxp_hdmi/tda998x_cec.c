/*****************************************************************************/
/* Copyright (c) 2009 NXP Semiconductors BV                                  */
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

#define _cec_c_

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/cdev.h>  
#include <linux/fs.h>
#include <linux/ioctl.h>
#include <linux/i2c.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/input.h>
#include <linux/poll.h>
#include <linux/gpio.h>

/* HDMI DevLib */
#include "tmNxCompId.h"
#include "tmdlHdmiCEC.h"
#include "tmdlHdmiCEC_local.h"

/* local */
#include "tda998x_version.h"
#include "tda998x_cec.h"
#include "tda998x_i2c.h"
#include "tda998x_ioctl.h"
#include "tda998x_exports.h"

/*
 *
 * DEFINITION
 * ----------
 * LEVEL 0
 *
 */

/*
 *  Global
 */

MODULE_DEVICE_TABLE(i2c, this_i2c_id);
static const struct i2c_device_id this_i2c_id[] = {
   { CEC_NAME, 0 },
   { },
};
static struct cdev our_cdev;
static cec_instance our_instance;

#ifdef TWL4030_HACK
/* AL : hack to bypass keypad */
struct input_dev *gkp_input;
extern struct input_dev *get_twm4030_input(void);
#endif


/*
 *  Module params
 */

static int param_verbose=0,param_major=0,param_minor=0,param_device=4,param_addr=0xFFFF;
module_param_named(verbose,param_verbose,int,S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(verbose, "Make the driver verbose");
module_param_named(major, param_major, int, S_IRUGO);
MODULE_PARM_DESC(major, "The major number of the device mapper");
module_param_named(device, param_device, int, S_IRUGO);
MODULE_PARM_DESC(device, "Device type can be 0:tv, 1:rec 3:tuner 4:mediaplayer, 5:audio");
module_param_named(addr, param_addr, int, S_IRUGO);
MODULE_PARM_DESC(addr, "Physical address (until EDID received)");

/*
 *
 * TOOLBOX
 * -------
 * LEVEL 1
 *
 * - i2c read/write
 * - chip Id check
 * - i2c client info
 * 
 */


/* 
 *  Get main and unique I2C Client driver handle (called from NXP SDK)
 */
struct i2c_client *cecGetThisI2cClient(void)
{
   return our_instance.driver.i2c_client;
}

/*
 * error handling
 */
static char *hdmi_cec_err_string(int err)
{
   switch (err)
      {
      case TMDL_ERR_DLHDMICEC_COMPATIBILITY: {return "SW Interface compatibility";break;}
      case TMDL_ERR_DLHDMICEC_MAJOR_VERSION: {return "SW Major Version error";break;}
      case TMDL_ERR_DLHDMICEC_COMP_VERSION: {return "SW component version error";break;}
      case TMDL_ERR_DLHDMICEC_BAD_UNIT_NUMBER: {return "Invalid device unit number";break;}
      case TMDL_ERR_DLHDMICEC_BAD_INSTANCE: {return "Bad input instance value  ";break;}
      case TMDL_ERR_DLHDMICEC_BAD_HANDLE: {return "Bad input handle";break;}
      case TMDL_ERR_DLHDMICEC_BAD_PARAMETER: {return "Invalid input parameter";break;}
      case TMDL_ERR_DLHDMICEC_NO_RESOURCES: {return "Resource is not available ";break;}
      case TMDL_ERR_DLHDMICEC_RESOURCE_OWNED: {return "Resource is already in use";break;}
      case TMDL_ERR_DLHDMICEC_RESOURCE_NOT_OWNED: {return "Caller does not own resource";break;}
      case TMDL_ERR_DLHDMICEC_INCONSISTENT_PARAMS: {return "Inconsistent input params";break;}
      case TMDL_ERR_DLHDMICEC_NOT_INITIALIZED: {return "Component is not initializ";break;}
      case TMDL_ERR_DLHDMICEC_NOT_SUPPORTED: {return "Function is not supported";break;}
      case TMDL_ERR_DLHDMICEC_INIT_FAILED: {return "Initialization failed";break;}
      case TMDL_ERR_DLHDMICEC_BUSY: {return "Component is busy";break;}
      case TMDL_ERR_DLHDMICEC_I2C_READ: {return "Read error";break;}
      case TMDL_ERR_DLHDMICEC_I2C_WRITE: {return "Write error";break;}
      case TMDL_ERR_DLHDMICEC_FULL: {return "Queue is full";break;}
      case TMDL_ERR_DLHDMICEC_NOT_STARTED: {return "Function is not started";break;}
      case TMDL_ERR_DLHDMICEC_ALREADY_STARTED: {return "Function is already starte";break;}
      case TMDL_ERR_DLHDMICEC_ASSERTION: {return "Assertion failure";break;}
      case TMDL_ERR_DLHDMICEC_INVALID_STATE: {return "Invalid state for function";break;}
      case TMDL_ERR_DLHDMICEC_OPERATION_NOT_PERMITTED: {return "Corresponds to posix EPERM";break;}
      default : {return "Unexpected error";break;}
      }
}

#ifndef CEC_I2C_DEBUG
static
#endif
char *cec_opcode(int op)
{
   switch (op)
      {
      case CEC_OPCODE_FEATURE_ABORT: {return "CEC_OPCODE_FEATURE_ABORT";break;}
      case CEC_OPCODE_IMAGE_VIEW_ON: {return "CEC_OPCODE_IMAGE_VIEW_ON";break;}
      case CEC_OPCODE_TUNER_STEP_INCREMENT: {return "CEC_OPCODE_TUNER_STEP_INCREMENT";break;}
      case CEC_OPCODE_TUNER_STEP_DECREMENT: {return "CEC_OPCODE_TUNER_STEP_DECREMENT";break;}
      case CEC_OPCODE_TUNER_DEVICE_STATUS: {return "CEC_OPCODE_TUNER_DEVICE_STATUS";break;}
      case CEC_OPCODE_GIVE_TUNER_DEVICE_STATUS: {return "CEC_OPCODE_GIVE_TUNER_DEVICE_STATUS";break;}
      case CEC_OPCODE_RECORD_ON: {return "CEC_OPCODE_RECORD_ON";break;}
      case CEC_OPCODE_RECORD_STATUS: {return "CEC_OPCODE_RECORD_STATUS";break;}
      case CEC_OPCODE_RECORD_OFF: {return "CEC_OPCODE_RECORD_OFF";break;}
      case CEC_OPCODE_TEXT_VIEW_ON: {return "CEC_OPCODE_TEXT_VIEW_ON";break;}
      case CEC_OPCODE_RECORD_TV_SCREEN: {return "CEC_OPCODE_RECORD_TV_SCREEN";break;}
      case CEC_OPCODE_GIVE_DECK_STATUS: {return "CEC_OPCODE_GIVE_DECK_STATUS";break;}
      case CEC_OPCODE_DECK_STATUS: {return "CEC_OPCODE_DECK_STATUS";break;}
      case CEC_OPCODE_SET_MENU_LANGUAGE: {return "CEC_OPCODE_SET_MENU_LANGUAGE";break;}
      case CEC_OPCODE_CLEAR_ANALOGUE_TIMER: {return "CEC_OPCODE_CLEAR_ANALOGUE_TIMER";break;}
      case CEC_OPCODE_SET_ANALOGUE_TIMER: {return "CEC_OPCODE_SET_ANALOGUE_TIMER";break;}
      case CEC_OPCODE_TIMER_STATUS: {return "CEC_OPCODE_TIMER_STATUS";break;}
      case CEC_OPCODE_STANDBY: {return "CEC_OPCODE_STANDBY";break;}
      case CEC_OPCODE_PLAY: {return "CEC_OPCODE_PLAY";break;}
/*       case CEC_OPCODE_DECK_CONTROL: {return "CEC_OPCODE_DECK_CONTROL";break;} */
      case CEC_OPCODE_TIMER_CLEARED_STATUS: {return "CEC_OPCODE_TIMER_CLEARED_STATUS";break;}
      case CEC_OPCODE_USER_CONTROL_PRESSED: {return "CEC_OPCODE_USER_CONTROL_PRESSED";break;}
      case CEC_OPCODE_USER_CONTROL_RELEASED: {return "CEC_OPCODE_USER_CONTROL_RELEASED";break;}
      case CEC_OPCODE_GIVE_OSD_NAME: {return "CEC_OPCODE_GIVE_OSD_NAME";break;}
      case CEC_OPCODE_SET_OSD_NAME: {return "CEC_OPCODE_SET_OSD_NAME";break;}
      case CEC_OPCODE_SET_OSD_STRING: {return "CEC_OPCODE_SET_OSD_STRING";break;}
      case CEC_OPCODE_SET_TIMER_PROGRAM_TITLE: {return "CEC_OPCODE_SET_TIMER_PROGRAM_TITLE";break;}
      case CEC_OPCODE_SYSTEM_AUDIO_MODE_REQUEST: {return "CEC_OPCODE_SYSTEM_AUDIO_MODE_REQUEST";break;}
      case CEC_OPCODE_GIVE_AUDIO_STATUS: {return "CEC_OPCODE_GIVE_AUDIO_STATUS";break;}
      case CEC_OPCODE_SET_SYSTEM_AUDIO_MODE: {return "CEC_OPCODE_SET_SYSTEM_AUDIO_MODE";break;}
      case CEC_OPCODE_REPORT_AUDIO_STATUS: {return "CEC_OPCODE_REPORT_AUDIO_STATUS";break;}
      case CEC_OPCODE_GIVE_SYSTEM_AUDIO_MODE_STATUS: {return "CEC_OPCODE_GIVE_SYSTEM_AUDIO_MODE_STATUS";break;}
      case CEC_OPCODE_SYSTEM_AUDIO_MODE_STATUS: {return "CEC_OPCODE_SYSTEM_AUDIO_MODE_STATUS";break;}
      case CEC_OPCODE_ROUTING_CHANGE: {return "CEC_OPCODE_ROUTING_CHANGE";break;}
      case CEC_OPCODE_ROUTING_INFORMATION: {return "CEC_OPCODE_ROUTING_INFORMATION";break;}
      case CEC_OPCODE_ACTIVE_SOURCE: {return "CEC_OPCODE_ACTIVE_SOURCE";break;}
      case CEC_OPCODE_GIVE_PHYSICAL_ADDRESS: {return "CEC_OPCODE_GIVE_PHYSICAL_ADDRESS";break;}
      case CEC_OPCODE_REPORT_PHYSICAL_ADDRESS: {return "CEC_OPCODE_REPORT_PHYSICAL_ADDRESS";break;}
      case CEC_OPCODE_REQUEST_ACTIVE_SOURCE: {return "CEC_OPCODE_REQUEST_ACTIVE_SOURCE";break;}
      case CEC_OPCODE_SET_STREAM_PATH: {return "CEC_OPCODE_SET_STREAM_PATH";break;}
      case CEC_OPCODE_DEVICE_VENDOR_ID: {return "CEC_OPCODE_DEVICE_VENDOR_ID";break;}
      case CEC_OPCODE_VENDOR_COMMAND: {return "CEC_OPCODE_VENDOR_COMMAND";break;}
      case CEC_OPCODE_VENDOR_REMOTE_BUTTON_DOWN: {return "CEC_OPCODE_VENDOR_REMOTE_BUTTON_DOWN";break;}
      case CEC_OPCODE_VENDOR_REMOTE_BUTTON_UP: {return "CEC_OPCODE_VENDOR_REMOTE_BUTTON_UP";break;}
      case CEC_OPCODE_GIVE_DEVICE_VENDOR_ID: {return "CEC_OPCODE_GIVE_DEVICE_VENDOR_ID";break;}
      case CEC_OPCODE_MENU_REQUEST: {return "CEC_OPCODE_MENU_REQUEST";break;}
      case CEC_OPCODE_MENU_STATUS: {return "CEC_OPCODE_MENU_STATUS";break;}
      case CEC_OPCODE_GIVE_DEVICE_POWER_STATUS: {return "CEC_OPCODE_GIVE_DEVICE_POWER_STATUS";break;}
      case CEC_OPCODE_REPORT_POWER_STATUS: {return "CEC_OPCODE_REPORT_POWER_STATUS";break;}
      case CEC_OPCODE_GET_MENU_LANGUAGE: {return "CEC_OPCODE_GET_MENU_LANGUAGE";break;}
      case CEC_OPCODE_SET_ANALOGUE_SERVICE: {return "CEC_OPCODE_SET_ANALOGUE_SERVICE";break;}
      case CEC_OPCODE_SET_DIGITAL_SERVICE: {return "CEC_OPCODE_SET_DIGITAL_SERVICE";break;}
      case CEC_OPCODE_SET_DIGITAL_TIMER: {return "CEC_OPCODE_SET_DIGITAL_TIMER";break;}
      case CEC_OPCODE_CLEAR_DIGITAL_TIMER: {return "CEC_OPCODE_CLEAR_DIGITAL_TIMER";break;}
      case CEC_OPCODE_SET_AUDIO_RATE: {return "CEC_OPCODE_SET_AUDIO_RATE";break;}
      case CEC_OPCODE_INACTIVE_SOURCE: {return "CEC_OPCODE_INACTIVE_SOURCE";break;}
      case CEC_OPCODE_CEC_VERSION: {return "CEC_OPCODE_CEC_VERSION";break;}
      case CEC_OPCODE_GET_CEC_VERSION: {return "CEC_OPCODE_GET_CEC_VERSION";break;}
      case CEC_OPCODE_VENDOR_COMMAND_WITH_ID: {return "CEC_OPCODE_VENDOR_COMMAND_WITH_ID";break;}
      case CEC_OPCODE_CLEAR_EXTERNAL_TIMER: {return "CEC_OPCODE_CLEAR_EXTERNAL_TIMER";break;}
      case CEC_OPCODE_SET_EXTERNAL_TIMER: {return "CEC_OPCODE_SET_EXTERNAL_TIMER";break;}
      case CEC_OPCODE_ABORT_MESSAGE: {return "CEC_OPCODE_ABORT_MESSAGE";break;}
      default : {return "unknown";break;}
      }
}


static char *cec_ioctl(int io)
{
   switch (io)
      {
      case CEC_VERBOSE_ON_CMD: {return "CEC_VERBOSE_ON_CMD";break;}
      case CEC_VERBOSE_OFF_CMD: {return "CEC_VERBOSE_OFF_CMD";break;}
      case CEC_BYEBYE_CMD: {return "CEC_BYEBYE_CMD";break;}
      case CEC_IOCTL_RX_ADDR_CMD: {return "CEC_IOCTL_RX_ADDR_CMD";break;}
      case CEC_IOCTL_PHY_ADDR_CMD: {return "CEC_IOCTL_PHY_ADDR_CMD";break;}
      case CEC_IOCTL_WAIT_FRAME_CMD: {return "CEC_IOCTL_WAIT_FRAME_CMD";break;}
      case CEC_IOCTL_ABORT_MSG_CMD: {return "CEC_IOCTL_ABORT_MSG_CMD";break;}
      case CEC_IOCTL_ACTIVE_SRC_CMD: {return "CEC_IOCTL_ACTIVE_SRC_CMD";break;}
      case CEC_IOCTL_VERSION_CMD: {return "CEC_IOCTL_VERSION_CMD";break;}
      case CEC_IOCTL_CLEAR_ANALOGUE_TIMER_CMD: {return "CEC_IOCTL_CLEAR_ANALOGUE_TIMER_CMD";break;}
      case CEC_IOCTL_CLEAR_DIGITAL_TIMER_CMD: {return "CEC_IOCTL_CLEAR_DIGITAL_TIMER_CMD";break;}
      case CEC_IOCTL_CLEAR_EXT_TIMER_WITH_EXT_PLUG_CMD: {return "CEC_IOCTL_CLEAR_EXT_TIMER_WITH_EXT_PLUG_CMD";break;}
      case CEC_IOCTL_CLEAR_EXT_TIMER_WITH_PHY_ADDR_CMD: {return "CEC_IOCTL_CLEAR_EXT_TIMER_WITH_PHY_ADDR_CMD";break;}
      case CEC_IOCTL_DECK_CTRL_CMD: {return "CEC_IOCTL_DECK_CTRL_CMD";break;}
      case CEC_IOCTL_DECK_STATUS_CMD: {return "CEC_IOCTL_DECK_STATUS_CMD";break;}
      case CEC_IOCTL_DEVICE_VENDOR_ID_CMD: {return "CEC_IOCTL_DEVICE_VENDOR_ID_CMD";break;}
      case CEC_IOCTL_FEATURE_ABORT_CMD: {return "CEC_IOCTL_FEATURE_ABORT_CMD";break;}
      case CEC_IOCTL_GET_CEC_VERSION_CMD: {return "CEC_IOCTL_GET_CEC_VERSION_CMD";break;}
      case CEC_IOCTL_GET_MENU_LANGUAGE_CMD: {return "CEC_IOCTL_GET_MENU_LANGUAGE_CMD";break;}
      case CEC_IOCTL_GIVE_AUDIO_STATUS_CMD: {return "CEC_IOCTL_GIVE_AUDIO_STATUS_CMD";break;}
      case CEC_IOCTL_GIVE_DECK_STATUS_CMD: {return "CEC_IOCTL_GIVE_DECK_STATUS_CMD";break;}
      case CEC_IOCTL_GIVE_DEVICE_POWER_STATUS_CMD: {return "CEC_IOCTL_GIVE_DEVICE_POWER_STATUS_CMD";break;}
      case CEC_IOCTL_GIVE_DEVICE_VENDOR_ID_CMD: {return "CEC_IOCTL_GIVE_DEVICE_VENDOR_ID_CMD";break;}
      case CEC_IOCTL_GIVE_OSD_NAME_CMD: {return "CEC_IOCTL_GIVE_OSD_NAME_CMD";break;}
      case CEC_IOCTL_GIVE_PHY_ADDR_CMD: {return "CEC_IOCTL_GIVE_PHY_ADDR_CMD";break;}
      case CEC_IOCTL_GIVE_SYS_AUDIO_MODE_STATUS_CMD: {return "CEC_IOCTL_GIVE_SYS_AUDIO_MODE_STATUS_CMD";break;}
      case CEC_IOCTL_GIVE_TUNER_DEVICE_STATUS_CMD: {return "CEC_IOCTL_GIVE_TUNER_DEVICE_STATUS_CMD";break;}
      case CEC_IOCTL_IMAGE_VIEW_ON_CMD: {return "CEC_IOCTL_IMAGE_VIEW_ON_CMD";break;}
      case CEC_IOCTL_INACTIVE_SRC_CMD: {return "CEC_IOCTL_INACTIVE_SRC_CMD";break;}
      case CEC_IOCTL_MENU_REQUEST_CMD: {return "CEC_IOCTL_MENU_REQUEST_CMD";break;}
      case CEC_IOCTL_MENU_STATUS_CMD: {return "CEC_IOCTL_MENU_STATUS_CMD";break;}
      case CEC_IOCTL_PLAY_CMD: {return "CEC_IOCTL_PLAY_CMD";break;}
      case CEC_IOCTL_POLLING_MSG_CMD: {return "CEC_IOCTL_POLLING_MSG_CMD";break;}
      case CEC_IOCTL_REC_OFF_CMD: {return "CEC_IOCTL_REC_OFF_CMD";break;}
      case CEC_IOCTL_REC_ON_ANALOGUE_SERVICE_CMD: {return "CEC_IOCTL_REC_ON_ANALOGUE_SERVICE_CMD";break;}
      case CEC_IOCTL_REC_ON_DIGITAL_SERVICE_CMD: {return "CEC_IOCTL_REC_ON_DIGITAL_SERVICE_CMD";break;}
      case CEC_IOCTL_REC_ON_EXT_PHY_ADDR_CMD: {return "CEC_IOCTL_REC_ON_EXT_PHY_ADDR_CMD";break;}
      case CEC_IOCTL_REC_ON_EXT_PLUG_CMD: {return "CEC_IOCTL_REC_ON_EXT_PLUG_CMD";break;}
      case CEC_IOCTL_REC_ON_OWN_SRC_CMD: {return "CEC_IOCTL_REC_ON_OWN_SRC_CMD";break;}
      case CEC_IOCTL_REC_STATUS_CMD: {return "CEC_IOCTL_REC_STATUS_CMD";break;}
      case CEC_IOCTL_REC_TV_SCREEN_CMD: {return "CEC_IOCTL_REC_TV_SCREEN_CMD";break;}
      case CEC_IOCTL_REPORT_AUDIO_STATUS_CMD: {return "CEC_IOCTL_REPORT_AUDIO_STATUS_CMD";break;}
      case CEC_IOCTL_REPORT_PHY_ADDR_CMD: {return "CEC_IOCTL_REPORT_PHY_ADDR_CMD";break;}
      case CEC_IOCTL_REPORT_POWER_STATUS_CMD: {return "CEC_IOCTL_REPORT_POWER_STATUS_CMD";break;}
      case CEC_IOCTL_REQUEST_ACTIVE_SRC_CMD: {return "CEC_IOCTL_REQUEST_ACTIVE_SRC_CMD";break;}
      case CEC_IOCTL_ROUTING_CHANGE_CMD: {return "CEC_IOCTL_ROUTING_CHANGE_CMD";break;}
      case CEC_IOCTL_ROUTING_INFORMATION_CMD: {return "CEC_IOCTL_ROUTING_INFORMATION_CMD";break;}
      case CEC_IOCTL_SELECT_ANALOGUE_SERVICE_CMD: {return "CEC_IOCTL_SELECT_ANALOGUE_SERVICE_CMD";break;}
      case CEC_IOCTL_SELECT_DIGITAL_SERVICE_CMD: {return "CEC_IOCTL_SELECT_DIGITAL_SERVICE_CMD";break;}
      case CEC_IOCTL_SET_ANALOGUE_TIMER_CMD: {return "CEC_IOCTL_SET_ANALOGUE_TIMER_CMD";break;}
      case CEC_IOCTL_SET_AUDIO_RATE_CMD: {return "CEC_IOCTL_SET_AUDIO_RATE_CMD";break;}
      case CEC_IOCTL_SET_DIGITAL_TIMER_CMD: {return "CEC_IOCTL_SET_DIGITAL_TIMER_CMD";break;}
      case CEC_IOCTL_SET_EXT_TIMER_WITH_EXT_PLUG_CMD: {return "CEC_IOCTL_SET_EXT_TIMER_WITH_EXT_PLUG_CMD";break;}
      case CEC_IOCTL_SET_EXT_TIMER_WITH_PHY_ADDR_CMD: {return "CEC_IOCTL_SET_EXT_TIMER_WITH_PHY_ADDR_CMD";break;}
      case CEC_IOCTL_SET_MENU_LANGUAGE_CMD: {return "CEC_IOCTL_SET_MENU_LANGUAGE_CMD";break;}
      case CEC_IOCTL_SET_OSD_NAME_CMD: {return "CEC_IOCTL_SET_OSD_NAME_CMD";break;}
      case CEC_IOCTL_SET_OSD_STRING_CMD: {return "CEC_IOCTL_SET_OSD_STRING_CMD";break;}
      case CEC_IOCTL_SET_STREAM_PATH_CMD: {return "CEC_IOCTL_SET_STREAM_PATH_CMD";break;}
      case CEC_IOCTL_SET_SYS_AUDIO_MODE_CMD: {return "CEC_IOCTL_SET_SYS_AUDIO_MODE_CMD";break;}
      case CEC_IOCTL_SET_TIMER_PROGRAM_TITLE_CMD: {return "CEC_IOCTL_SET_TIMER_PROGRAM_TITLE_CMD";break;}
      case CEC_IOCTL_STANDBY_CMD: {return "CEC_IOCTL_STANDBY_CMD";break;}
      case CEC_IOCTL_SYS_AUDIO_MODE_REQUEST_CMD: {return "CEC_IOCTL_SYS_AUDIO_MODE_REQUEST_CMD";break;}
      case CEC_IOCTL_SYS_AUDIO_MODE_STATUS_CMD: {return "CEC_IOCTL_SYS_AUDIO_MODE_STATUS_CMD";break;}
      case CEC_IOCTL_TEXT_VIEW_ON_CMD: {return "CEC_IOCTL_TEXT_VIEW_ON_CMD";break;}
      case CEC_IOCTL_TIMER_CLEARED_STATUS_CMD: {return "CEC_IOCTL_TIMER_CLEARED_STATUS_CMD";break;}
      case CEC_IOCTL_TIMER_STATUS_CMD: {return "CEC_IOCTL_TIMER_STATUS_CMD";break;}
      case CEC_IOCTL_TUNER_DEVICE_STATUS_ANALOGUE_CMD: {return "CEC_IOCTL_TUNER_DEVICE_STATUS_ANALOGUE_CMD";break;}
      case CEC_IOCTL_TUNER_DEVICE_STATUS_DIGITAL_CMD: {return "CEC_IOCTL_TUNER_DEVICE_STATUS_DIGITAL_CMD";break;}
      case CEC_IOCTL_TUNER_STEP_DECREMENT_CMD: {return "CEC_IOCTL_TUNER_STEP_DECREMENT_CMD";break;}
      case CEC_IOCTL_TUNER_STEP_INCREMENT_CMD: {return "CEC_IOCTL_TUNER_STEP_INCREMENT_CMD";break;}
      case CEC_IOCTL_USER_CTRL_CMD: {return "CEC_IOCTL_USER_CTRL_CMD";break;}
      case CEC_IOCTL_USER_CTRL_PLAY_CMD: {return "CEC_IOCTL_USER_CTRL_PLAY_CMD";break;}
      case CEC_IOCTL_USER_CTRL_SELECT_AUDIOINPUT_CMD: {return "CEC_IOCTL_USER_CTRL_SELECT_AUDIOINPUT_CMD";break;}
      case CEC_IOCTL_USER_CTRL_SELECT_AVINPUT_CMD: {return "CEC_IOCTL_USER_CTRL_SELECT_AVINPUT_CMD";break;}
      case CEC_IOCTL_USER_CTRL_SELECT_MEDIA_CMD: {return "CEC_IOCTL_USER_CTRL_SELECT_MEDIA_CMD";break;}
      case CEC_IOCTL_USER_CTRL_TUNE_CMD: {return "CEC_IOCTL_USER_CTRL_TUNE_CMD";break;}
      case CEC_IOCTL_USER_CTRL_RELEASED_CMD: {return "CEC_IOCTL_USER_CTRL_RELEASED_CMD";break;}
      case CEC_IOCTL_VENDOR_COMMAND_CMD: {return "CEC_IOCTL_VENDOR_COMMAND_CMD";break;}
      case CEC_IOCTL_VENDOR_COMMAND_WITH_ID_CMD: {return "CEC_IOCTL_VENDOR_COMMAND_WITH_ID_CMD";break;}
      case CEC_IOCTL_VENDOR_REMOTE_BUTTON_DOWN_CMD: {return "CEC_IOCTL_VENDOR_REMOTE_BUTTON_DOWN_CMD";break;}
      case CEC_IOCTL_VENDOR_REMOTE_BUTTON_UP_CMD: {return "CEC_IOCTL_VENDOR_REMOTE_BUTTON_UP_CMD";break;}
      case CEC_IOCTL_GET_SW_VERSION_CMD: {return "CEC_IOCTL_GET_SW_VERSION_CMD";break;}
      case CEC_IOCTL_SET_POWER_STATE_CMD: {return "CEC_IOCTL_SET_POWER_STATE_CMD";break;}
      case CEC_IOCTL_GET_POWER_STATE_CMD: {return "CEC_IOCTL_GET_POWER_STATE_CMD";break;}
      case CEC_IOCTL_INSTANCE_CONFIG_CMD: {return "CEC_IOCTL_INSTANCE_CONFIG_CMD";break;}
      case CEC_IOCTL_INSTANCE_SETUP_CMD: {return "CEC_IOCTL_INSTANCE_SETUP_CMD";break;}
      case CEC_IOCTL_GET_INSTANCE_SETUP_CMD: {return "CEC_IOCTL_GET_INSTANCE_SETUP_CMD";break;}
      case CEC_IOCTL_ENABLE_EVENT_CMD: {return "CEC_IOCTL_ENABLE_EVENT_CMD";break;}
      case CEC_IOCTL_DISABLE_EVENT_CMD: {return "CEC_IOCTL_DISABLE_EVENT_CMD";break;}
      case CEC_IOCTL_ENABLE_CALIBRATION_CMD: {return "CEC_IOCTL_ENABLE_CALIBRATION_CMD";break;}
      case CEC_IOCTL_DISABLE_CALIBRATION_CMD: {return "CEC_IOCTL_DISABLE_CALIBRATION_CMD";break;}
      case CEC_IOCTL_SEND_MSG_CMD: {return "CEC_IOCTL_SEND_MSG_CMD";break;}
      case CEC_IOCTL_SET_REGISTER_CMD: {return "CEC_IOCTL_SET_REGISTER_CMD";break;}

      case CEC_IOCTL_GET_RAW_MODE_CMD: {return "CEC_IOCTL_GET_RAW_MODE_CMD";break;}
      case CEC_IOCTL_SET_RAW_MODE_CMD: {return "CEC_IOCTL_SET_RAW_MODE_CMD";break;}
      case CEC_IOCTL_GET_RAW_INFO_CMD: {return "CEC_IOCTL_GET_RAW_INFO_CMD";break;}
      case CEC_IOCTL_SET_RX_ADDR_MASK_CMD: {return "CEC_IOCTL_SET_RX_ADDR_MASK_CMD";break;}

      default : {return "unknown";break;}
      }
}


static char *cec_rxstatus(int s)
{
   switch (s)
      {
      case CEC_MSG_SUCCESS :{return "success";break;}
      case CEC_MSG_FAIL_DATA_NOT_ACK :{return "data not ack";break;}
      case CEC_CSP_OFF_STATE :{return "CSP off";break;}
      case CEC_BAD_REQ_SERVICE :{return "bad Req";break;}
      case CEC_MSG_FAIL_UNABLE_TO_ACCESS :{return "CEC line error";break;}
      case CEC_MSG_FAIL_ARBITRATION_ERROR :{return "arb error";break;}
      case CEC_MSG_FAIL_BIT_TIMMING_ERROR :{return "bit error";break;}
      case CEC_MSG_FAIL_DEST_NOT_ACK :{return "destination not ack";break;}
      default : {return "unknown";break;}
      }
}


static unsigned char get_next_logical_addr(cec_device_type device,unsigned char la)
{
   switch (device) {
   case CEC_DEVICE_TYPE_TV:
      switch (la) {
      case CEC_LOGICAL_ADDRESS_TV:
         return CEC_LOGICAL_ADDRESS_UNREGISTRED_BROADCAST;
      default:
         return CEC_LOGICAL_ADDRESS_TV;
      }
   case CEC_DEVICE_TYPE_REC_DEVICE:
      switch (la) {
      case CEC_LOGICAL_ADDRESS_RECORDING_DEVICE_1:
         return CEC_LOGICAL_ADDRESS_RECORDING_DEVICE_2;
      case CEC_LOGICAL_ADDRESS_RECORDING_DEVICE_2:
         return CEC_LOGICAL_ADDRESS_RECORDING_DEVICE_3;
      case CEC_LOGICAL_ADDRESS_RECORDING_DEVICE_3:
         return CEC_LOGICAL_ADDRESS_UNREGISTRED_BROADCAST;
      default:
         return CEC_LOGICAL_ADDRESS_RECORDING_DEVICE_1;
      }
   case CEC_DEVICE_TYPE_TUNER:
      switch (la) {
      case CEC_LOGICAL_ADDRESS_TUNER_1:
         return CEC_LOGICAL_ADDRESS_TUNER_2;
      case CEC_LOGICAL_ADDRESS_TUNER_2:
         return CEC_LOGICAL_ADDRESS_TUNER_3;
      case CEC_LOGICAL_ADDRESS_TUNER_3:
         return CEC_LOGICAL_ADDRESS_TUNER_4;
      case CEC_LOGICAL_ADDRESS_TUNER_4:
         return CEC_LOGICAL_ADDRESS_UNREGISTRED_BROADCAST;
      default:
         return CEC_LOGICAL_ADDRESS_TUNER_1;
      }
   case CEC_DEVICE_TYPE_PLAYBACK_DEVICE:
      switch (la) {
      case CEC_LOGICAL_ADDRESS_PLAYBACK_DEVICE_1:
         return CEC_LOGICAL_ADDRESS_PLAYBACK_DEVICE_2;
      case CEC_LOGICAL_ADDRESS_PLAYBACK_DEVICE_2:
         return CEC_LOGICAL_ADDRESS_PLAYBACK_DEVICE_3;
      case CEC_LOGICAL_ADDRESS_PLAYBACK_DEVICE_3:
         return CEC_LOGICAL_ADDRESS_UNREGISTRED_BROADCAST;
      default:
         return CEC_LOGICAL_ADDRESS_PLAYBACK_DEVICE_1;
      }
   case CEC_DEVICE_TYPE_AUDIO_DEVICE: 
      switch (la) {
      case CEC_LOGICAL_ADDRESS_AUDIO_SYSTEM:
         return CEC_LOGICAL_ADDRESS_UNREGISTRED_BROADCAST;
      default:
         return CEC_LOGICAL_ADDRESS_AUDIO_SYSTEM;
      }
   default:
      return CEC_LOGICAL_ADDRESS_UNREGISTRED_BROADCAST;
   }
}

static int device_type(int type)
{
   printk(KERN_INFO "hdmicec declared as a ");
   switch (type) {
   case CEC_DEVICE_TYPE_TV: 
      printk("TV");
      break;
   case CEC_DEVICE_TYPE_REC_DEVICE: 
      printk("record");
      break;
   case CEC_DEVICE_TYPE_TUNER: 
      printk("tuner");
      break;
   case CEC_DEVICE_TYPE_PLAYBACK_DEVICE: 
      printk("playback");
      break;
   case CEC_DEVICE_TYPE_AUDIO_DEVICE: 
      printk("audio");
      break;
   default: 
      printk("default (playback)");
      type = CEC_DEVICE_TYPE_PLAYBACK_DEVICE;
      break;
   }
   printk(" device type\n");
   return type;
}


/*
 *
 * PROCESSING
 * ----------
 * LEVEL 2
 *
 */


/*
 *  CEC Power On
 */
static void cec_on(cec_instance *this)
{
   int err;
   struct task_struct *tsk = current;

   disable_irq(gpio_to_irq(TDA_IRQ_CALIB));

   /* turn GPIO into calib pulse generator */
   err = gpio_direction_output(TDA_IRQ_CALIB,0); /* output (1 means try-state or high) */
   if (err < 0) {
	LOG(KERN_ERR,"Cannot access GPIO %d, err:%d\n",TDA_IRQ_CALIB,err);

	enable_irq(gpio_to_irq(TDA_IRQ_CALIB));
	return;
   }

   __gpio_set_value(TDA_IRQ_CALIB,1);

   this->cec.power = tmPowerOn;
   TRY(tmdlHdmiCecSetPowerState(this->cec.inst,this->cec.power));

   this->cec.clock = TMDL_HDMICEC_CLOCK_FRO;
   TRY(tmdlHdmiCecEnableCalibration(this->cec.inst,this->cec.clock));
   msleep(10);
   set_current_state(TASK_UNINTERRUPTIBLE);

   /* CAUTION : TDA needs a real 10ms pulse */
   cpu_relax();
   spin_lock_irq(&tsk->sighand->siglock);
   __gpio_set_value(TDA_IRQ_CALIB,0);
   __udelay(10000);
   __gpio_set_value(TDA_IRQ_CALIB,1);
   spin_unlock_irq(&tsk->sighand->siglock);

   msleep(10);
   TRY(tmdlHdmiCecDisableCalibration(this->cec.inst));

   /* setup */ 
   TRY(tmdlHdmiCecGetInstanceSetup(this->cec.inst,&this->cec.setup));
   this->cec.setup.DeviceLogicalAddress = this->cec.rx_addr;
   this->cec.clock = TMDL_HDMICEC_CLOCK_FRO;
   this->cec.setup.cecClockSource = this->cec.clock;
   TRY(tmdlHdmiCecInstanceSetup(this->cec.inst,&this->cec.setup));

   LOG(KERN_INFO,"standby --> on\n");

 TRY_DONE:
   /* turn GPIO into IRQ */
   gpio_direction_input(TDA_IRQ_CALIB);
   enable_irq(gpio_to_irq(TDA_IRQ_CALIB));
}

/*
 *  CEC Power Off
 */
static void cec_standby(cec_instance *this)
{
   int err;

   this->cec.power = tmPowerStandby;
   TRY(tmdlHdmiCecSetPowerState(this->cec.inst,this->cec.power));
   
   LOG(KERN_INFO,"on --> standby\n");

 TRY_DONE:
   (void)0;
}

/*
 * enable listening on a single logical addresses
 */
static void cec_listen_single(cec_instance *this, unsigned char rx_addr)
{
   int err;

   LOG(KERN_INFO,"logAddr set to 0x%x\n", rx_addr);

   TRY(tmdlHdmiCecSetLogicalAddress(this->cec.inst, rx_addr));

   this->cec.rx_addr = rx_addr;
   this->cec.rx_addr_mask = (1 << rx_addr) & 0x7fff;

 TRY_DONE:
   (void)0;
}

/*
 * enable listening on multiple logical addresses
 */
static void cec_listen_multi(cec_instance *this, 
                             unsigned short onMask, unsigned short offMask)
{
   int err;
   unsigned short newMask, changeMask;

   newMask = (this->cec.rx_addr_mask | onMask) & ~(offMask | 0x8000);
   changeMask = newMask ^ this->cec.rx_addr_mask; 

   if (changeMask & 0xff00) {
       TRY(tmdlHdmiCecSetRegister(this->cec.inst, E_REG_ACKH, newMask >> 8));
       this->cec.rx_addr_mask ^= changeMask & 0xff00;
   } 

   if (changeMask & 0x80ff) {
       TRY(tmdlHdmiCecSetRegister(this->cec.inst, E_REG_ACKL, newMask & 0xff));
       this->cec.rx_addr_mask ^= changeMask & 0x00ff;
   }

   LOG(KERN_INFO,"logAddrMask is now 0x%04x\n", this->cec.rx_addr_mask);

 TRY_DONE:
   (void)0;
}

/*
 *  CEC interrupt polling
 */
static void cec_interrupt(struct work_struct *dummy)
{
   cec_instance *this=&our_instance;
   cec_power display_active = get_hpd_status();
   unsigned short new_phy_addr = edid_phy_addr();
   int err=0;
   
   LOG(KERN_INFO,"called\n");
   
   /* sync with this_cdev_write */
   down(&this->driver.sem);


   /* switch on/off CEC */
   if (!display_active && (this->cec.power == tmPowerOn)) {
      this->cec.source_status = CEC_POWER_STATUS_STANDBY;
/*       TRY(tmdlHdmiCecInactiveSource(this->cec.inst,             \ */
/*                                        this->cec.initiator,     \ */
/*                                        this->cec.phy_addr)); */
      cec_standby(this);
   }
   else if (display_active && (this->cec.power == tmPowerStandby)) {
      /* send active msg when hdmi has been abled */
      cec_on(this);

#ifndef GUI_OVER_HDMI
      this->cec.source_status = CEC_POWER_STATUS_ON;
#endif
   }
   /* new phy addr means new EDID, mean HPD ! */
   else if ((this->cec.phy_addr != new_phy_addr) &&
           (this->cec.source_status == CEC_POWER_STATUS_ON)) {
      LOG(KERN_INFO,"New physical address %04x\n",new_phy_addr);
      this->cec.phy_addr = new_phy_addr;

      if( !this->param.passive) {
         if (!this->driver.raw_mode) {
            if (this->cec.phy_addr != 0xFFFF) {
               this->cec.rx_addr = get_next_logical_addr(
                  this->cec.device_type, CEC_LOGICAL_ADDRESS_UNREGISTRED_BROADCAST);
               TRY(tmdlHdmiCecPollingMessage(this->cec.inst, this->cec.rx_addr));
            }
            else {
               this->cec.rx_addr = CEC_LOGICAL_ADDRESS_UNREGISTRED_BROADCAST;
            }
         } 
         else {
            // Todo: how to notify libCEC about this ??
         }
      }
   }
#ifdef GUI_OVER_HDMI
   else if (edid_received()) { /* Check me */
      if (this->cec.source_status == CEC_POWER_STATUS_STANDBY) {
         LOG(KERN_INFO,"EDID received in CEC_POWER_STATUS_STANDBY\n");

         if( !this->param.passive) {
            /* only for GFX on HDMI, do not use if only video playback on HDMI */
            TRY(tmdlHdmiCecImageViewOn(this->cec.inst,this->cec.initiator));
            TRY(tmdlHdmiCecHandleInterrupt(this->cec.inst));
            msleep(200);
            TRY(tmdlHdmiCecActiveSource(this->cec.inst,this->cec.phy_addr));
         }
         
         this->cec.source_status = CEC_POWER_STATUS_ON;
         goto TRY_DONE;
      }
   }
#endif
   
#if 0
   if (this->cec.phy_addr != 0xFFFF) {

      /* claim source status */
      if ((get_hdmi_status() ==  tmPowerStandby) &&             \
          (this->cec.source_status == CEC_POWER_STATUS_ON)) {
         /* send inactive msg when hdmi has been disabled */
         this->cec.source_status = CEC_POWER_STATUS_STANDBY;
         TRY(tmdlHdmiCecInactiveSource(this->cec.inst,          \
                                       this->cec.initiator,     \
                                       this->cec.phy_addr));
      }
      else if ((get_hdmi_status() ==  tmPowerOn) &&                     \
               (this->cec.source_status == CEC_POWER_STATUS_STANDBY)) {
         /* send active msg when hdmi has been abled */
         this->cec.source_status = CEC_POWER_STATUS_ON;
         TRY(tmdlHdmiCecActiveSource(this->cec.inst,        \
                                     this->cec.phy_addr));
      }
/*       printk(KERN_INFO "DBG phd_status:%s cec.power:%s\n", \ */
/*              get_hpd_status()?"Active":"Inactive",                                      \ */
/*              (this->cec.power==tmPowerOn)?"On":"Standby"); */
   }
#endif

   /* internal handeling */
   TRY(tmdlHdmiCecHandleInterrupt(this->cec.inst));
    
 TRY_DONE:

   /* sync with this_cdev_write */
   up(&this->driver.sem);

   /* setup next tick */
   if (!this->driver.deinit_req) {
      /* setup next polling */
#ifndef IRQ
/*       this->driver.timer.expires = jiffies + ( CHECK_EVERY_XX_MS * HZ / 1000 ); */
/*       add_timer(&this->driver.timer); */
      mod_timer(&this->driver.timer,jiffies + ( CHECK_EVERY_XX_MS * HZ / 1000 ));
#endif
   }
   else {
      this->driver.deinit_req++;
      wake_up_interruptible(&this->driver.wait);
   }
}

#ifndef IRQ
static DECLARE_WORK(wq_name, cec_interrupt);

void polling_timeout(unsigned long arg)
{

#if 0
   /* fake frame for equipement-less testing */

      cec_instance *this=&our_instance;

      if (this->driver.timer.data++>1000) {
         printk(KERN_INFO "Fake Rx message\n");
         this->driver.timer.data=0;

         this->cec.frame.size = 4;
         this->cec.frame.addr = 4; /* 0-->4 (TV-->MediaPlayer1) */
         this->cec.frame.data[0]=0x46; /* opcode: "GiveOsd" */
         this->cec.frame.service = CEC_RX_DONE;
         
         this->driver.poll_done = true;
         wake_up_interruptible(&this->driver.wait);
      }
#endif

   /* derefered because ATOMIC context of timer does not support I2C_transfert */
   schedule_work(&wq_name);

}
#endif

#ifndef IRQ
/*
 *  TDA irq
 */
static irqreturn_t tda_irq(int irq, void *_udc)
{
   cec_instance *this=&our_instance;
   /*    printk(KERN_INFO "DBG caught irq:%d\n",irq); */
 
   /* do it now */
   mod_timer(&this->driver.timer,jiffies);

   return IRQ_HANDLED;
}
#endif

#ifdef TWL4030_HACK
/*
 *  User Control
 */
static void user_control(int key, int press)
{
   input_report_key(gkp_input, key, press);
   input_sync(gkp_input);
   msleep(20);
   input_report_key(gkp_input, key, 0);
   input_sync(gkp_input);
}
#endif


/*
 *  CEC callback
 */
static void eventCallbackCEC(tmdlHdmiCecEvent_t event, unsigned char *data, unsigned char length)
{
   int err=0;
   cec_instance *this=&our_instance;
   int opcode;
   int initiator,receiver;
   cec_frame *frame;
   int new_tail;

   if (this->driver.raw_mode) {
      new_tail = (this->driver.read_queue_tail + 1) %
                     ARRAY_SIZE(this->driver.read_queue);

      frame = &this->driver.read_queue[this->driver.read_queue_tail];
      memset(frame, 0, sizeof(frame[0]));

      if (event == TMDL_HDMICEC_CALLBACK_MESSAGE_AVAILABLE) {

         if (new_tail != this->driver.read_queue_head) {
            frame->size    = length + 2;		/* sizeof(size) + sizeof(service) */
            frame->service = CEC_RX_PKT;		/* this is an rx packet */
            frame->addr    = data[0];			/* AddressByte */
            memcpy(frame->data, &data[1], length - 1);	/* DataBytes[], length - sizeof(addr) */

            initiator = (frame->addr >> 4) & 0x0F;
            receiver  = frame->addr & 0x0F;
            opcode    = frame->data[0];
            LOG(KERN_INFO,"hdmicec:Rx:[%x--->%x] %s length:%d [%02x %02x %02x %02x]\n",
               initiator,receiver,length > 1 ? cec_opcode(opcode) : "POLL",length, 
               frame->data[0], frame->data[1], frame->data[2], frame->data[3]);

            this->driver.read_queue_tail = new_tail;
         }

         wake_up_interruptible(&this->driver.wait_read);

      } else if (event == TMDL_HDMICEC_CALLBACK_STATUS) {

         if (new_tail != this->driver.read_queue_head) {
            frame->size    = length + 2;		/* sizeof(size) + sizeof(service) */
            frame->service = CEC_ACK_PKT;		/* this is an ACK/NAK packet */
            frame->addr    = data[0];			/* AddressByte */
            memcpy(frame->data, &data[1], length - 1);	/* DataBytes[], length - sizeof(addr) */

            initiator = (frame->addr >> 4) & 0x0F;
            receiver  = frame->addr & 0x0F;
            opcode    = frame->data[0];
            LOG(KERN_INFO,"hdmicec:ACK:[%x--->%x] %s lenght:%d [%02x %02x %02x %02x]\n",
	      initiator,receiver,cec_rxstatus(opcode),length, 
              frame->data[0], frame->data[1], frame->data[2], frame->data[3]);

            this->driver.read_queue_tail = new_tail;
         }

	 this->driver.write_pending = 0;
         wake_up_interruptible(&this->driver.wait_read);
         wake_up_interruptible(&this->driver.wait_write);

      } else {
         LOG(KERN_ERR,"Oups ! Callback got invalid event %d !\n",event);
      }

      return;
   }

   if (this->param.passive)
      return;


   if (event == TMDL_HDMICEC_CALLBACK_MESSAGE_AVAILABLE) {

      this->cec.frame.size = length + 2;
      this->cec.frame.addr = data[0]; /* .AddressByte */
      initiator = (this->cec.frame.addr >> 4) & 0x0F;
      this->cec.initiator = initiator;
      receiver = this->cec.frame.addr & 0x0F;
      memcpy(&this->cec.frame.data,&data[1],length-1); /* .DataBytes[], length - siezof(length,addr,ack) */
      opcode=this->cec.frame.data[0];
      LOG(KERN_INFO,"hdmicec:Rx:[%x--->%x] %s length:%d addr:%d %02x%02x%02x%02x\n",initiator,receiver,cec_opcode(opcode), \
          length,
          this->cec.frame.addr,
          this->cec.frame.data[0],                                      \
          this->cec.frame.data[1],                                      \
          this->cec.frame.data[2],                                      \
          this->cec.frame.data[3]);
      this->cec.frame.service = CEC_RX_DONE;

      msleep(20);

      /* automatic answering */
      switch (opcode) {
      case CEC_OPCODE_GIVE_PHYSICAL_ADDRESS:
         TRY(tmdlHdmiCecReportPhysicalAddress(this->cec.inst,           \
                                              this->cec.phy_addr,       \
                                              this->cec.device_type));
         break;
      case CEC_OPCODE_GET_CEC_VERSION:
         TRY(tmdlHdmiCecVersion(this->cec.inst,                         \
                                this->cec.initiator,                    \
                                this->cec.version));
         break;   
      case CEC_OPCODE_GIVE_OSD_NAME:
         TRY(tmdlHdmiCecSetOsdName(this->cec.inst,         \
                                   this->cec.initiator,        \
                                   this->cec.osd_name.data,         \
                                   this->cec.osd_name.length));
         break;
      case CEC_OPCODE_GIVE_DEVICE_VENDOR_ID:
         TRY(tmdlHdmiCecDeviceVendorID(this->cec.inst,              \
                                       this->cec.vendor_id));
         break;
      case CEC_OPCODE_REQUEST_ACTIVE_SOURCE:
         if (this->cec.source_status == CEC_POWER_STATUS_ON) {
            if (this->cec.initiator != 0x0F) {
               if (receiver == 0x0F) {
                  TRY(tmdlHdmiCecActiveSource(this->cec.inst,this->cec.phy_addr));
               }
            }
         }
         break;
      case CEC_OPCODE_ACTIVE_SOURCE:
         if (this->cec.source_status == CEC_POWER_STATUS_ON) {
            this->cec.source_status = CEC_POWER_STATUS_STANDBY;
            hdmi_disable(1);
            this->cec.power = tmPowerOn;
            TRY(tmdlHdmiCecSetPowerState(this->cec.inst,this->cec.power)); /* keeps CEC alive */
         }
         break;
      case CEC_OPCODE_GIVE_DEVICE_POWER_STATUS:
         TRY(tmdlHdmiCecReportPowerStatus(this->cec.inst,               \
                                          this->cec.initiator,          \
                                          this->cec.source_status));
         break;
      case CEC_OPCODE_STANDBY:
         /* mind recording device can only be stopped by appli */ 
         if (this->cec.device_type != CEC_DEVICE_TYPE_REC_DEVICE) {
            this->cec.source_status = CEC_POWER_STATUS_STANDBY;
            hdmi_disable(1);
            this->cec.power = tmPowerOn;
            TRY(tmdlHdmiCecSetPowerState(this->cec.inst,this->cec.power));  /* keeps CEC alive */
         }
         break;
      case CEC_OPCODE_ROUTING_INFORMATION:
      case CEC_OPCODE_SET_STREAM_PATH:
         /* wake-up if called */
         if (this->cec.phy_addr == (((int)this->cec.frame.data[1] << 8) + this->cec.frame.data[2])) {
            if (this->cec.source_status != CEC_POWER_STATUS_ON) {
               this->cec.source_status = CEC_POWER_STATUS_ON;
               hdmi_enable();
            }
            TRY(tmdlHdmiCecActiveSource(this->cec.inst,this->cec.phy_addr));
         }
         break;
/*       case /\* NEW DECK ??? *\/ */
      case CEC_OPCODE_ROUTING_CHANGE:
         /* wake-up if called */
         if (this->cec.phy_addr == (((int)this->cec.frame.data[3] << 8) + this->cec.frame.data[4])) {
            if (this->cec.source_status != CEC_POWER_STATUS_ON) {
               this->cec.source_status = CEC_POWER_STATUS_ON;
               hdmi_enable();
            }
            TRY(tmdlHdmiCecActiveSource(this->cec.inst,this->cec.phy_addr));
         }
         break;
      case CEC_OPCODE_ABORT_MESSAGE:
         if (this->cec.phy_addr == (((int)this->cec.frame.data[3] << 8) + this->cec.frame.data[4])) {
            TRY(tmdlHdmiCecFeatureAbort(this->cec.inst, \
                                        this->cec.initiator, \
                                        this->cec.feature_abort.FeatureOpcode, \
                                        this->cec.feature_abort.AbortReason));
         }
         break;
      case CEC_OPCODE_MENU_REQUEST:
#ifdef TWL4030_HACK
         this->cec.menu_status = CEC_MENU_STATE_ACTIVATE;
         TRY(tmdlHdmiCecMenuStatus(this->cec.inst, \
                                   this->cec.initiator, \
                                   this->cec.menu_status));
         break;
#endif
      case CEC_OPCODE_USER_CONTROL_PRESSED:
         switch (this->cec.frame.data[1]) {
#ifdef TWL4030_HACK /* AL : hack to bypass keypad */
         case CEC_REMOTE_BUTTON_SELECT:
            user_control(353,64);
            break;
         case CEC_REMOTE_BUTTON_UP:
            user_control(103,128);
            break;
         case CEC_REMOTE_BUTTON_DOWN:
            user_control(108,128);
            break;
         case CEC_REMOTE_BUTTON_LEFT:
            user_control(105,128);
            break;
         case CEC_REMOTE_BUTTON_RIGHT:
            user_control(106,128);
            break;
         case CEC_REMOTE_BUTTON_EXIT:
            user_control(14,8);
            break;
#endif
         case CEC_REMOTE_BUTTON_POWER:
            this->cec.source_status = CEC_POWER_STATUS_ON;
            hdmi_enable();
            break;
         default:
            this->cec.feature_abort.FeatureOpcode=opcode;
            this->cec.feature_abort.AbortReason=CEC_ABORT_INVALID_OPERAND;
            TRY(tmdlHdmiCecFeatureAbort(this->cec.inst, \
                                        this->cec.initiator, \
                                        this->cec.feature_abort.FeatureOpcode, \
                                        this->cec.feature_abort.AbortReason));
            break;
         }
         break;
#ifdef TWL4030_HACK
      case CEC_OPCODE_VENDOR_REMOTE_BUTTON_DOWN:
         user_control(59,8);
#endif
         break;
      case CEC_OPCODE_FEATURE_ABORT:
         /* stop any state machine transition */
         break;
      case CEC_OPCODE_VENDOR_COMMAND:
      case CEC_OPCODE_DEVICE_VENDOR_ID:
         /* hopefully will be handle in userspace */
         break;
      default:
         if (receiver != 0x0F) {
            this->cec.feature_abort.FeatureOpcode=opcode;
            this->cec.feature_abort.AbortReason=CEC_ABORT_UNKNOWN_OPCODE;
            TRY(tmdlHdmiCecFeatureAbort(this->cec.inst,                 \
                                        this->cec.initiator,            \
                                        this->cec.feature_abort.FeatureOpcode, \
                                        this->cec.feature_abort.AbortReason));
         }
         break;
      }
      this->driver.poll_done = true;
      wake_up_interruptible(&this->driver.wait);
   }
   else if (event == TMDL_HDMICEC_CALLBACK_STATUS) {

      this->cec.frame.size = length + 2;
      this->cec.frame.addr = data[0]; /* .AddressByte */
      initiator = (this->cec.frame.addr >> 4) & 0x0F;
      receiver = this->cec.frame.addr & 0x0F;
      memcpy(&this->cec.frame.data,&data[1],length-1); /* .DataBytes[], length - siezof(length,addr)  */
      opcode=this->cec.frame.data[0];
      this->cec.frame.service = CEC_TX_DONE;

      if (length==POLLING_LENGTH) {
         if (opcode == CEC_MSG_FAIL_DEST_NOT_ACK) {
            /* no echo means it's mine ! */
            TRY(tmdlHdmiCecSetLogicalAddress(this->cec.inst,this->cec.rx_addr));
            TRY(tmdlHdmiCecReportPhysicalAddress(this->cec.inst,        \
                                                 this->cec.phy_addr,    \
                                                 this->cec.device_type));
            /* DEVICE VENDOR ID sending after logicial address allocation according to spec 1.4 */
            TRY(tmdlHdmiCecDeviceVendorID(this->cec.inst, this->cec.vendor_id));
         }
         else if (opcode == CEC_MSG_SUCCESS) {
            /* try next one */
            this->cec.rx_addr=get_next_logical_addr(this->cec.device_type,this->cec.rx_addr);
            if (this->cec.rx_addr != CEC_LOGICAL_ADDRESS_UNREGISTRED_BROADCAST) {
               TRY(tmdlHdmiCecPollingMessage(this->cec.inst,this->cec.rx_addr));
            }
            else {
               /* no more room, keep and claim unregistred */
               TRY(tmdlHdmiCecSetLogicalAddress(this->cec.inst,this->cec.rx_addr));
               TRY(tmdlHdmiCecReportPhysicalAddress(this->cec.inst,     \
                                                    this->cec.phy_addr, \
                                                    this->cec.device_type));
            }
         }
         else {
            LOG(KERN_INFO,"ACK [%x--->%x] %s\n",initiator,receiver,cec_rxstatus(opcode));
         }
      }
      else {
         if (CEC_MSG_SUCCESS != opcode) {
            LOG(KERN_INFO,"ACK [%x--->%x] %s\n",initiator,receiver,cec_rxstatus(opcode));
         }
      }

      this->driver.poll_done = true;
      wake_up_interruptible(&this->driver.wait);

   }
   else {
      LOG(KERN_ERR,"Oups ! Callback got invalid event %d !\n",event);
   }

 TRY_DONE:
   (void)err;
}

/*
 *  DevLib CEC opening
 */
static int hdmi_cec_init(cec_instance *this, const char *osd_name)
{
   int err=0;

   /* Real opening */
   TRY(tmdlHdmiCecOpen(&this->cec.inst));

/*    this->cec.vendor_id = 0x006037;   /\* NXP (IEEE OUI) *\/ */
/*    this->cec.vendor_id = 0x0000f0;   /\* Samsung *\/ */
   this->cec.vendor_id = 0x00e091;   /* LGE */

/*    this->cec.version = CEC_VERSION_1_4; */
   this->cec.version = CEC_VERSION_1_3a;

   if (!osd_name)
      osd_name = "TDA19989";	/* TDA19989 by default */

   strncpy(this->cec.osd_name.data, 
           osd_name, sizeof(this->cec.osd_name.data));
   this->cec.osd_name.length = 
      min(strlen(osd_name), sizeof(this->cec.osd_name.data));
     
   this->cec.rx_addr = CEC_LOGICAL_ADDRESS_UNREGISTRED_BROADCAST;
   this->cec.rx_addr_mask = 0;
   this->cec.phy_addr = param_addr;			// 0xffff == unconfigured
   this->cec.device_type = device_type(param_device); // 4 == CEC_DEVICE_TYPE_PLAYBACK_DEVICE 

   TRY(tmdlHdmiCecRegisterCallbacks(this->cec.inst,eventCallbackCEC));

 TRY_DONE:
   return err;
}


/*
 *
 * ENTRY POINTS
 * ------------
 * LEVEL 3
 *
 * - 
 *
 */



/*
 *  ioctl driver :: opening
 */

static int this_cdev_open(struct inode *pInode, struct file *pFile)
{
   cec_instance *this = pFile->private_data;
   int minor = iminor(pInode);

   if (minor >= MAX_MINOR) {
      printk(KERN_ERR "hdmicec:%s:only one cec opening please\n",__func__);
      return -EINVAL;
   }

   if (this && this != &our_instance) {
      printk(KERN_ERR "hdmicec:%s:pFile mismatch\n",__func__);
      return -EINVAL;
   }

   this = &our_instance;
   pFile->private_data = this;

   down(&this->driver.sem);

   LOG(KERN_INFO,"major:%d minor:%d user:%d\n", 
       imajor(pInode), iminor(pInode), this->driver.user_counter);

   if ((this->driver.user_counter++) && (this->driver.minor == minor)) {
      /* init already done */
      up(&this->driver.sem);
      return 0;
   }

   this->driver.minor = minor;

   up(&this->driver.sem);
   return 0;
}

/*
 *  ioctl driver :: ioctl
 */
static long this_cdev_ioctl(struct file *pFile, unsigned int cmd, unsigned long arg)
{
   cec_instance* this = pFile->private_data;
   int err=0;

   LOG(KERN_INFO,":%s\n",cec_ioctl(_IOC_NR(cmd)));

   if (_IOC_TYPE(cmd) != CEC_IOCTL_BASE) {
      printk(KERN_INFO "hdmicec:%s:unknown ioctl type: %x\n",__func__,_IOC_TYPE(cmd));
      return -ENOIOCTLCMD;
   }

   if (_IOC_DIR(cmd) & _IOC_READ) 
      err = !arg || !access_ok(VERIFY_WRITE, (void __user *)arg, _IOC_SIZE(cmd));
   else if (_IOC_DIR(cmd) & _IOC_WRITE)
      err = !arg || !access_ok(VERIFY_READ, (void __user *)arg, _IOC_SIZE(cmd));

   if (err) {
      printk(KERN_ERR "hdmicec:%s:argument access denied (check address vs value)\n",__func__);
      printk(KERN_ERR "_IOC_DIR:%d arg:%lx\n",_IOC_DIR(cmd),arg);
      return -EFAULT;
   }
   
   down(&this->driver.sem);

   /* Check DevLib consistancy here */

   switch ( _IOC_NR(cmd) )
      {
      case CEC_IOCTL_SEND_MSG_CMD:
         {
	    cec_frame frame;

            BUG_ON(copy_from_user(&frame, (void *)arg, sizeof(cec_frame)) != 0);

            if (frame.size >= 3 && frame.size < sizeof(cec_frame)) {
               /* set initiator to "broadcast" */
               frame.addr |= CEC_LOGICAL_ADDRESS_UNREGISTRED_BROADCAST << 4;
               TRY(tmdlHdmiCecSendMessage(this->cec.inst, &frame.addr, frame.size - 2));
            }
            break;
         }

      case CEC_IOCTL_GET_RAW_MODE_CMD:
         {
            BUG_ON(copy_to_user((void *)arg,&this->driver.raw_mode,sizeof(this->driver.raw_mode)) != 0);
	    break;
	 }

      case CEC_IOCTL_SET_RAW_MODE_CMD:
         {
            unsigned char raw_mode;
            BUG_ON(copy_from_user(&raw_mode,(void *)arg,sizeof(raw_mode)) != 0);

            if (raw_mode < 2 && this->driver.raw_mode != raw_mode) {

               this->driver.raw_mode = raw_mode;

	       if (this->driver.raw_mode) {
                  this->driver.write_pending = 0;
                  this->driver.read_queue_head = this->driver.read_queue_tail;
               } 
               else {
                  this->driver.write_pending = 1;
                  this->driver.read_queue_head = this->driver.read_queue_tail;
                  wake_up_interruptible(&this->driver.wait_read);
                  wake_up_interruptible(&this->driver.wait_write);
	       }

               cec_listen_single(this, CEC_LOGICAL_ADDRESS_UNREGISTRED_BROADCAST);
            }
	    break;
	 }

      case CEC_IOCTL_GET_RAW_INFO_CMD:
         {
            int len;
            cec_raw_info info;

            info.VendorID = this->cec.vendor_id;
            info.QueueSize = ARRAY_SIZE(this->driver.read_queue);
            info.LogicalAddress = this->cec.rx_addr;
            info.PhysicalAddress = this->cec.phy_addr;
            info.LogicalAddressMask = this->cec.rx_addr_mask;

	    len = min((size_t)this->cec.osd_name.length, sizeof(info.OsdName)-1);
	    memcpy(info.OsdName, this->cec.osd_name.data, len);
	    memset(&info.OsdName[len], '\0', sizeof(info.OsdName) - len);

            BUG_ON(copy_to_user((void *)arg,&info,sizeof(info)) != 0);
	    break;
	 }

      case CEC_IOCTL_SET_RX_ADDR_MASK_CMD:
         {
            cec_rx_mask rx_mask;
            BUG_ON(copy_from_user(&rx_mask,(void *)arg,sizeof(rx_mask)) != 0);

            if (this->driver.raw_mode) {
               cec_listen_multi(this, rx_mask.SwitchOn, rx_mask.SwitchOff);
            }
            break;
         }


      case CEC_VERBOSE_ON_CMD:
         {
            printk(KERN_INFO "verbose on\n");
            this->param.verbose=1;
            break;
         }

      case CEC_VERBOSE_OFF_CMD:
         {
            printk(KERN_INFO "verbose off\n");
            this->param.verbose=0;
            break;
         }

      case CEC_BYEBYE_CMD:
         {
            LOG(KERN_INFO,"callback release request\n");
            this->cec.frame.service=CEC_RELEASE;
            this->driver.poll_done = true;
            wake_up_interruptible(&this->driver.wait);
            break;
         }

         /*
           no param
         */

      case CEC_IOCTL_DISABLE_CALIBRATION_CMD:
         {
            TRY(tmdlHdmiCecDisableCalibration(this->cec.inst));
            break;
         }

      case CEC_IOCTL_INSTANCE_CONFIG_CMD:
         {
            TRY(tmdlHdmiCecInstanceConfig(this->cec.inst));
            break;
         }

      case CEC_IOCTL_REQUEST_ACTIVE_SRC_CMD:
         {
            TRY(tmdlHdmiCecRequestActiveSource(this->cec.inst));
            break;
         }

      case CEC_IOCTL_ABORT_MSG_CMD:
         {
            TRY(tmdlHdmiCecAbortMessage(this->cec.inst,this->cec.initiator));
            break;
         }

      case CEC_IOCTL_GET_MENU_LANGUAGE_CMD:
         {
            TRY(tmdlHdmiCecGetMenuLanguage(this->cec.inst,this->cec.initiator));
            break;
         }

      case CEC_IOCTL_GIVE_AUDIO_STATUS_CMD:
         {
            TRY(tmdlHdmiCecGiveAudioStatus(this->cec.inst,this->cec.initiator));
            break;
         }

      case CEC_IOCTL_GIVE_DEVICE_POWER_STATUS_CMD:
         {
            TRY(tmdlHdmiCecGiveDevicePowerStatus(this->cec.inst,this->cec.initiator));
            break;
         }

      case CEC_IOCTL_GIVE_DEVICE_VENDOR_ID_CMD:
         {
            TRY(tmdlHdmiCecGiveDeviceVendorID(this->cec.inst,this->cec.initiator));
            break;
         }

      case CEC_IOCTL_GIVE_OSD_NAME_CMD:
         {
            TRY(tmdlHdmiCecGiveOsdName(this->cec.inst,this->cec.initiator));
            break;
         }

      case CEC_IOCTL_GIVE_PHY_ADDR_CMD:
         {
            TRY(tmdlHdmiCecGivePhysicalAddress(this->cec.inst,this->cec.initiator));
            break;
         }

      case CEC_IOCTL_GIVE_SYS_AUDIO_MODE_STATUS_CMD:
         {
            TRY(tmdlHdmiCecGiveSystemAudioModeStatus(this->cec.inst,this->cec.initiator));
            break;
         }

      case CEC_IOCTL_IMAGE_VIEW_ON_CMD:
         {
            TRY(tmdlHdmiCecImageViewOn(this->cec.inst,this->cec.initiator));
            break;
         }

      case CEC_IOCTL_POLLING_MSG_CMD:
         {
            TRY(tmdlHdmiCecPollingMessage(this->cec.inst,this->cec.rx_addr));
            break;
         }

      case CEC_IOCTL_REC_OFF_CMD:
         {
            TRY(tmdlHdmiCecRecordOff(this->cec.inst,this->cec.initiator));
            break;
         }

      case CEC_IOCTL_REC_ON_OWN_SRC_CMD:
         {
            TRY(tmdlHdmiCecRecordOnOwnSource(this->cec.inst,this->cec.initiator));
            break;
         }

      case CEC_IOCTL_REC_TV_SCREEN_CMD:
         {
            TRY(tmdlHdmiCecRecordTvScreen(this->cec.inst,this->cec.initiator));
            break;
         }

      case CEC_IOCTL_STANDBY_CMD:
         {
            TRY(tmdlHdmiCecStandby(this->cec.inst,this->cec.initiator));
            break;
         }

      case CEC_IOCTL_TEXT_VIEW_ON_CMD:
         {
            TRY(tmdlHdmiCecTextViewOn(this->cec.inst,this->cec.initiator));
            break;
         }

      case CEC_IOCTL_TUNER_STEP_DECREMENT_CMD:
         {
            TRY(tmdlHdmiCecTunerStepDecrement(this->cec.inst,this->cec.initiator));
            break;
         }

      case CEC_IOCTL_TUNER_STEP_INCREMENT_CMD:
         {
            TRY(tmdlHdmiCecTunerStepIncrement(this->cec.inst,this->cec.initiator));
            break;
         }

      case CEC_IOCTL_USER_CTRL_RELEASED_CMD:
         {
            TRY(tmdlHdmiCecUserControlReleased(this->cec.inst,this->cec.initiator));
            break;
         }

      case CEC_IOCTL_VENDOR_REMOTE_BUTTON_UP_CMD:
         {
            TRY(tmdlHdmiCecVendorRemoteButtonUp(this->cec.inst,this->cec.initiator));
            break;
         }

      case CEC_IOCTL_ROUTING_INFORMATION_CMD:
         {
            TRY(tmdlHdmiCecRoutingInformation(this->cec.inst,this->cec.phy_addr));
            break;
         }

      case CEC_IOCTL_SET_STREAM_PATH_CMD:
         {
            TRY(tmdlHdmiCecSetStreamPath(this->cec.inst,this->cec.phy_addr));
            break;
         }

      case CEC_IOCTL_ACTIVE_SRC_CMD:
         {
            /* NEW first do a <image view on> */
            /* NEW when switch by DSS and was inactive */
            TRY(tmdlHdmiCecActiveSource(this->cec.inst,this->cec.phy_addr));
            break;
         }

      case CEC_IOCTL_SYS_AUDIO_MODE_REQUEST_CMD:
         {
            TRY(tmdlHdmiCecSystemAudioModeRequest(this->cec.inst, \
                                                  this->cec.initiator, \
                                                  this->cec.phy_addr));
            break;
         }

         /*
           1 param
         */

      case CEC_IOCTL_RX_ADDR_CMD:
         {
            unsigned char rx_addr;
            BUG_ON(copy_from_user(&rx_addr,(void *)arg,sizeof(unsigned char)) != 0);
            if (rx_addr <= CEC_LOGICAL_ADDRESS_UNREGISTRED_BROADCAST) {
               cec_listen_single(this, rx_addr);
            }
            break;
         }

      case CEC_IOCTL_PHY_ADDR_CMD:
         {
            BUG_ON(copy_from_user(&this->cec.phy_addr,(unsigned short*)arg,sizeof(unsigned short)) != 0);
            break;
         }

      case CEC_IOCTL_GET_CEC_VERSION_CMD:
         {
            BUG_ON(copy_from_user(&this->cec.version,(cec_version*)arg,sizeof(cec_version)) != 0);
            TRY(tmdlHdmiCecGetCecVersion(this->cec.inst,this->cec.version));
            break;
         }

      case CEC_IOCTL_GET_SW_VERSION_CMD:
         {
            TRY(tmdlHdmiCecGetSWVersion(&this->cec.sw_version));
            BUG_ON(copy_to_user((cec_sw_version*)arg,&this->cec.sw_version,sizeof(cec_sw_version)) != 0);
            break;
         }

      case CEC_IOCTL_SET_POWER_STATE_CMD:
         {
            /* NEW : log : please use DSS */
            BUG_ON(copy_from_user(&this->cec.power,(cec_power*)arg,sizeof(cec_power)) != 0);
            TRY(tmdlHdmiCecSetPowerState(this->cec.inst,this->cec.power));
            break;
         }

      case CEC_IOCTL_GET_POWER_STATE_CMD:
         {
            TRY(tmdlHdmiCecGetPowerState(this->cec.inst,&this->cec.power));
            BUG_ON(copy_to_user((cec_power*)arg,&this->cec.power,sizeof(cec_power)) != 0);
            break;
         }

      case CEC_IOCTL_INSTANCE_SETUP_CMD:
         {
            BUG_ON(copy_from_user(&this->cec.setup,(cec_setup*)arg,sizeof(cec_setup)) != 0);
            TRY(tmdlHdmiCecInstanceSetup(this->cec.inst,&this->cec.setup));
            break;
         }

      case CEC_IOCTL_GET_INSTANCE_SETUP_CMD:
         {
            TRY(tmdlHdmiCecGetInstanceSetup(this->cec.inst,&this->cec.setup));
            BUG_ON(copy_to_user((cec_setup*)arg,&this->cec.setup,sizeof(cec_setup)) != 0);
            break;
         }

         /*
           case CEC_IOCTL_ENABLE_EVENT_CMD:
           {
           BUG_ON(copy_from_user(&this->cec.an_event,(cec_event*)arg,sizeof(cec_event)) != 0);
           TRY(tmdlHdmiCecEnableEvent(this->cec.inst,this->cec.an_event));
           break;
           }

           case CEC_IOCTL_DISABLE_EVENT_CMD:
           {
           BUG_ON(copy_from_user(&this->cec.an_event,(cec_event*)arg,sizeof(cec_event)) != 0);
           TRY(tmdlHdmiCecDisableEvent(this->cec.inst,this->cec.an_event));
           break;
           }
         */

      case CEC_IOCTL_SET_MENU_LANGUAGE_CMD:
         {
            BUG_ON(copy_from_user(&this->cec.clock,(cec_string*)arg,sizeof(cec_string)) != 0);
            TRY(tmdlHdmiCecSetMenuLanguage(this->cec.inst,this->cec.string.data));
            break;
         }

      case CEC_IOCTL_ENABLE_CALIBRATION_CMD:
         {
            BUG_ON(copy_from_user(&this->cec.clock,(cec_clock*)arg,sizeof(cec_clock)) != 0);
            TRY(tmdlHdmiCecEnableCalibration(this->cec.inst,this->cec.clock));
            break;
         }

         /*
           >1 param
         */

      case CEC_IOCTL_WAIT_FRAME_CMD:
         {
            this->cec.frame.service = CEC_WAITING;
            this->driver.poll_done = false;
            up(&this->driver.sem);
            if (wait_event_interruptible(this->driver.wait,this->driver.poll_done)) return -ERESTARTSYS;
            down(&this->driver.sem);
            BUG_ON(copy_to_user((cec_frame*)arg,&this->cec.frame,sizeof(cec_frame)) != 0);
            break;
         }

      case CEC_IOCTL_VERSION_CMD:
         {
            BUG_ON(copy_from_user(&this->cec.version,(cec_version*)arg,sizeof(cec_version)) != 0);
            TRY(tmdlHdmiCecVersion(this->cec.inst, \
                                   this->cec.initiator, \
                                   this->cec.version));
            break;
         }

      case CEC_IOCTL_CLEAR_ANALOGUE_TIMER_CMD:
         {
            BUG_ON(copy_from_user(&this->cec.analog_timer,(cec_analogue_timer*)arg,sizeof(cec_analogue_timer)) != 0);
            TRY(tmdlHdmiCecClearAnalogueTimer(this->cec.inst, \
                                              this->cec.initiator, \
                                              this->cec.analog_timer.DayOfMonth, \
                                              this->cec.analog_timer.MonthOfYear, \
                                              this->cec.analog_timer.StartTime, \
                                              &this->cec.analog_timer.Duration, \
                                              this->cec.analog_timer.RecordingSequence, \
                                              this->cec.analog_timer.AnalogueBroadcastType, \
                                              this->cec.analog_timer.AnalogueFrequency, \
                                              this->cec.analog_timer.BroadcastSystem));
            break;
         }

      case CEC_IOCTL_CLEAR_DIGITAL_TIMER_CMD:
         {
            BUG_ON(copy_from_user(&this->cec.digital_timer,(cec_digital_timer*)arg,sizeof(cec_digital_timer)) != 0);
            TRY(tmdlHdmiCecClearDigitalTimer(this->cec.inst, \
                                             this->cec.initiator, \
                                             this->cec.digital_timer.DayOfMonth, \
                                             this->cec.digital_timer.MonthOfYear, \
                                             this->cec.digital_timer.StartTime, \
                                             &this->cec.digital_timer.Duration, \
                                             this->cec.digital_timer.RecordingSequence, \
                                             &this->cec.digital_timer.ServiceIdentification));
            break;
         }

      case CEC_IOCTL_CLEAR_EXT_TIMER_WITH_EXT_PLUG_CMD:
         {
            BUG_ON(copy_from_user(&this->cec.etwep,(cec_ext_timer_with_ext_plug*)arg,sizeof(cec_ext_timer_with_ext_plug)) != 0);
            TRY(tmdlHdmiCecClearExternalTimerWithExternalPlug(this->cec.inst, \
                                                              this->cec.initiator, \
                                                              this->cec.etwep.DayOfMonth, \
                                                              this->cec.etwep.MonthOfYear, \
                                                              this->cec.etwep.StartTime, \
                                                              &this->cec.etwep.Duration, \
                                                              this->cec.etwep.RecordingSequence, \
                                                              this->cec.etwep.ExternalPlug));
            break;
         }

      case CEC_IOCTL_CLEAR_EXT_TIMER_WITH_PHY_ADDR_CMD:
         {
            BUG_ON(copy_from_user(&this->cec.etwpa,(cec_ext_timer_with_phy_addr*)arg,sizeof(cec_ext_timer_with_phy_addr)) != 0);
            TRY(tmdlHdmiCecClearExternalTimerWithPhysicalAddress(this->cec.inst, \
                                                                 this->cec.initiator, \
                                                                 this->cec.etwpa.DayOfMonth, \
                                                                 this->cec.etwpa.MonthOfYear, \
                                                                 this->cec.etwpa.StartTime, \
                                                                 &this->cec.etwpa.Duration, \
                                                                 this->cec.etwpa.RecordingSequence, \
                                                                 this->cec.etwpa.ExternalPhysicalAddress));
            break;
         }

      case CEC_IOCTL_DECK_CTRL_CMD:
         {
            BUG_ON(copy_from_user(&this->cec.deck_ctrl,(cec_deck_ctrl*)arg,sizeof(cec_deck_ctrl)) != 0);
            TRY(tmdlHdmiCecDeckControl(this->cec.inst, \
                                       this->cec.initiator, \
                                       this->cec.deck_ctrl));
            break;
         }

      case CEC_IOCTL_DECK_STATUS_CMD:
         {
            BUG_ON(copy_from_user(&this->cec.deck_status,(cec_deck_status*)arg,sizeof(cec_deck_status)) != 0);
            TRY(tmdlHdmiCecDeckStatus(this->cec.inst, \
                                      this->cec.initiator, \
                                      this->cec.deck_status));
            break;
         }

      case CEC_IOCTL_DEVICE_VENDOR_ID_CMD:
         {
            BUG_ON(copy_from_user(&this->cec.vendor_id,(unsigned long*)arg,sizeof(unsigned long)) != 0);
            TRY(tmdlHdmiCecDeviceVendorID(this->cec.inst, \
                                          this->cec.vendor_id));
            break;
         }

      case CEC_IOCTL_FEATURE_ABORT_CMD:
         {
            BUG_ON(copy_from_user(&this->cec.feature_abort,(cec_feature_abort*)arg,sizeof(cec_feature_abort)) != 0);
            TRY(tmdlHdmiCecFeatureAbort(this->cec.inst, \
                                        this->cec.initiator, \
                                        this->cec.feature_abort.FeatureOpcode, \
                                        this->cec.feature_abort.AbortReason));
            break;
         }

      case CEC_IOCTL_GIVE_DECK_STATUS_CMD:
         {
            BUG_ON(copy_from_user(&this->cec.satus_request,(cec_status_request*)arg,sizeof(cec_status_request)) != 0);
            TRY(tmdlHdmiCecGiveDeckStatus(this->cec.inst, \
                                          this->cec.initiator, \
                                          this->cec.satus_request));
            break;
         }

      case CEC_IOCTL_GIVE_TUNER_DEVICE_STATUS_CMD:
         {
            BUG_ON(copy_from_user(&this->cec.satus_request,(cec_status_request*)arg,sizeof(cec_status_request*)) != 0);
            TRY(tmdlHdmiCecGiveTunerDeviceStatus(this->cec.inst, \
                                                 this->cec.initiator, \
                                                 this->cec.satus_request));
            break;
         }

      case CEC_IOCTL_INACTIVE_SRC_CMD:
         {
            /* NEW first stand by video */
            /* NEW when hdmi_disable and was active */
            TRY(tmdlHdmiCecInactiveSource(this->cec.inst, \
                                          this->cec.initiator, \
                                          this->cec.phy_addr));
            break;
         }

      case CEC_IOCTL_MENU_REQUEST_CMD:
         {
            BUG_ON(copy_from_user(&this->cec.menu_request,(cec_menu_request*)arg,sizeof(cec_menu_request)) != 0);
            TRY(tmdlHdmiCecMenuRequest(this->cec.inst, \
                                       this->cec.initiator, \
                                       this->cec.menu_request));
            break;
         }

      case CEC_IOCTL_MENU_STATUS_CMD:
         {
            BUG_ON(copy_from_user(&this->cec.menu_status,(cec_menu_status*)arg,sizeof(cec_menu_status)) != 0);
            TRY(tmdlHdmiCecMenuStatus(this->cec.inst, \
                                      this->cec.initiator, \
                                      this->cec.menu_status));
            break;
         }

      case CEC_IOCTL_PLAY_CMD:
         {
            BUG_ON(copy_from_user(&this->cec.play,(cec_play*)arg,sizeof(cec_play)) != 0);
            TRY(tmdlHdmiCecPlay(this->cec.inst, \
                                this->cec.initiator, \
                                this->cec.play));
            break;
         }

      case CEC_IOCTL_REC_ON_ANALOGUE_SERVICE_CMD:
         {
            BUG_ON(copy_from_user(&this->cec.analog_service,(cec_analogue_service*)arg,sizeof(cec_analogue_service)) != 0);
            TRY(tmdlHdmiCecRecordOnAnalogueService(this->cec.inst, \
                                                   this->cec.initiator, \
                                                   this->cec.analog_service.AnalogueBroadcastType, \
                                                   this->cec.analog_service.AnalogueFrequency, \
                                                   this->cec.analog_service.BroadcastSystem));
            break;
         }

      case CEC_IOCTL_REC_ON_DIGITAL_SERVICE_CMD:
         {
            BUG_ON(copy_from_user(&this->cec.digital_service,(cec_digital_service*)arg,sizeof(cec_digital_service)) != 0);
            TRY(tmdlHdmiCecRecordOnDigitalService(this->cec.inst, \
                                                  this->cec.initiator, \
                                                  &this->cec.digital_service));
            break;
         }

      case CEC_IOCTL_REC_ON_EXT_PHY_ADDR_CMD:
         {
            TRY(tmdlHdmiCecRecordOnExternalPhysicalAddress(this->cec.inst, \
                                                           this->cec.initiator, \
                                                           this->cec.phy_addr));
            break;
         }

      case CEC_IOCTL_REC_ON_EXT_PLUG_CMD:
         {
            BUG_ON(copy_from_user(&this->cec.ext_plug,(cec_ext_plug*)arg,sizeof(cec_ext_plug)) != 0);
            TRY(tmdlHdmiCecRecordOnExternalPlug(this->cec.inst, \
                                                this->cec.initiator, \
                                                this->cec.ext_plug));
            break;
         }

      case CEC_IOCTL_REC_STATUS_CMD:
         {
            BUG_ON(copy_from_user(&this->cec.rec_status,(cec_rec_status*)arg,sizeof(cec_rec_status)) != 0);
            TRY(tmdlHdmiCecRecordStatus(this->cec.inst, \
                                        this->cec.initiator, \
                                        this->cec.rec_status));
            break;
         }

      case CEC_IOCTL_REPORT_AUDIO_STATUS_CMD:
         {
            BUG_ON(copy_from_user(&this->cec.audio_status,(cec_audio_status*)arg,sizeof(cec_audio_status)) != 0);
            TRY(tmdlHdmiCecReportAudioStatus(this->cec.inst, \
                                             this->cec.initiator, \
                                             &this->cec.audio_status));
            break;
         }

      case CEC_IOCTL_REPORT_PHY_ADDR_CMD:
         {
            BUG_ON(copy_from_user(&this->cec.device_type,(cec_device_type*)arg,sizeof(cec_device_type)) != 0);
            TRY(tmdlHdmiCecReportPhysicalAddress(this->cec.inst, \
                                                 this->cec.phy_addr, \
                                                 this->cec.device_type));
            break;
         }

      case CEC_IOCTL_REPORT_POWER_STATUS_CMD:
         {
            BUG_ON(copy_from_user(&this->cec.source_status,(cec_power_status*)arg,sizeof(cec_power_status)) != 0);
            TRY(tmdlHdmiCecReportPowerStatus(this->cec.inst, \
                                             this->cec.initiator, \
                                             this->cec.source_status));
            break;
         }

      case CEC_IOCTL_SELECT_ANALOGUE_SERVICE_CMD:
         {
            BUG_ON(copy_from_user(&this->cec.analog_service,(cec_analogue_service*)arg,sizeof(cec_analogue_service)) != 0);
            TRY(tmdlHdmiCecSelectAnalogueService(this->cec.inst, \
                                                 this->cec.initiator, \
                                                 this->cec.analog_service.AnalogueBroadcastType, \
                                                 this->cec.analog_service.AnalogueFrequency, \
                                                 this->cec.analog_service.BroadcastSystem));
            break;
         }

      case CEC_IOCTL_SELECT_DIGITAL_SERVICE_CMD:
         {
            BUG_ON(copy_from_user(&this->cec.digital_service,(cec_digital_service*)arg,sizeof(cec_digital_service)) != 0);
            TRY(tmdlHdmiCecSelectDigitalService(this->cec.inst, \
                                                this->cec.initiator, \
                                                &this->cec.digital_service));
            break;
         }

      case CEC_IOCTL_SET_ANALOGUE_TIMER_CMD:
         {
            BUG_ON(copy_from_user(&this->cec.analog_timer,(cec_analogue_timer*)arg,sizeof(cec_analogue_timer)) != 0);
            TRY(tmdlHdmiCecSetAnalogueTimer(this->cec.inst, \
                                            this->cec.initiator, \
                                            this->cec.analog_timer.DayOfMonth, \
                                            this->cec.analog_timer.MonthOfYear, \
                                            this->cec.analog_timer.StartTime, \
                                            &this->cec.analog_timer.Duration, \
                                            this->cec.analog_timer.RecordingSequence, \
                                            this->cec.analog_timer.AnalogueBroadcastType, \
                                            this->cec.analog_timer.AnalogueFrequency, \
                                            this->cec.analog_timer.BroadcastSystem));
            break;
         }

      case CEC_IOCTL_SET_AUDIO_RATE_CMD:
         {
            BUG_ON(copy_from_user(&this->cec.audio_rate,(cec_audio_rate*)arg,sizeof(cec_audio_rate)) != 0);
            TRY(tmdlHdmiCecSetAudioRate(this->cec.inst, \
                                        this->cec.initiator, \
                                        this->cec.audio_rate));
            break;
         }

      case CEC_IOCTL_SET_DIGITAL_TIMER_CMD:
         {
            BUG_ON(copy_from_user(&this->cec.digital_timer,(cec_digital_timer*)arg,sizeof(cec_digital_timer)) != 0);
            TRY(tmdlHdmiCecSetDigitalTimer(this->cec.inst, \
                                           this->cec.initiator, \
                                           this->cec.digital_timer.DayOfMonth, \
                                           this->cec.digital_timer.MonthOfYear, \
                                           this->cec.digital_timer.StartTime, \
                                           &this->cec.digital_timer.Duration, \
                                           this->cec.digital_timer.RecordingSequence, \
                                           &this->cec.digital_timer.ServiceIdentification));
            break;
         }

      case CEC_IOCTL_SET_EXT_TIMER_WITH_EXT_PLUG_CMD:
         {
            BUG_ON(copy_from_user(&this->cec.etwep,(cec_ext_timer_with_ext_plug*)arg,sizeof(cec_ext_timer_with_ext_plug)) != 0);
            TRY(tmdlHdmiCecSetExternalTimerWithExternalPlug(this->cec.inst, \
                                                            this->cec.initiator, \
                                                            this->cec.etwep.DayOfMonth, \
                                                            this->cec.etwep.MonthOfYear, \
                                                            this->cec.etwep.StartTime, \
                                                            &this->cec.etwep.Duration, \
                                                            this->cec.etwep.RecordingSequence, \
                                                            this->cec.etwep.ExternalPlug));
            break;
         }

      case CEC_IOCTL_SET_EXT_TIMER_WITH_PHY_ADDR_CMD:
         {
            BUG_ON(copy_from_user(&this->cec.etwpa,(cec_ext_timer_with_phy_addr*)arg,sizeof(cec_ext_timer_with_phy_addr)) != 0);
            TRY(tmdlHdmiCecSetExternalTimerWithPhysicalAddress(this->cec.inst, \
                                                               this->cec.initiator, \
                                                               this->cec.etwpa.DayOfMonth, \
                                                               this->cec.etwpa.MonthOfYear, \
                                                               this->cec.etwpa.StartTime, \
                                                               &this->cec.etwpa.Duration, \
                                                               this->cec.etwpa.RecordingSequence, \
                                                               this->cec.etwpa.ExternalPhysicalAddress));
            break;
         }

      case CEC_IOCTL_SET_SYS_AUDIO_MODE_CMD:
         {
            BUG_ON(copy_from_user(&this->cec.sys_audio_status,(cec_sys_audio_status*)arg,sizeof(cec_sys_audio_status)) != 0);
            TRY(tmdlHdmiCecSetSystemAudioMode(this->cec.inst, \
                                              this->cec.initiator, \
                                              this->cec.sys_audio_status));
            break;
         }

      case CEC_IOCTL_SYS_AUDIO_MODE_STATUS_CMD:
         {
            BUG_ON(copy_from_user(&this->cec.sys_audio_status,(cec_sys_audio_status*)arg,sizeof(cec_sys_audio_status)) != 0);
            TRY(tmdlHdmiCecSystemAudioModeStatus(this->cec.inst, \
                                                 this->cec.initiator, \
                                                 this->cec.sys_audio_status));
            break;
         }

      case CEC_IOCTL_TIMER_CLEARED_STATUS_CMD:
         {
            BUG_ON(copy_from_user(&this->cec.timer_cleared_status,(cec_timer_cleared_status*)arg,sizeof(cec_timer_cleared_status)) != 0);
            TRY(tmdlHdmiCecTimerClearedStatus(this->cec.inst, \
                                              this->cec.initiator, \
                                              this->cec.timer_cleared_status));
            break;
         }

      case CEC_IOCTL_TIMER_STATUS_CMD:
         {
            BUG_ON(copy_from_user(&this->cec.timer_status,(cec_timer_status*)arg,sizeof(cec_timer_status)) != 0);
            TRY(tmdlHdmiCecTimerStatus(this->cec.inst, \
                                       this->cec.initiator, \
                                       &this->cec.timer_status));
            break;
         }

      case CEC_IOCTL_TUNER_DEVICE_STATUS_ANALOGUE_CMD:
         {
            BUG_ON(copy_from_user(&this->cec.tdsa,(cec_tuner_device_status_analogue*)arg,sizeof(cec_tuner_device_status_analogue)) != 0);
            TRY(tmdlHdmiCecTunerDeviceStatusAnalogue(this->cec.inst, \
                                                     this->cec.initiator, \
                                                     this->cec.tdsa.RecordingFlag, \
                                                     this->cec.tdsa.TunerDisplayInfo, \
                                                     this->cec.tdsa.AnalogueBroadcastType, \
                                                     this->cec.tdsa.AnalogueFrequency, \
                                                     this->cec.tdsa.BroadcastSystem));
            break;
         }

      case CEC_IOCTL_TUNER_DEVICE_STATUS_DIGITAL_CMD:
         {
            BUG_ON(copy_from_user(&this->cec.tdsd,(cec_tuner_device_status_digital*)arg,sizeof(cec_tuner_device_status_digital)) != 0);
            TRY(tmdlHdmiCecTunerDeviceStatusDigital(this->cec.inst, \
                                                    this->cec.initiator, \
                                                    this->cec.tdsd.RecordingFlag, \
                                                    this->cec.tdsd.TunerDisplayInfo, \
                                                    &this->cec.tdsd.ServiceIdentification));
            break;
         }

      case CEC_IOCTL_USER_CTRL_CMD:
         {
            BUG_ON(copy_from_user(&this->cec.user_ctrl,(cec_user_ctrl*)arg,sizeof(cec_user_ctrl)) != 0);
            TRY(tmdlHdmiCecUserControlPressed(this->cec.inst, \
                                              this->cec.initiator, \
                                              this->cec.user_ctrl));
            break;
         }

      case CEC_IOCTL_USER_CTRL_PLAY_CMD:
         {
            BUG_ON(copy_from_user(&this->cec.play,(cec_play*)arg,sizeof(cec_play)) != 0);
            TRY(tmdlHdmiCecUserControlPressedPlay(this->cec.inst, \
                                                  this->cec.initiator, \
                                                  this->cec.play));
            break;
         }

      case CEC_IOCTL_USER_CTRL_SELECT_AUDIOINPUT_CMD:
         {
            BUG_ON(copy_from_user(&this->cec.select,(unsigned char*)arg,sizeof(unsigned char)) != 0);
            TRY(tmdlHdmiCecUserControlPressedSelectAudioInput(this->cec.inst, \
                                                              this->cec.initiator, \
                                                              this->cec.select));
            break;
         }

      case CEC_IOCTL_USER_CTRL_SELECT_AVINPUT_CMD:
         {
            BUG_ON(copy_from_user(&this->cec.select,(unsigned char*)arg,sizeof(unsigned char)) != 0);
            TRY(tmdlHdmiCecUserControlPressedSelectAVInput(this->cec.inst, \
                                                           this->cec.initiator, \
                                                           this->cec.select));
            break;
         }

      case CEC_IOCTL_USER_CTRL_SELECT_MEDIA_CMD:
         {
            BUG_ON(copy_from_user(&this->cec.select,(unsigned char*)arg,sizeof(unsigned char)) != 0);
            TRY(tmdlHdmiCecUserControlPressedSelectMedia(this->cec.inst, \
                                                         this->cec.initiator, \
                                                         this->cec.select));
            break;
         }

      case CEC_IOCTL_USER_CTRL_TUNE_CMD:
         {
            BUG_ON(copy_from_user(&this->cec.user_ctrl_tune,(cec_user_ctrl_tune*)arg,sizeof(cec_user_ctrl_tune)) != 0);
            TRY(tmdlHdmiCecUserControlPressedTune(this->cec.inst, \
                                                  this->cec.initiator, \
                                                  &this->cec.user_ctrl_tune));
            break;
         }

      case CEC_IOCTL_SET_OSD_NAME_CMD:
         {
            BUG_ON(copy_from_user(&this->cec.osd_name,(cec_string*)arg,sizeof(cec_string)) != 0);
            TRY(tmdlHdmiCecSetOsdName(this->cec.inst,      \
                                      this->cec.initiator, \
                                      this->cec.osd_name.data, \
                                      this->cec.osd_name.length));
            break;
         }

      case CEC_IOCTL_SET_OSD_STRING_CMD:
         {
            BUG_ON(copy_from_user(&this->cec.osd_string,(cec_osd_string*)arg,sizeof(cec_osd_string)) != 0);
            TRY(tmdlHdmiCecSetOsdString(this->cec.inst, \
                                        this->cec.initiator, \
                                        this->cec.osd_string.DisplayControl, \
                                        this->cec.osd_string.data, \
                                        this->cec.osd_string.length));
            break;
         }

      case CEC_IOCTL_SET_TIMER_PROGRAM_TITLE_CMD:
         {
            BUG_ON(copy_from_user(&this->cec.string,(cec_string*)arg,sizeof(cec_string)) != 0);
            TRY(tmdlHdmiCecSetTimerProgramTitle(this->cec.inst, \
                                                this->cec.initiator, \
                                                this->cec.string.data, \
                                                this->cec.string.length));
            break;
         }

      case CEC_IOCTL_VENDOR_COMMAND_CMD:
         {
            BUG_ON(copy_from_user(&this->cec.string,(cec_string*)arg,sizeof(cec_string)) != 0);
            TRY(tmdlHdmiCecVendorCommand(this->cec.inst, \
                                         this->cec.initiator, \
                                         this->cec.string.data, \
                                         this->cec.string.length));
            break;
         }

      case CEC_IOCTL_VENDOR_REMOTE_BUTTON_DOWN_CMD:
         {
            BUG_ON(copy_from_user(&this->cec.string,(cec_string*)arg,sizeof(cec_string)) != 0);
            TRY(tmdlHdmiCecVendorRemoteButtonDown(this->cec.inst, \
                                                  this->cec.initiator, \
                                                  this->cec.string.data, \
                                                  this->cec.string.length));
            break;
         }

      case CEC_IOCTL_VENDOR_COMMAND_WITH_ID_CMD:
         {
            BUG_ON(copy_from_user(&this->cec.vcwi,(cec_vendor_command_with_id*)arg,sizeof(cec_vendor_command_with_id)) != 0);
            TRY(tmdlHdmiCecVendorCommandWithID(this->cec.inst,	\
                                               this->cec.initiator,	\
                                               this->cec.vcwi.VendorID, \
                                               this->cec.vcwi.cmd.data, \
                                               this->cec.vcwi.cmd.length));
            break;
         }

         /*       case : */
         /* 	 { */
         /* 	    BUG_ON(copy_from_user(&this->cec.,(*)arg,sizeof()) != 0); */
         /* 	    TRY((this->cec.inst, \ */
         /* 		 this->cec.,	 \ */
         /* 		 &this->cec.)); */
         /* 	    break; */
         /* 	 } */

      default:
         {
            /* unrecognized ioctl */	
            printk(KERN_INFO " unknown ioctl %x\n",cmd);
            up(&this->driver.sem);
            return -ENOIOCTLCMD;
         }
      }

 TRY_DONE:
   up(&this->driver.sem);
   return err;
}

/*
 *  ioctl driver :: read
 */
static ssize_t this_cdev_read(struct file *pFile, char *buffer, size_t length, loff_t *offset)
{
   cec_instance* this = pFile->private_data;
   int bytes_returned = 0;

   if (!this->driver.raw_mode || length < sizeof(cec_frame))
      return -EINVAL; 

   while (length - bytes_returned >= sizeof(cec_frame) &&
          this->driver.read_queue_head != this->driver.read_queue_tail) {
      memcpy(buffer + bytes_returned, 
        &this->driver.read_queue[this->driver.read_queue_head], sizeof(cec_frame));

      this->driver.read_queue_head = 
        (this->driver.read_queue_head + 1) % ARRAY_SIZE(this->driver.read_queue);

      bytes_returned += sizeof(cec_frame);
   }

   return bytes_returned;
}

/*
 *  ioctl driver :: write
 */
static ssize_t this_cdev_write(struct file *pFile, const char *buffer, size_t length, loff_t *offset)
{
   cec_instance* this = pFile->private_data;
   int err = 0, bytes_written = 0;
   const cec_frame *frame;

   if (!this->driver.raw_mode || length < sizeof(cec_frame))
      return -EINVAL; 

   frame = (const cec_frame *)buffer;
   if (frame->size < 3 || frame->size > sizeof(cec_frame))
      return -EINVAL; 
   
   down(&this->driver.sem);

   if (this->driver.write_pending || this->cec.source_status != CEC_POWER_STATUS_ON) {
      up(&this->driver.sem);
      return -EAGAIN;
   }

   if (this->cec.rx_addr == CEC_LOGICAL_ADDRESS_UNREGISTRED_BROADCAST ||
       (frame->addr & 0xf0) != (CEC_LOGICAL_ADDRESS_UNREGISTRED_BROADCAST << 4)) {
      TRY(tmdlHdmiCecSendMessage(this->cec.inst, (UInt8 *)&frame->addr, frame->size - 2));
      this->driver.write_pending = 1;
      bytes_written += length;
   }
   else {
      LOG(KERN_ERR,"initiator 'Broadcast' is not supported !\n");
      goto TRY_DONE;
   }

   up(&this->driver.sem);
   return bytes_written;

 TRY_DONE:
   up(&this->driver.sem);
   return -EINVAL;
}

/*
 *  ioctl driver :: poll
 */
static unsigned int this_cdev_poll(struct file *pFile, poll_table *poll_data)
{
   cec_instance* this = pFile->private_data;
   unsigned int mask = 0;

   down(&this->driver.sem);

   if (this->driver.raw_mode) {
      poll_wait(pFile, &this->driver.wait_read, poll_data);
      poll_wait(pFile, &this->driver.wait_write, poll_data);

      if (this->driver.read_queue_head != this->driver.read_queue_tail)
         mask |= POLLIN | POLLRDNORM;	/* readable */

      if (!this->driver.write_pending && this->cec.source_status == CEC_POWER_STATUS_ON)
         mask |= POLLOUT | POLLWRNORM;	/* writable */

   } else {
      mask |= POLLIN | POLLRDNORM | POLLOUT | POLLWRNORM;
   }

   up(&this->driver.sem);

   return mask;
}

/*
 *  ioctl driver :: releasing
 */
static int this_cdev_release(struct inode *pInode, struct file *pFile)
{
   cec_instance* this = pFile->private_data;
   int minor = iminor(pInode);

   LOG(KERN_INFO,"called\n");

   if (minor >= MAX_MINOR) {
      return -EINVAL;
   }

   BUG_ON(this->driver.minor!=iminor(pInode));
   down(&this->driver.sem);

   if (--this->driver.user_counter == 0) {
      if (this->driver.raw_mode) {
         this->driver.raw_mode = 0;
         this->driver.write_pending = 1;
         this->driver.read_queue_head = this->driver.read_queue_tail;

         wake_up_interruptible(&this->driver.wait_write);
         wake_up_interruptible(&this->driver.wait_read);

         cec_listen_single(this, CEC_LOGICAL_ADDRESS_UNREGISTRED_BROADCAST);
      }

      pFile->private_data = NULL;
   }
   else {
      LOG(KERN_INFO,"Still %d user pending\n",this->driver.user_counter);
   }

   up(&this->driver.sem);
   return 0;
}

/*
 *  I2C client :: creation
 */
static int __devinit this_i2c_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
   cec_instance *this=&our_instance;
   int err=0;

   LOG(KERN_INFO,"called\n");

   /*
     I2C setup
   */
   if (this->driver.i2c_client) {
      dev_err(&this->driver.i2c_client->dev, "<%s> CEC Device already created \n",
              __func__);
      return -ENODEV;
   }

   this->driver.i2c_client = client;
   i2c_set_clientdata(client, this);

   tmdlHdmiCecGetSWVersion(&this->cec.sw_version);
   LOG(KERN_INFO,"HDMI CEC SW Version:%lu.%lu compatibility:%lu\n", \
       this->cec.sw_version.majorVersionNr,\
       this->cec.sw_version.minorVersionNr,\
       this->cec.sw_version.compatibilityNr);

   /* I2C ok, then let's startup CEC */

   /* initialize raw mode stuff */
   this->driver.raw_mode = 0;
   this->driver.write_pending = 1;
   this->driver.read_queue_head = 0;
   this->driver.read_queue_tail = 0;
   init_waitqueue_head(&this->driver.wait_read);
   init_waitqueue_head(&this->driver.wait_write);

   /* prepare event */
   this->driver.poll_done = true; /* currently idle */
   init_waitqueue_head(&this->driver.wait);
#ifndef IRQ
   init_timer(&this->driver.timer); /* do it before request_irq */
   this->driver.timer.function=polling_timeout;
   this->driver.timer.data=0;
   this->driver.timer.expires = jiffies + HZ; /* start polling in one sec */
   add_timer(&this->driver.timer);
#else
   register_cec_interrupt((cec_callback_t)cec_interrupt);
#endif

#ifndef IRQ
   /* FRO calibration */
   err=gpio_request(TDA_IRQ_CALIB,"tda19989 calibration");
   if (err < 0) {
      printk(KERN_ERR "hdmicec:%s:Cannot use GPIO %d, err:%d\n",__func__,TDA_IRQ_CALIB,err);
      goto i2c_out;
   }
   /* turn GPIO into IRQ */
   gpio_direction_input(TDA_IRQ_CALIB);
   msleep(1);
   if (request_irq(gpio_to_irq(TDA_IRQ_CALIB), \
                   tda_irq, IRQF_TRIGGER_FALLING|IRQF_DISABLED, "TDA IRQ", NULL)) {
      printk(KERN_ERR "hdmicec:%s:Cannot request irq, err:%d\n",__func__,err);
      gpio_free(TDA_IRQ_CALIB);
      goto i2c_out;
   }
#endif

   err = hdmi_cec_init(this, (const char *)client->dev.platform_data);
   if (err) goto i2c_out;

   if (get_hpd_status()) {
      cec_on(this);
      disable_irq(gpio_to_irq(TDA_IRQ_CALIB));
      cec_interrupt(NULL); /* initiate polling */
      enable_irq(gpio_to_irq(TDA_IRQ_CALIB));
   }
   else {
      cec_standby(this);
   }

   return 0;

 i2c_out:
   LOG(KERN_INFO,"HDMICEC eject: this->driver.i2c_client removed\n");
   tmdlHdmiCecClose(this->cec.inst);

   this->driver.i2c_client = NULL;

   return err;
}

/*
 *  I2C client :: destroy
 */
static int this_i2c_remove(struct i2c_client *client)
{
   cec_instance *this=&our_instance;
   int err=0;

   LOG(KERN_INFO,"called\n");

   err=tmdlHdmiCecClose(this->cec.inst);

   if (!client->adapter) {
      dev_err(&this->driver.i2c_client->dev, "<%s> No CEC Device \n",
              __func__);
      return -ENODEV;
   }

   this->driver.i2c_client = NULL;

   return err;
}

/*
 *  I2C client driver (backend)
 *  -----------------
 */
static struct i2c_driver this_i2c_driver = {
   .driver = {
      .owner = THIS_MODULE,
      .name = CEC_NAME,
   },
   .probe = this_i2c_probe,
   .remove = this_i2c_remove,
   .id_table = this_i2c_id,
};

/*
 *  ioctl driver (userland frontend)
 *  ------------
 */
static struct file_operations this_cdev_fops = {
 owner:    THIS_MODULE,
 open:     this_cdev_open,
 release:  this_cdev_release,
 unlocked_ioctl: this_cdev_ioctl,
 read:     this_cdev_read,
 write:    this_cdev_write,
 poll:     this_cdev_poll
};

/*
 *  Module :: start up
 */
static int __init cec_init(void)
{
   cec_instance *this=&our_instance;
   dev_t dev=0;
   int err=0;

   /* 
      general device context
   */
   memset(this,0,sizeof(cec_instance));
   this->param.verbose = param_verbose;
   this->param.major = param_major;
   this->param.minor = param_minor;
   this->param.passive = 1;
   
   /* Hello word */
   printk(KERN_INFO "%s(%s) %d.%d.%d compiled: %s %s %s\n", HDMICEC_NAME, TDA_NAME, TDA_VERSION_MAJOR,
          TDA_VERSION_MINOR, TDA_VERSION_PATCHLEVEL, __DATE__, __TIME__, TDA_VERSION_EXTRA);
   if (this->param.verbose) LOG(KERN_INFO,".verbose mode\n");

   /*
     plug I2C (backend : Hw interfacing)
   */
   err = i2c_add_driver(&this_i2c_driver);
   if (err < 0) {
      printk(KERN_ERR "Driver registration failed\n");
      return -ENODEV;
   }

   if (this->driver.i2c_client == NULL) {
      printk(KERN_ERR "this->driver.i2c_client not allocated\n");
      err = -ENODEV;
      goto init_out;
   }

   /*
     cdev init (userland frontend)
   */

   /* arbitray range of device numbers */
   if (this->param.major) {
      /* user force major number @ insmod */
      dev = MKDEV(this->param.major, this->param.minor);
      err = register_chrdev_region(dev,MAX_MINOR,HDMICEC_NAME);
      if (err) {
         printk(KERN_ERR "unable to register %s, dev=%d %s\n",HDMICEC_NAME,dev,ERR_TO_STR(err));
         goto init_out;
      }
   } else {
      /* fully dynamic major number */
      err = alloc_chrdev_region(&dev, this->param.minor, MAX_MINOR,HDMICEC_NAME);
      if (err) {
         printk(KERN_ERR "unable to alloc chrdev region for %s, dev=%d %s\n",HDMICEC_NAME,dev,ERR_TO_STR(err));
         goto init_out;
      }
      this->param.major = MAJOR(dev);
   }

   cdev_init(&our_cdev, &this_cdev_fops);
   our_cdev.owner = THIS_MODULE;

   this->driver.class = class_create(THIS_MODULE, HDMICEC_NAME);
   if (IS_ERR(this->driver.class)) {
      printk(KERN_INFO "Error creating mmap device class.\n");
      err =-EIO;
      goto init_out;
   }
   this->driver.dev = device_create(this->driver.class, NULL, dev, NULL, HDMICEC_NAME);

   this->driver.devno = dev;
   err = cdev_add(&our_cdev, this->driver.devno, MAX_MINOR);
   if (err){
      printk(KERN_INFO "unable to add device for %s, ipp_driver.devno=%d %s\n",HDMICEC_NAME,this->driver.devno,ERR_TO_STR(err));
      device_destroy(this->driver.class,this->driver.devno);
      class_destroy(this->driver.class);
      unregister_chrdev_region(this->driver.devno, MAX_MINOR);
      goto init_out;
   }   

#ifdef TWL4030_HACK
   /* AL : hack to bypass keypad */
   gkp_input = get_twm4030_input();
#endif

   /* 
      general device context
   */
   sema_init(&this->driver.sem,1);
   this->driver.deinit_req=0;
   
   return 0;

 init_out:
   i2c_del_driver(&this_i2c_driver);
   return err;
}

/*
 *  Module :: shut down
 */
static void __exit cec_exit(void)
{
   cec_instance *this=&our_instance;
   
   LOG(KERN_INFO,"called\n");
   
#ifndef IRQ
   free_irq(gpio_to_irq(TDA_IRQ_CALIB), NULL);
#endif
   
   unregister_cec_interrupt();
   this->driver.deinit_req=1;
#ifndef IRQ
   if (wait_event_interruptible(this->driver.wait,this->driver.deinit_req>1)) {
      /* oups... just wait... */
      msleep(CHECK_EVERY_XX_MS*20);
   }
#endif
   
#ifndef IRQ
   /* release GPIO */ 
   gpio_free(TDA_IRQ_CALIB);
#endif

   /* unregister cdevice */
   cdev_del(&our_cdev);
   unregister_chrdev_region(this->driver.devno, MAX_MINOR);

   /* unregister device */
   device_destroy(this->driver.class,this->driver.devno);
   class_destroy(this->driver.class);

   /* unregister i2c */
   i2c_del_driver(&this_i2c_driver);
}


/*
 *  Module
 *  ------
 */
 late_initcall(cec_init); 
//module_init(cec_init);
module_exit(cec_exit);

/*
 *  Disclamer
 *  ---------
 */
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Andre Lepine <andre.lepine@nxp.com>");
MODULE_DESCRIPTION(HDMICEC_NAME " driver");

