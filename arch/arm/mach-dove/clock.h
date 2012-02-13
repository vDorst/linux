struct clk;

struct clkops {
	void			(*enable)(struct clk *);
	void			(*disable)(struct clk *);
	unsigned long		(*getrate)(struct clk *);
	int			(*setrate)(struct clk *, unsigned long rate);
};

struct clk {
	const struct clkops     *ops;
	unsigned long		*rate;
	u32			flags;
	__s8                    usecount;
	u32			mask;
};

/* Clock flags */
#define ALWAYS_ENABLED          1

int dove_devclks_init(void);
unsigned int  dove_tclk_get(void);
int dove_clk_config(struct device *dev, const char *id, unsigned long rate);
void ds_clks_disable_all(int include_pci0, int include_pci1);
