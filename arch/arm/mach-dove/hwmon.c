/*
 * hwmon-dove.c - temperature monitoring driver for Dove SoC
 *
 * Inspired from other hwmon drivers
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301 USA.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/jiffies.h>
#include <linux/delay.h>
#include <linux/hwmon.h>
#include <linux/sysfs.h>
#include <linux/hwmon-sysfs.h>
#include <linux/err.h>
#include <linux/list.h>
#include <linux/platform_device.h>
#include <linux/cpu.h>
#include <linux/gpio.h>
#include <linux/io.h>
#include <mach/bridge-regs.h>
#include <mach/pm.h>

#define DOVE_OVERHEAT_TEMP	105000		/* milidegree Celsius */
#define DOVE_OVERHEAT_DELAY	0x700
#define DOVE_OVERCOOL_TEMP	10000		/* milidegree Celsius */
#define	DOVE_OVERCOOL_DELAY	0x700
#define DOVE_OVERHEAT_MIN	0
#define DOVE_OVERHEAT_MAX	110000
#define DOVE_OVERCOOL_MIN	0
#define DOVE_OVERCOOL_MAX	110000

/* Junction Temperature */
#define DOVE_TSEN_TEMP2RAW(x) ((2281638-(10*x))/7298)    /* in millCelsius */
#define DOVE_TSEN_RAW2TEMP(x) ((2281638-(7298*x))/10)	/* in miliCelsius */
#define DOVE_TSEN_RAW2VOLT(x)  (((x) + 241)*100000/39619)

#define LABEL "T-junction"

/*
 * Implement hysteresis like temperature up and down.
 * When die junction temperature reaches LIMIT1_UP set warning and start
 * lowering it by decreasing CPU frequency.
 * When temperature reaches LIMIT1_DOWN then get CPU back to max speed.
 * LIMIT_SHUTDOWN is hard limit to minimize power consumption by shutting down
 * as much as possible.
 * Those temperatures are all die junction temperatures. Ambient should be much
 * less than those (typically 50-60c).
 */
#define LIMIT1_UP		105000 /* 100c */
#define LIMIT1_DOWN		95000  /* 90c */
#define LIMIT_SHUTDOWN		120000 /* 120c */

static struct device *hwmon_dev;
static int warning_flag;
static int current_mode;
static unsigned int temp_min = DOVE_OVERCOOL_TEMP;
static unsigned int temp_max = DOVE_OVERHEAT_TEMP;

#define SENSOR_TEMP 1
#define SENSOR_VOLT_CPU 2
#define SENSOR_VOLT_CORE 3
static struct timer_list thermal_check;

enum { SHOW_TEMP,
	TEMP_MAX,
	TEMP_MIN,
	SHOW_NAME,
	SHOW_TYPE,
	SHOW_LABEL,
	SHOW_VCORE,
	SHOW_VCPU
} SHOW;

/* CPU Clock Speed - DFS */
enum DOVE_CPU_SPEED {
	CPU_CLOCK_TURBO = 0x0,
	CPU_CLOCK_SLOW = 0x1
};

