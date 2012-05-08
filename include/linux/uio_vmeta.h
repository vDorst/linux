/*
 * This software program is licensed subject to the GNU General Public License
 * (GPL).Version 2,June 1991, available at http://www.fsf.org/copyleft/gpl.html

 * (C) Copyright 2010 Marvell International Ltd.
 * All Rights Reserved
 */

#ifndef __UIO_VMETA_H
#define __UIO_VMETA_H

typedef unsigned int vmeta_instance_status;
typedef struct _id_instance {
	vmeta_instance_status	status;
	int						height;
	int						width;
	int						frame_rate;
	pid_t					pid;
	unsigned int			pt; /* pthread_t */
} id_instance;

#define MAX_VMETA_INSTANCE 32

typedef enum _VMETA_LOCK_FLAG {
	VMETA_LOCK_OFF = 0,
	VMETA_LOCK_ON,
	VMETA_LOCK_FORCE_INIT
} VMETA_LOCK_FLAG;

/* This struct should be aligned with user space API */
typedef struct _kernel_share {
	int ref_count;
	VMETA_LOCK_FLAG lock_flag;
	int active_user_id;
	id_instance user_id_list[MAX_VMETA_INSTANCE];
} kernel_share;

#define IOP_MAGIC	('v')

#define VMETA_CMD_POWER_ON		_IO(IOP_MAGIC, 0)
#define VMETA_CMD_POWER_OFF		_IO(IOP_MAGIC, 1)
#define VMETA_CMD_CLK_ON		_IO(IOP_MAGIC, 2)
#define VMETA_CMD_CLK_OFF		_IO(IOP_MAGIC, 3)
#define VMETA_CMD_CLK_SWITCH		_IO(IOP_MAGIC, 4)
#define VMETA_CMD_LOCK			_IO(IOP_MAGIC, 5)
#define VMETA_CMD_UNLOCK		_IO(IOP_MAGIC, 6)
#define VMETA_CMD_PRIV_LOCK		_IO(IOP_MAGIC, 7)
#define VMETA_CMD_PRIV_UNLOCK		_IO(IOP_MAGIC, 8)
#define VMETA_CMD_SUSPEND_CHECK		_IOR(IOP_MAGIC, 9, int)
#define VMETA_CMD_SUSPEND_READY		_IO(IOP_MAGIC, 10)
#define VMETA_CMD_SUSPEND_SET		_IO(IOP_MAGIC, 11)
#define VMETA_CMD_SUSPEND_UNSET		_IO(IOP_MAGIC, 12)

#endif /* __UIO_VMETA_H */
