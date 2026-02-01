/*
 * C910 MHCR Register Reader
 * Kernel module to read and parse entire mhcr/shcr register
 * Based on C910 User Manual 16.1.7.2 M-mode hardware configuration register
 *
 * Creates /proc/c910_predictors for userspace reading
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Security Research");
MODULE_DESCRIPTION("Read and parse C910 mhcr register via shcr");

#define PROC_NAME "c910_predictors"

// S-mode hardware control register (mapped from mhcr)
#define SHCR_ADDR 0x5c8

// MHCR bit definitions (from C910 manual section 16.1.7.2)
#define MHCR_IE_BIT     0   // I-Cache enable
#define MHCR_DE_BIT     1   // D-Cache enable
#define MHCR_WA_BIT     2   // Write allocate
#define MHCR_WB_BIT     3   // Writeback (fixed to 1)
#define MHCR_RS_BIT     4   // Return stack enable
#define MHCR_BPE_BIT    5   // Branch prediction enable
#define MHCR_BTB_BIT    6   // Branch target buffer enable
#define MHCR_IBPE_BIT   7   // Indirect branch prediction enable
#define MHCR_WBR_BIT    8   // Write burst (fixed to 1)
#define MHCR_L0BTB_BIT  12  // Level-0 BTB enable
#define MHCR_SCK_BIT    18  // System clock ratio (bits 18:17)

static struct proc_dir_entry *proc_entry;

static unsigned long read_shcr(void)
{
    unsigned long value;
    asm volatile("csrr %0, 0x5C1" : "=r"(value));
    return value;
}

// Proc file show function - called when userspace reads /proc/c910_predictors
static int c910_predictors_show(struct seq_file *m, void *v)
{
    unsigned long reg_value;
    int ie, de, wa, wb, rs, bpe, btb, ibpe, wbr, l0btb, sck;

    // Read current SHCR value
    reg_value = read_shcr();

    // Extract all bit fields
    ie    = (reg_value >> MHCR_IE_BIT) & 1;
    de    = (reg_value >> MHCR_DE_BIT) & 1;
    wa    = (reg_value >> MHCR_WA_BIT) & 1;
    wb    = (reg_value >> MHCR_WB_BIT) & 1;
    rs    = (reg_value >> MHCR_RS_BIT) & 1;
    bpe   = (reg_value >> MHCR_BPE_BIT) & 1;
    btb   = (reg_value >> MHCR_BTB_BIT) & 1;
    ibpe  = (reg_value >> MHCR_IBPE_BIT) & 1;
    wbr   = (reg_value >> MHCR_WBR_BIT) & 1;
    l0btb = (reg_value >> MHCR_L0BTB_BIT) & 1;
    sck   = (reg_value >> MHCR_SCK_BIT) & 0x3;

    // Print in standard /proc format (key: value)
    seq_printf(m, "mhcr:\t\t\t0x%016lx\n", reg_value);
    seq_printf(m, "icache_enable:\t\t%d\n", ie);
    seq_printf(m, "dcache_enable:\t\t%d\n", de);
    seq_printf(m, "write_allocate:\t\t%d\n", wa);
    seq_printf(m, "writeback:\t\t%d\n", wb);
    seq_printf(m, "write_burst:\t\t%d\n", wbr);
    seq_printf(m, "branch_prediction:\t%d\n", bpe);
    seq_printf(m, "btb:\t\t\t%d\n", btb);
    seq_printf(m, "indirect_branch_pred:\t%d\n", ibpe);
    seq_printf(m, "return_stack:\t\t%d\n", rs);
    seq_printf(m, "l0btb:\t\t\t%d\n", l0btb);
    seq_printf(m, "clock_ratio:\t\t%d:1\n", sck + 1);

    return 0;
}

// Proc file open function
static int c910_predictors_open(struct inode *inode, struct file *file)
{
    return single_open(file, c910_predictors_show, NULL);
}

// Proc file operations
static const struct proc_ops c910_predictors_ops = {
    .proc_open = c910_predictors_open,
    .proc_read = seq_read,
    .proc_lseek = seq_lseek,
    .proc_release = single_release,
};

static int __init check_predictor_init(void)
{
    printk(KERN_INFO "C910: Module init starting\n");

    // Create /proc/c910_predictors
    proc_entry = proc_create(PROC_NAME, 0444, NULL, &c910_predictors_ops);
    if (!proc_entry) {
        printk(KERN_ERR "C910: Failed to create /proc/%s\n", PROC_NAME);
        return -ENOMEM;
    }

    printk(KERN_INFO "C910: predictor check module loaded\n");
    printk(KERN_INFO "C910: Read predictor status: cat /proc/%s\n", PROC_NAME);

    return 0;
}

static void __exit check_predictor_exit(void)
{
    // Remove /proc/c910_predictors
    proc_remove(proc_entry);
    printk(KERN_INFO "C910: predictor check module unloaded\n");
}

module_init(check_predictor_init);
module_exit(check_predictor_exit);
