/*
 * Andestech SPI controller driver
 *
 * Author: Nylon Chen
 *	nylon7@andestech.com
 *
 * 2020 (c) Andes Technology Corporation

 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */


#include <linux/clk.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/timer.h>
#include <linux/delay.h>
#include <linux/list.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/io.h>
#include <linux/spi/spi.h>

#define ANDES_SPI_NAME "atcspi200-spi"
#define ANDES_MAX_HZ	50000000
#define MAX_TRANSFER_LEN        512
#define CHUNK_SIZE              1
#define SPI_TIMEOUT             0x100000
#define SPI_BUS                	0
#define SPI_BASE               	0xf0b00000
#define NSPI_MAX_CS_NUM         1

/* SPI transfer format register */
#define SPI_CPHA                      (1UL << 0)
#define SPI_CPOL                      (1UL << 1)
#define SPI_SLAVE                     (1UL << 2)
#define SPI_LSB                       (1UL << 3)
#define SPI_MERGE                     (1UL << 7)
#define DATA_BITS(data_bits)          ((data_bits - 1) << 8)

/* SPI transfer control register */
#define SPI_TRANSCTRL_SLV_DATA_ONLY   (0x1 << 31)

// RD/WR transfer count
#define RD_TRANCNT(num)               ((num - 1) << 0)
#define WR_TRANCNT(num)               ((num - 1) << 12)

// SPI transfer mode
#define SPI_TRANSMODE_WRnRD           (0x0 << 24)
#define SPI_TRANSMODE_WRONLY          (0x1 << 24)
#define SPI_TRANSMODE_RDONLY          (0x2 << 24)
#define SPI_TRANSMODE_WR_RD           (0x3 << 24)
#define SPI_TRANSMODE_RD_WR           (0x4 << 24)
#define SPI_TRANSMODE_WR_DMY_RD       (0x5 << 24)
#define SPI_TRANSMODE_RD_DMY_WR       (0x6 << 24)
#define SPI_TRANSMODE_NONEDATA        (0x7 << 24)
#define SPI_TRANSMODE_DMY_WR          (0x8 << 24)
#define SPI_TRANSMODE_DMY_RD          (0x9 << 24)

/* SPI control register */
#define SPIRST                        (1UL << 0)
#define RXFIFORST                     (1UL << 1)
#define TXFIFORST                     (1UL << 2)
#define RXDMAEN                       (1UL << 3)
#define TXDMAEN                       (1UL << 4)
#define RXTHRES(num)                  (num << 8)
#define TXTHRES(num)                  (num << 16)

#define THRES_MASK_FIFO_16            (0x1f)
#define THRES_MASK_FIFO_128           (0xff)
#define RXTHRES_OFFSET                (8)
#define TXTHRES_OFFSET                (16)

/* SPI interrupt enable register */
#define SPI_RXFIFOOORINT              (1UL << 0)
#define SPI_TXFIFOOURINT              (1UL << 1)
#define SPI_RXFIFOINT                 (1UL << 2)
#define SPI_TXFIFOINT                 (1UL << 3)
#define SPI_ENDINT                    (1UL << 4)
#define SPI_SLVCMD                    (1UL << 5)

// SPI flags
#define SPI_FLAG_INITIALIZED          (1UL << 0)     // SPI initialized
#define SPI_FLAG_POWERED              (1UL << 1)     // SPI powered on
#define SPI_FLAG_CONFIGURED           (1UL << 2)     // SPI configured
#define SPI_FLAG_DATA_LOST            (1UL << 3)     // SPI data lost occurred
#define SPI_FLAG_MODE_FAULT           (1UL << 4)     // SPI mode fault occurred

#define IDREV		0x00	/* 0x00 ID and revision register */
#define TRANSFMT	0x10	/* 0x10 SPI transfer format register */
#define DIRECTIO	0x14	/* 0x14 SPI direct IO control register */
#define TRANSCTRL	0x20	/* 0x20 SPI transfer control register */
#define CMD		0x24	/* 0x24 SPI command register */
#define ADDR		0x28	/* 0x28 SPI address register */
#define DATA		0x2C	/* 0x2c SPI data register */
#define CTRL		0x30	/* 0x30 SPI conrtol register */
#define STATUS		0x34	/* 0x34 SPI status register */
#define INTREN		0x38	/* 0x38 SPI interrupt enable register */
#define INTRST		0x3C	/* 0x3c SPI interrupt status register */
#define TIMING		0x40	/* 0x40 SPI interface timing register */
#define MEMCTRL		0x50	/* 0x50 SPI memory access control register */
#define SLVST		0x60	/* 0x60 SPI slave status register */
#define SLVDATACNT	0x64	/* 0x64 SPI slave data count register */
#define CONFIG		0x7C	/* 0x7c Configuration register */