static void dovetemp_set_thresholds(unsigned int max, unsigned int min)
{
	u32 temp, reg;

	/* Set the overheat threashold & delay */
	temp = DOVE_TSEN_TEMP2RAW(max);
	reg = readl(PMU_THERMAL_MNGR_REG);
	reg &= ~PMU_TM_OVRHEAT_THRSH_MASK;
	reg |= (temp << PMU_TM_OVRHEAT_THRSH_OFFS);
	writel(reg, PMU_THERMAL_MNGR_REG);

	/* Set the overcool threshole & delay */
	temp = DOVE_TSEN_TEMP2RAW(min);
	reg = readl(PMU_THERMAL_MNGR_REG);
	reg &= ~PMU_TM_COOL_THRSH_MASK;
	reg |= (temp << PMU_TM_COOL_THRSH_OFFS);
	writel(reg, PMU_THERMAL_MNGR_REG);
}
static int dovetemp_init_sensor(void)
{
	u32 reg;
	u32 i;

	/* Configure the Diode Control Register #0 */
	reg = readl(PMU_TEMP_DIOD_CTRL0_REG);
	/* Use average of 2 */
	reg &= ~PMU_TDC0_AVG_NUM_MASK;
	reg |= (0x1 << PMU_TDC0_AVG_NUM_OFFS);
	/* Reference calibration value */
	reg &= ~PMU_TDC0_REF_CAL_CNT_MASK;
	reg |= (0x0F1 << PMU_TDC0_REF_CAL_CNT_OFFS);
	/* Set the high level reference for calibration */
	reg &= ~PMU_TDC0_SEL_VCAL_MASK;
	reg |= (0x2 << PMU_TDC0_SEL_VCAL_OFFS);
	writel(reg, PMU_TEMP_DIOD_CTRL0_REG);

	/* Reset the sensor */
	reg = readl(PMU_TEMP_DIOD_CTRL0_REG);
	writel((reg | PMU_TDC0_SW_RST_MASK), PMU_TEMP_DIOD_CTRL0_REG);
	writel(reg, PMU_TEMP_DIOD_CTRL0_REG);

	/* Enable the sensor */
	reg = readl(PMU_THERMAL_MNGR_REG);
	reg &= ~PMU_TM_DISABLE_MASK;
	writel(reg, PMU_THERMAL_MNGR_REG);

	/* Poll the sensor for the first reading */
	for (i = 0; i < 1000000; i++) {
		reg = readl(PMU_THERMAL_MNGR_REG);
		if (reg & PMU_TM_CURR_TEMP_MASK)
			break;
	}

	if (i == 1000000)
		return -EIO;

	/* Set thresholds */
	dovetemp_set_thresholds(temp_max, temp_min);

	/* Set delays */
	writel(DOVE_OVERHEAT_DELAY, PMU_TM_OVRHEAT_DLY_REG);
	writel(DOVE_OVERCOOL_DELAY, PMU_TM_COOLING_DLY_REG);

	current_mode = 0;
	warning_flag = 0;
	return 0;
}

static void set_sensor_mode(int mode)
{
	u32 reg;

	if ((mode == SENSOR_TEMP) && (current_mode == SENSOR_TEMP))
		return;

	dovetemp_init_sensor();
	reg = readl(PMU_TEMP_DIOD_CTRL0_REG);
	if (mode == SENSOR_TEMP) {
		printk(KERN_INFO "switching to TEMP\n");

		reg &= ~(PMU_TDC0_SEL_IP_MODE_MASK);
	} else if (mode == SENSOR_VOLT_CPU) {
		printk(KERN_INFO "switching to CPU\n");

		reg |= PMU_TDC0_SEL_IP_MODE_MASK;
		reg &= ~(PMU_TDC0_SEL_VSEN_MASK);

	} else if (mode == SENSOR_VOLT_CORE) {
		printk(KERN_INFO "switching to CORE\n");

		reg |= PMU_TDC0_SEL_IP_MODE_MASK;
		reg |= PMU_TDC0_SEL_VSEN_MASK;
	} else {
		printk(KERN_ERR "dove-hwmon: invalid mode (%d)!\n", mode);
	}

	writel(reg, PMU_TEMP_DIOD_CTRL0_REG);
	reg = readl(PMU_TEMP_DIOD_CTRL1_REG);
	reg ^= PMU_TDC1_STRT_CAL_MASK;
	writel(reg, PMU_TEMP_DIOD_CTRL1_REG);

	current_mode = mode;
	mdelay(5);
}

