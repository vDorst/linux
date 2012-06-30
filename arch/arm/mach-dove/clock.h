int dove_devclks_init(void);
unsigned int  dove_tclk_get(void);
int dove_clk_config(struct device *dev, const char *id, unsigned long rate);
void ds_clks_disable_all(int include_pci0, int include_pci1);
