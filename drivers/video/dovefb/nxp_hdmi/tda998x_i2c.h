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

#ifndef __i2c_h__
#define __i2c_h__

#define CEC_I2C_DEBUG

struct i2c_client *txGetThisI2cClient(void);
struct i2c_client *cecGetThisI2cClient(void);

int tda998x_i2c_read_regs(struct i2c_client *client, u8 len, u8 reg, u8 *val);
int tda998x_i2c_write_regs(struct i2c_client *client, u8 len, u8 reg, u8 *val);

#endif