static int dovetemp_read_temp(void)
{
	u32 reg = 0;
	u32 reg1 = 0;
	u32 i;

	set_sensor_mode(SENSOR_TEMP);
	/* Verify that the temperature is valid */
	reg = readl(PMU_TEMP_DIOD_CTRL1_REG);
	if ((reg & PMU_TDC1_TEMP_VLID_MASK) == 0x0)
		return -EIO;

	/*
	 * Read the thermal sensor looking two successive readings that differ
	 * in LSb only.
	 */
	for (i = 0; i < 16; i++) {
		/* Read the raw temperature */
		reg = readl(PMU_THERMAL_MNGR_REG);
		reg &= PMU_TM_CURR_TEMP_MASK;
		reg >>= PMU_TM_CURR_TEMP_OFFS;

		if (((reg ^ reg1) & 0x1FE) == 0x0)
			break;
		/* save the current reading for the next iteration */
		reg1 = reg;
	}

	if (i == 16)
		printk(KERN_WARNING "dove-hwmon: Thermal sensor is unstable!\n");

	/* Convert the temperature to Celsuis */
	return DOVE_TSEN_RAW2TEMP(reg);
}

static int dovetemp_read_volt(int mode)
{
	u32 reg = 0;
	u32 reg1 = 0;
	u32 i;

	set_sensor_mode(mode);
	/* Verify that the temperature is valid */
	reg = readl(PMU_TEMP_DIOD_CTRL1_REG);
	if ((reg & PMU_TDC1_TEMP_VLID_MASK) == 0x0)
		return -EIO;

	/*
	 * Read the thermal sensor looking two successive readings that differ
	 * in LSb only.
	 */
	for (i = 0; i < 16; i++) {
		/* Read the raw temperature */
		reg = readl(PMU_THERMAL_MNGR_REG);
		reg &= PMU_TM_CURR_TEMP_MASK;
		reg >>= PMU_TM_CURR_TEMP_OFFS;

		if (((reg ^ reg1) & 0x1FE) == 0x0)
			break;
		/* save the current reading for the next iteration */
		reg1 = reg;
	}

	if (i == 16)
		printk(KERN_WARNING "dove-hwmon: Thermal sensor is unstable!\n");

	/* Convert the temperature to Celsuis */
	return DOVE_TSEN_RAW2VOLT(reg);
}