typedef struct _SPI_STATUS {
	uint8_t        	busy;         // transmitter/receiver busy flag
	uint8_t		data_lost;    // data lost: receiver overflow/transmit underflow (cleared on start of transfer operation)
	uint8_t		mode_fault;   // Mode fault detected; optional (cleared on start of transfer operation)
} SPI_STATUS

typedef struct _SPI_TRANSFER_INFO {
        uint8_t                 transfer_op;   // transfer operation: send/recv/transfer
        uint8_t                 *rx_buf;       // pointer to in data buffer
        uint8_t                 *tx_buf;       // pointer to out data buffer
        uint8_t                 *tx_buf_limit; // pointer to the end of in data buffer
        uint32_t                rx_cnt;        // number of data received
        uint32_t                tx_cnt;        // number of data sent
        uint8_t                 txfifo_refill; // The size of data SPI TX ISR refills one time.
} SPI_TRANSFER_INFO;


// SPI information (Run-time)
typedef struct _SPI_INFO {
        uint32_t   cb_event;      // event callback
        SPI_STATUS              status;        // SPI status flags
        SPI_TRANSFER_INFO       xfer;          // SPI transfer information
        uint8_t                 flags;         // SPI driver flags
        uint32_t                mode;          // SPI mode
        uint8_t                 txfifo_size;   // SPI HW TXFIFO size
        uint8_t                 data_bits;     // the size of one unit SPI data (1 ~ 32, defaults: 8 bits)
        uint32_t                data_num;      // num of the transfer data(use in auto-size)
        uint32_t                block_num;     // num of the transfer block(use in auto-size)
} SPI_INFO;


struct andes_spi {
	void __iomem    *regs;
	struct clk      *clk;
	SPI_INFO	*info
	struct completion done;         /* Wake-up from interrupt */ 
};


static void andes_spi_tx(struct andes_spi *spi, const void *data_out)
{
}

static int andes_spi_rx(struct andes_spi *spi,void *din, unsigned int bytes)
{
}

static void andes_spi_write(struct andes_spi *spi, int offset, u32 value)
{
	iowrite32(value, spi->regs + offset);
}

static u32 andes_spi_read(struct andes_spi *spi, int offset)
{
	return ioread32(spi->regs + offset);
}

static void spi_polling_spiactive(struct andes_spi *spi) {
	poll = andes_spi_read(spi,STATUS)
        while ((poll) & 0x1);
}

static uint32_t spi_get_txfifo_size(SPI_RESOURCES *spi) {
		int cfg = andes_spi_read(spi,CONFIG);
                return 2 << ((cfg >> 4) & 0x3);
        }
}

static void andes_spi_init(struct andes_spi *spi)
{
	spi->info->status.busy	= 0U;
	spi->info->status.data_lost  = 0U;
        spi->info->status.mode_fault = 0U;

	spi->info->xfer.rx_buf       = 0U;
        spi->info->xfer.tx_buf       = 0U;
        spi->info->xfer.tx_buf_limit = 0U;
        spi->info->xfer.rx_cnt       = 0U;
        spi->info->xfer.tx_cnt       = 0U;

        spi->info->mode              = 0U;
	spi->info->txfifo_size = spi_get_txfifo_size(spi);

	spi->info->flags = SPI_FLAG_INITIALIZED;

	return 0;
}

