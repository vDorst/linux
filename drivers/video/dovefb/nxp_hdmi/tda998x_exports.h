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

#ifndef __exports__
#define __exports__

void register_cec_interrupt(cec_callback_t fct); 
void unregister_cec_interrupt(void); 

int hdmi_enable(void);
int hdmi_disable(int event_tracking);
void reset_hdmi(int hdcp_module);

int edid_received(void);
short edid_phy_addr(void); 
tmPowerState_t get_hdmi_status(void);
tmPowerState_t get_hpd_status(void);

#endif