static void local_cpu_freq_scale(enum DOVE_CPU_SPEED speed)
{
	unsigned long ints;
	u32 mc;
	local_irq_save(ints);
	mc = readl(IRQ_MASK_HIGH);
	/* Temporarily unmask pmu interrupt */
	writel(mc | 0x2, IRQ_MASK_HIGH);
#if 0 /* Currently unsupportd. Will add support later */
	stat = mvPmuCpuFreqScale(speed);
#endif
	writel(mc | 0x2, IRQ_MASK_HIGH);
	local_irq_restore(ints);
}
/* Thermal check routine */
static inline void dovetemp_checktemp(unsigned long data)
{
	int temp = dovetemp_read_temp();
	u32 mc, mc2, reg;
	unsigned long ints;
	if (temp >= LIMIT_SHUTDOWN) {
		/* Shutdown as much as possible */
		printk(KERN_CRIT "Reached maximum thermal allowed. Shutting down\n");
		local_irq_save(ints);
		/* Disable external USB current limiter */
		gpio_direction_output(1, 0);
		local_cpu_freq_scale(CPU_CLOCK_SLOW);
		/* eSata - 40mA */
		writel(0x009B1215, DOVE_SB_REGS_VIRT_BASE + 0x0a2050);
		/* PCI-E clocks - 10mA */
		writel(0x0003007f, DOVE_SB_REGS_VIRT_BASE + 0x0d0058);
		/* SMI power down phy - ~20mA on fast ethernet */
		writel(0x00010800, DOVE_SB_REGS_VIRT_BASE + 0x072004);
		msleep(100);
		/* gigE MAC - digital part */
		writel(0x00000002, DOVE_SB_REGS_VIRT_BASE + 0x0720b0);
		/* PCI-E clocks and gigE I/Os = 10mA */
		writel(0x0003007f, DOVE_SB_REGS_VIRT_BASE + 0x0d0058);

		/* disable GPU and Video engine isolators */
		reg = readl(PMU_ISO_CTRL_REG);
		reg &= ~(PMU_ISO_GPU_MASK | PMU_ISO_VIDEO_MASK);
		writel(reg, PMU_ISO_CTRL_REG);

		/* reset GPU and video engine units */
		reg = readl(PMU_SW_RST_CTRL_REG);
		reg &= ~(PMU_SW_RST_GPU_MASK | PMU_SW_RST_VIDEO_MASK);
		writel(reg, PMU_SW_RST_CTRL_REG);

		/* power off GPU and video engine*/
		reg = readl(PMU_PWR_SUPLY_CTRL_REG);
		reg |= (PMU_PWR_GPU_PWR_DWN_MASK | PMU_PWR_VPU_PWR_DWN_OFFS);
		writel(reg, PMU_PWR_SUPLY_CTRL_REG);

		/* Set PWM to no-select - Will reduce LED power consumption */
		reg = readl(DOVE_SB_REGS_VIRT_BASE + 0x0d0208);
		reg &= ~0xf00;
		reg |= 0x100;
		writel(reg, DOVE_SB_REGS_VIRT_BASE + 0x0d0208);

		/* Set GPIO 18 to 1 */
		reg = readl(DOVE_SB_REGS_VIRT_BASE + 0x0d0400);
		reg |= (1 << 18);
		writel(reg, DOVE_SB_REGS_VIRT_BASE + 0x0d0400);

		/* USB 0 and 1 phy shutdown */
		writel(0xff000160, DOVE_SB_REGS_VIRT_BASE + 0x050400);
		writel(0xff000160, DOVE_SB_REGS_VIRT_BASE + 0x051400);
		/* Clock gating of unused south bridge units */
		writel(0xff3800c4, DOVE_SB_REGS_VIRT_BASE + 0x0d0038);

		/* Disable all interrupts besides uart0 */
		mc = readl(IRQ_MASK_LOW);
		mc2 = readl(IRQ_MASK_HIGH);
		writel(IRQ_MASK_LOW, 0x80);
		writel(IRQ_MASK_HIGH, 0x0);
		printk(KERN_CRIT "Just before entering deep sleep\n");
		/* The following will put DDR in self-refresh mode */
#if 0 /* Currently unsupported */
		/* Put DDR in self-refresh */
#endif
		__asm__ __volatile__("wfi\n");
		/* Should never reach here */
		panic("Should never reach here function\n");
		local_irq_restore(ints);
	}
	if (temp >= LIMIT1_UP && !warning_flag) {
		warning_flag = 1;
		printk(KERN_WARNING "Die temperature exceeded limit (%d mili C)\n",
			temp);

		/* Limit CPU speed to DDR speed */
		local_cpu_freq_scale(CPU_CLOCK_SLOW);
	}
	if (temp <= LIMIT1_DOWN && warning_flag) {
		warning_flag = 0;
		printk(KERN_INFO "Die temperature went below limit (%d mili C)\n",
			temp);
		/* Remove CPU speed limit */
		local_cpu_freq_scale(CPU_CLOCK_TURBO);
	}
	if (warning_flag) {
		/*
		 * Make sure external frequency scaling mechanism, if being
		 * used, didn't change CPU freq.
		 */
		local_cpu_freq_scale(CPU_CLOCK_SLOW);
	}
	mod_timer(&thermal_check, jiffies + (HZ));
}
/*
 * Sysfs stuff
 */

static ssize_t show_name(struct device *dev, struct device_attribute
			  *devattr, char *buf) {
	return sprintf(buf, "%s\n", "dove-hwmon");
}