static void spi_irq_handler(struct andes_spi *spi) {
	uint32_t i, j, status, interen;
        uint32_t data = 0;
        uint32_t rx_num = 0;
        uint32_t event = 0;
        uint32_t have_clear_state = 0;

	// read status register
	status = andes_spi_read(spi,INTRST);

	if((status & SPI_RXFIFOOORINT) || (status & SPI_TXFIFOOURINT))
	{
		// TX FIFO underrun or RX FIFO overrun interrupt status
		spi->info->status.data_lost = 1U;
		event |= NDS_SPI_EVENT_DATA_LOST;
	}
	if(status & SPI_TXFIFOINT)
	{
		for(i=0; i<spi->info->xfer.txfifo_refill; i++) {
			data = 0;
			if(spi->info->xfer.tx_buf < spi->info->xfer.rx_buf_limit) {
			// handle the data frame format
				if (spi->info->data_bits <= 8) {
					data = *spi->info->xfer.tx_buf;
					spi->info->xfer.tx_buf++;
				} else if (spi->info->data_bits <= 16) {
					for (j = 0; j < 2; j++) {
						data |= *spi->info->xfer.tx_buf << j * 8;
						spi->info->xfer.tx_buf++;
                                        }
				} else {
					for (j = 0; j < 4; j++) {
                                                data |= *spi->info->xfer.tx_buf << j * 8;
                                                spi->info->xfer.tx_buf++;
					}
				}
			}
				andes_spi_write(spi,DATA,data);
				spi->info->xfer.tx_cnt++;
			else {
				andes_spi_write(spi,INTREN);
			}
		}

}

static irqreturn_t andes_andes_irq(int irq, void *dev_id)
{
	struct andes_spi *spi = dev_id;
	u32 interrupt_enable = andes_spi_read(spi, SPI_TXFIFOINT|SPI_ENDINT);

	if(interrupts &(SPI_RXFIFOINT|SPI_TXFIFOINT)) {
		andes_spi_write(spi,ATCSPI200_INTEREN,0);
		complete(&spi->done);
		return IRQ_HANDLED;
	}

	return IRQ_NONE;
}

static int
andes_spi_prep_transfer(struct andes_spi *spi, struct spi_device *device,struct spi_transfer *t)
{
	u32 tm;
	u8 div;
	tm = andes_spi_read(spi,ATCSPI200_TIMING);
	tm &= ~ATCSPI200_TIMING_SCLK_DIV_MASK;

	if(ANDES_MAX_HZ >= spi->clk)
		div = 0xff;
	else{
		for (div=0; div <0xff; div++){
			if(ANDES_MAX_HZ >=spi->clk / (2*(div+1)))
				break;
		}
	}

	tm |= div;
	andes_spi_write(spi,ATCSPI200_TIMING,tm);


}

static int andes_spi_start(struct andes_spi *spi,const u8* tx_ptr, u8* rx_ptr)
{
	int i,olen=0;
	int tc = andes_spi_read(spi,ATCSPI200_TRANSCTRL);

	tc &= (ATCSPI200_TRANSCTRL_WRTRANCNT_MASK|ATCSPI200_TRANSCTRL_RDTRANCNT_MASK|ATCSPI200_TRANSCTRL_TRANSMODE_MASK);
	if((*tx_ptr)&&spi->cmd_len)
		tc |= ATCSPI200_TRANSCTRL_TRANSMODE_WR;
	else if((tx_ptr))
		tc |= ATCSPI200_TRANSCTRL_TRANSMODE_R_ONLY;
	else
		tc |= ATCSPI200_TRANSCTRL_TRANSMODE_W_ONLY;

	if(rx_ptr)
		olen = spi->tran_len;

	andes_spi_write(spi,ATCSPI200_TRANSCTRL,tc);
	andes_spi_write(spi,ATCSPI200_CMD,1);

	for(i=0;i<spi->cmd_len;i++)
		andes_spi_write(spi,ATCSPI200_DATA,spi->cmd_buf[i]);

	return 0;
}

//static int andes_spi_transfer_one(struct spi_master *master,struct spi_device *spi,struct spi_transfer *t)
static int andes_spi_transfer_one(struct spi_master *master,struct spi_device *spi, struct spi_transfer *t)
{
	struct andes_spi *spi = spi_master_get_devdata(master);
	int poll = andes_spi_prep_transfer(spi, device, t);

	unsigned int event, rx_bytes;
	int num_bytes;
	int num_blks, num_chunks, max_tran_len, tran_len;
	u8 *cmd_buf = spi->cmd_buf;
	size_t cmd_len = spi->cmd_len;
	unsigned long data_len = t->len;
	int rf_cnt;
	int ret = 0;

	max_transfer_length = spi->max_transfer_length;

	const u8 *tx_ptr = NULL;
	u8 *rx_ptr = NULL;
	//flags = begin
	cmd_len = spi->cmd_len = data_len;
	memcpy(cmd_buf,data_out,cmd_len);
	//flags = end
	//flags = once
	data_out = 0;
	data_len = 0;

	


}

static int andes_spi_probe(struct platform_device *pdev)
{
	struct resource *res;
	struct spi_master *master;
	struct andes_spi *spi;
	int ret;
	u32 num_cs = NSPI_MAX_CS_NUM;

	master = spi_alloc_master(&pdev->dev, sizeof(struct andes_spi));
	if (!master) {
		dev_err(&pdev->dev, "spi_allooc_master error\n");
		return -ENOMEM;
	}

	spi = spi_master_get_devdata(master);
	init_completion(&spi->done);
	platform_set_drvdata(pdev, master);

	/* get base addr  */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	spi->regs = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(spi->regs)) {
		dev_err(&pdev->dev, "Unable to map IO resources\n");
		ret = PTR_ERR(spi->regs);
		goto put_master;
	}

	spi->to = SPI_TIMEOUT;
	spi->max_transfer_length = MAX_TRANSFER_LEN;
	spi->mtiming = andes_spi_read(spi,ATCSPI200_TIMING);

	/* get clock */
	spi->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(spi->clk)) {
		dev_err(&pdev->dev, "Unable to find bus clock\n");
		ret = PTR_ERR(spi->clk);
		goto put_master;
	}

	/* get interrupt */
	spi->irq = platform_get_irq(pdev, 0);
	if (spi->irq < 0) {
		dev_err(&pdev->dev, "Unable to find interrupt\n");
		ret = PTR_ERR(spi->irq);
		goto put_master;
	}

	/* Optional parameters */
	ret = of_property_read_u32(tsd->dev->of_node, "spi-max-frequency",&master->max_speed_hz)
	if(ret) {
		master->max_speed_hz = ANDES_MAX_HZ; /* 50MHz */
	}

	/* Spin up the bus clock before hitting registers */
	ret = clk_prepare_enable(spi->clk);
	if(ret) {
		dev_err(&pdev->dev, "Unable to enable bus clock\n");
		goto put_master;
	}
	/* probe the number of CS lines */
	ret = of_property_read_u32(of_node, "num-cs", &num_cs)
	if (ret) {
		dev_err(dev, "could not find num-cs\n");
		ret = -ENXIO;
		goto put_master;
	}
	if (num_cs > NSPI_MAX_CS_NUM) {
		dev_warn(dev, "unsupported number of cs (%i), reducing to 1\n",num_cs);
		num_cs = NSPI_MAX_CS_NUM;
	}


	/* Define our master */
	master->bus_num = pdev->id;
	master->mode_bits = SPI_CPOL | SPI_CPHA;
	master->dev.of_node = pdev->dev.of_node;
	master->num_chipselect = num_cs;
	master->transfer_one = andes_spi_transfer_one;

	/* Configure the SPI master hardware */
	andes_spi_init(spi);

	/* Register for SPI InterruptRegister for SPI Interrupt */
	ret = devm_request_irq(&pdev->dev, irq, andes_spi_irq, 0,
				dev_name(&pdev->dev), spi);
	if (ret) {
		dev_err(&pdev->dev, "Unable to bind to interrupt\n");
		goto put_master;
	}

	dev_info(&pdev->dev, "mapped; irq=%d, cs=%d\n",
		irq, master->num_chipselect);

	ret = devm_spi_register_master(&pdev->dev, master);
	if (ret < 0) {
		dev_err(&pdev->dev, "spi_register_master failed\n");
		goto put_master;
	}

	dev_info(&pdev->dev, "Andes SPI driver.\n");

	return 0;

put_master:
	spi_master_put(master);

	return ret;
}

static int andes_spi_remove(struct platform_device *pdev)
{
	struct andes_spi *spi = platform_get_drvdata(pdev);

	pm_runtime_disable(&pdev->dev);

	clk_put(spi->clk);

	return 0;
}

static const struct of_device_id andes_spi_of_match[] = {
	{ .compatible = "andestech,atcspi200", },
	{}
};
MODULE_DEVICE_TABLE(of, andes_spi_of_match);

static struct platform_driver andes_spi_driver = {
	.probe = andes_spi_probe,
	.remove = andes_spi_remove,
	.driver = {
		.name = ANDES_SPI_NAME,
		.of_match_table = andes_spi_of_match,
	},
};
module_platform_driver(andes_spi_driver);

MODULE_AUTHOR("Nylon Chen. <nylon7@andestech.com>");
MODULE_DESCRIPTION("Andes SPI driver");
MODULE_LICENSE("GPL");
