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

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/semaphore.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/i2c.h>
#include <linux/delay.h>

#include "tda998x_i2c.h"


/*
 * Write bytes into consecutive device registers
 */
int tda998x_i2c_write_regs(struct i2c_client *client, u8 len, u8 reg, u8 *val)
{
   u8 i, data[256];
   int err, retries;
   struct i2c_msg msg[1];
   bool is_cec_data = (reg == 7 && client->addr != 0x70);

   retries = (is_cec_data) ?  1 : 3;

   for (;;) {
      msg[0].addr  = client->addr;
      msg[0].flags = 0;
      msg[0].len   = len + 1;
      msg[0].buf   = data;
   
      data[0] = reg;   
      for (i = 0; i < len; i++) 
         data[i+1] = val[i];
   
      err = i2c_transfer(client->adapter, msg, 1);
      udelay(50);

      if (err < 0) {
         if (len <= 1)
            dev_err(&client->dev, "<%s> i2c write error at=%x "
		    "val=%x flags=%d err=%d\n",
		    __func__, reg, val[0], msg[0].flags, err);
         else
            dev_err(&client->dev, "<%s> i2c block write error at=%x "
		    "*val=%x bytes=%d flags=%d err=%d\n",
		    __func__, reg, val[0], len, msg[0].flags, err);
      }
#ifdef CEC_I2C_DEBUG
      else if (is_cec_data) {
         extern char *cec_opcode(int op);
         unsigned char initiator, receiver;

         for (i = len + 1; i < 8; i++)
            data[i] = 0xff;

         initiator = data[3] >> 4;
         receiver  = data[3] & 0x0f;
         if (len == 3) {
            printk(KERN_INFO "hdmicec:poll:[%x--->%x] \n", initiator,receiver);
         }
         else if (len > 3) {
            printk(KERN_INFO "hdmicec:Tx:[%x--->%x] %s %02x%02x%02x%02x\n",
                   initiator,receiver,cec_opcode(data[4]),
                   data[4],data[5],data[6],data[7]);
         }
         else {
            printk(KERN_INFO "hdmicec:??: invalid data length (%d)\n", len); 
         }
      }
#endif

      if (err >= 0 || --retries == 0)
         break;

      dev_info(&client->dev, "Retrying I2C... %d\n", retries);
      set_current_state(TASK_UNINTERRUPTIBLE);
      schedule_timeout(msecs_to_jiffies(20));
   }

   return (err < 0) ? err : 0;
}

/*
 * Read bytes from consecutive device registers
 */
int tda998x_i2c_read_regs(struct i2c_client *client, u8 len, u8 reg, u8 *val)
{
   int err;
   struct i2c_msg msg[2];

   msg[0].addr  = client->addr;
   msg[0].flags = 0;
   msg[0].len   = 1;
   msg[0].buf   = &reg;

   msg[1].addr  = client->addr;
   msg[1].flags = I2C_M_RD;
   msg[1].len   = len;
   msg[1].buf   = val;

   err = i2c_transfer(client->adapter, msg, 2);

   if (err < 0) {
      if (len <= 1)
         dev_err(&client->dev, "<%s> i2c read error at 0x%x, "
	         "*val=%x flags=%d err=%d\n",
	         __func__, reg, *val, msg[0].flags, err);
      else
         dev_err(&client->dev, "<%s> i2c block read error at 0x%x, "
	         "*val=%x bytes=%d flags=%d err=%d\n",
	         __func__, reg, *val, len, msg[0].flags, err);
   }

   return (err < 0) ? err : 0;
}