static ssize_t show_alarm(struct device *dev, struct device_attribute
			  *devattr, char *buf)
{
	int alarm = 0;
	u32 reg;

	reg = readl(PMU_INTERRUPT_CAUSE);
	if (reg & PMU_INTERRUPT_OVRHEAT_MASK) {
		alarm = 1;
		writel((reg & ~PMU_INTERRUPT_OVRHEAT_MASK),
			PMU_INTERRUPT_CAUSE);
	} else if (reg & PMU_INTERRUPT_COOLING_MASK) {
		alarm = 2;
		writel((reg & ~PMU_INTERRUPT_COOLING_MASK),
			PMU_INTERRUPT_CAUSE);
	}

	return sprintf(buf, "%d\n", alarm);
}

static ssize_t show_info(struct device *dev,
			 struct device_attribute *devattr, char *buf) {
	int ret;
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);

	if (attr->index == SHOW_TYPE)
		ret = sprintf(buf, "%d\n", 3);
	else if (attr->index == SHOW_LABEL)
		ret = sprintf(buf, "%s\n", LABEL);
	else
		ret = sprintf(buf, "%d\n", -1);
	return ret;
}

static ssize_t show_temp(struct device *dev,
			 struct device_attribute *devattr, char *buf) {
	int ret;
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);

	if (attr->index == SHOW_TEMP)
		ret = sprintf(buf, "%d\n", dovetemp_read_temp());
	else if (attr->index == TEMP_MAX)
		ret = sprintf(buf, "%d\n", temp_max);
	else if (attr->index == TEMP_MIN)
		ret = sprintf(buf, "%d\n", temp_min);
	else
		ret = sprintf(buf, "%d\n", -1);

	return ret;
}

static ssize_t show_volt(struct device *dev,
			 struct device_attribute *devattr, char *buf) {
	int ret;
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);

	if (attr->index == SHOW_VCORE)
		ret = sprintf(buf, "%d\n",
			dovetemp_read_volt(SENSOR_VOLT_CORE));
	else if (attr->index == SHOW_VCPU)
		ret = sprintf(buf, "%d\n",
			dovetemp_read_volt(SENSOR_VOLT_CPU));
	else
		ret = sprintf(buf, "%d\n", -1);

	return ret;
}

static ssize_t set_temp(struct device *dev, struct device_attribute *devattr,
			 const char *buf, size_t count) {

	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	unsigned int temp;

	if (sscanf(buf, "%d", &temp) != 1)
		printk(KERN_WARNING "Invalid input string for temperature!");

	if (attr->index == TEMP_MAX) {
		if ((temp < DOVE_OVERHEAT_MIN) || (temp > DOVE_OVERHEAT_MAX))
			printk(KERN_WARNING "Invalid max temperature input (out of range: %d-%d)!",
				DOVE_OVERHEAT_MIN, DOVE_OVERHEAT_MAX);
		else {
			temp_max = temp;
			dovetemp_set_thresholds(temp_max, temp_min);
		}
	} else if (attr->index == TEMP_MIN) {
		if ((temp < DOVE_OVERCOOL_MIN) || (temp > DOVE_OVERCOOL_MAX))
			printk(KERN_WARNING "Invalid min temperature input (out of range: %d-%d)!",
				DOVE_OVERCOOL_MIN, DOVE_OVERCOOL_MAX);
		else {
			temp_min = temp;
			dovetemp_set_thresholds(temp_max, temp_min);
		}
	} else {
		printk(KERN_ERR "dove-temp: Invalid sensor attribute!");
	}
	printk(KERN_INFO "set_temp got string: %d\n", temp);

	return count;
}

/* TODO - Add read/write support in order to support setting max/min */
static SENSOR_DEVICE_ATTR(temp1_type, S_IRUGO, show_info, NULL,
			  SHOW_TYPE);
static SENSOR_DEVICE_ATTR(temp1_label, S_IRUGO, show_info, NULL,
			  SHOW_LABEL);
static SENSOR_DEVICE_ATTR(temp1_input, S_IRUGO, show_temp, NULL,
			  SHOW_TEMP);
static SENSOR_DEVICE_ATTR(temp1_max, S_IRWXUGO, show_temp, set_temp,
			  TEMP_MAX);
static SENSOR_DEVICE_ATTR(temp1_min, S_IRWXUGO, show_temp, set_temp,
			  TEMP_MIN);
static DEVICE_ATTR(temp1_crit_alarm, S_IRUGO, show_alarm, NULL);
static SENSOR_DEVICE_ATTR(vcore, S_IRUGO, show_volt, NULL, SHOW_VCORE);
static SENSOR_DEVICE_ATTR(vcpu, S_IRUGO, show_volt, NULL, SHOW_VCPU);

static SENSOR_DEVICE_ATTR(name, S_IRUGO, show_name, NULL, SHOW_NAME);

static struct attribute *dovetemp_attributes[] = {
	&sensor_dev_attr_name.dev_attr.attr,
	&dev_attr_temp1_crit_alarm.attr,
	&sensor_dev_attr_temp1_input.dev_attr.attr,
	&sensor_dev_attr_temp1_max.dev_attr.attr,
	&sensor_dev_attr_temp1_min.dev_attr.attr,
	&sensor_dev_attr_temp1_type.dev_attr.attr,
	&sensor_dev_attr_temp1_label.dev_attr.attr,
	&sensor_dev_attr_vcore.dev_attr.attr,
	&sensor_dev_attr_vcpu.dev_attr.attr,
	NULL
};

static const struct attribute_group dovetemp_group = {
	.attrs = dovetemp_attributes,
};

static int __devinit dovetemp_probe(struct platform_device *pdev)
{
	int err;
	err = dovetemp_init_sensor();
	if (err)
		goto exit;

	err = sysfs_create_group(&pdev->dev.kobj, &dovetemp_group);
	if (err)
		goto exit;

	hwmon_dev = hwmon_device_register(&pdev->dev);
	if (IS_ERR(hwmon_dev)) {
		dev_err(&pdev->dev, "Class registration failed (%d)\n",
			err);
		goto exit;
	}

	printk(KERN_INFO "Dove hwmon thermal sensor initialized.\n");
	init_timer(&thermal_check);
	thermal_check.data = 0;
	thermal_check.function = dovetemp_checktemp;
	thermal_check.expires = jiffies + (HZ);
	add_timer(&thermal_check);

	return 0;

exit:
	sysfs_remove_group(&pdev->dev.kobj, &dovetemp_group);
	return err;
}

static int __devexit dovetemp_remove(struct platform_device *pdev)
{
	struct dovetemp_data *data = platform_get_drvdata(pdev);

	hwmon_device_unregister(hwmon_dev);
	sysfs_remove_group(&pdev->dev.kobj, &dovetemp_group);
	platform_set_drvdata(pdev, NULL);
	kfree(data);
	return 0;
}

static int dovetemp_resume(struct platform_device *dev)
{
	return dovetemp_init_sensor();
}

static struct platform_driver dovetemp_driver = {
	.driver = {
		.owner = THIS_MODULE,
		.name = "dove-temp",
	},
	.probe = dovetemp_probe,
	.remove = __devexit_p(dovetemp_remove),
	.resume = dovetemp_resume,
};

static int __init dovetemp_init(void)
{
	return platform_driver_register(&dovetemp_driver);
}

static void __exit dovetemp_exit(void)
{
	platform_driver_unregister(&dovetemp_driver);
}

MODULE_AUTHOR("Marvell Semiconductors");
MODULE_DESCRIPTION("Marvell Dove SoC hwmon driver");
MODULE_LICENSE("GPL");

module_init(dovetemp_init)
module_exit(dovetemp_exit)
