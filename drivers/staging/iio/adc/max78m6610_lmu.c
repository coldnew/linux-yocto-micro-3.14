/*
 * max78m6610+lmu SPI protocol driver
 *
 * Copyright(c) 2013 Intel Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Contact Information:
 * Intel Corporation
 *
 * This SPI protocol driver is developed for the Maxim 78M6610+LMU (eADC).
 * The driver is developed as a part of the Clanton BSP where integrated into
 * Clanton Evaluation Boards Cross Hill Industrial-E.
 *
 * The Maxim 78M6610+LMU is an energy measurement processor (EMP) for
 * load monitoring on single or spilt-phase AC loads. It supports varies
 * interface configuration protocols through I/O pins.
 *
 * With 3 wire serial input/output interfaces provided by Clanton SoC,
 * the 78M6610+LMU can be connected directly as SPI slave device.
 */

#include <linux/iio/iio.h>
#include <linux/iio/buffer.h>
#include <linux/iio/types.h>
#include <linux/iio/trigger.h>
#include <linux/iio/trigger_consumer.h>
#include <linux/iio/triggered_buffer.h>
#include <linux/spi/spi.h>
#include <linux/version.h>

#define INSTAN_VA       0x33 /* instaneous Voltage for VA source */
#define INSTAN_IA       0x44 /* instaneous Current for IA source */
#define INSTAN_PA       0x5C /* instaneous Active Power for source A*/
#define INSTAN_PQA      0x5E /* instaneous Reactive Power for source A*/
#define VA_RMS          0x2B /* RMS voltage for VA source */
#define IA_RMS          0x3E /* RMS current for VA source */
#define WATT_A          0x4B /* Active Power for source A */
#define VAR_A           0x51 /* Reactive power for source A */
#define VA_A            0x4E /* Volt-Amperes for source A */
#define PFA             0x65 /* Source A Power Factor */

#define INSTAN_VB       0x34 /* instaneous Voltage for VB source */
#define INSTAN_IB       0x45 /* instaneous Current for IB source */
#define INSTAN_PB       0x5D /* instaneous Active Power for source B*/
#define INSTAN_PQB      0x5F /* instaneous Voltage for VB source */
#define VB_RMS          0x2C /* RMS voltage for VB source */
#define IB_RMS          0x3F /* RMS current for VB source */
#define WATT_B          0x4C /* Active Power for source B */
#define VAR_B           0x52 /* Reactive power for source B */
#define VA_B            0x4F /* Volt-amperes for source B */
#define PFB             0x66 /* Source B Power Factor */

/* Addr bit 6-7: ADDR6, ADDR7 */
#define SPI_CB_ADDR_MASK_7_6(x)	(((x) & 0xC0) >> 6)
/* Addr bit 0 - 5 */
#define SPI_TB_ADDR_MASK_5_0(x)	((x) & 0x3F)

#define SPI_CB_NBR_ACC	0x00	/* number register of accesss, limit to 1 */
#define SPI_CB_CMD	0x01	/* SPI command flag */
#define SPI_OP_READ	0x00	/* bit 1: Read/Write RD:0 W:1 */
#define SPI_OP_WRITE	0x02	/* bit 1: Read/Write RD:0 W:1 */
/* Positive / negative conversion */
#define SIGN_CONVERT	0xFFFFFFFFFFFFFFFF
#define DATA_BIT_MASK	0x00FFFFFF
#define SIGN_BIT_NUM	23
#define SPI_MSG_LEN	5
#define RX_OFFSET	1

/* SPI message Control byte */
#define SPI_CB(x)	((SPI_CB_NBR_ACC << 4)\
			| (SPI_CB_ADDR_MASK_7_6(x) << 2)\
			| SPI_CB_CMD)
/* SPI message Transaction byte */
#define SPI_TB_READ(x)	((SPI_TB_ADDR_MASK_5_0(x) << 2)\
			| SPI_OP_READ)


/**
 * max78m6610_lmu_channels structure maps eADC measurement features to
 * correlates IIO channels
 */
static const struct iio_chan_spec max78m6610_lmu_channels[] = {
	/* IIO Channels for source A */
	{
		.type = IIO_VOLTAGE,
		.indexed = 1,
		.channel = 0,
		.extend_name = "inst",
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
		.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE),
		.address = INSTAN_VA,
		.scan_index = 0,
		.scan_type = {
			.sign = 's',
			.realbits = 32,
			.storagebits = 32,
			.shift = 23,
		},
	},
	{
		.type = IIO_CURRENT,
		.indexed = 1,
		.channel = 0,
		.extend_name = "rms",
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
		.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE),
		.address = IA_RMS,
		.scan_index = 1,
		.scan_type = {
			.sign = 's',
			.realbits = 32,
			.storagebits = 32,
			.shift = 23,
		},
	},
	{
		.type = IIO_POWER,
		.indexed = 1,
		.channel = 0,
		.extend_name = "inst_act",
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
		.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE),
		.address = INSTAN_PA,
		.scan_index = 2,
		.scan_type = {
			.sign = 's',
			.realbits = 32,
			.storagebits = 32,
			.shift = 23,
		},
	},
	{
		.type = IIO_POWER,
		.indexed = 1,
		.channel = 0,
		.extend_name = "inst_react",
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
		.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE),
		.address = INSTAN_PQA,
		.scan_index = 3,
		.scan_type = {
			.sign = 's',
			.realbits = 32,
			.storagebits = 32,
			.shift = 23,
		},
	},
	{
		.type = IIO_POWER,
		.indexed = 1,
		.channel = 0,
		.extend_name = "avg_act",
		/* IIO_CHAN_INFO_AVERAGE_RAW is not used here,
		 * this average value is provide by HW register,
		 */
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
		.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE),
		.address = WATT_A,
		.scan_index = 4,
		.scan_type = {
			.sign = 's',
			.realbits = 32,
			.storagebits = 32,
			.shift = 23,
		},
	},
	{
		.type = IIO_POWER,
		.indexed = 1,
		.channel = 0,
		.extend_name = "avg_react",
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
		.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE),
		.address = VAR_A,
		.scan_index = 5,
		.scan_type = {
			.sign = 's',
			.realbits = 32,
			.storagebits = 32,
			.shift = 23,
		},
	},
	{
		.type = IIO_POWER,
		.indexed = 1,
		.channel = 0,
		.extend_name = "apparent",
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
		.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE),
		.address = VA_A,
		.scan_index = 6,
		.scan_type = {
			.sign = 's',
			.realbits = 32,
			.storagebits = 32,
			.shift = 23,
		},
	},
	{
		.type = IIO_POWER,
		.indexed = 1,
		.channel = 0,
		.extend_name = "factor",
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
		.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE),
		.address = PFA,
		.scan_index = 7,
		.scan_type = {
			.sign = 's',
			.realbits = 32, /* data type S.22 */
			.storagebits = 32,
			.shift = 22,
		},
	},
	{
		.type = IIO_VOLTAGE,
		.indexed = 1,
		.channel = 0,
		.extend_name = "rms",
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
		.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE),
		.address = VA_RMS,
		.scan_index = 8,
		.scan_type = {
			.sign = 's',
			.realbits = 32,
			.storagebits = 32,
			.shift = 23,
		},
	},

	/* IIO channels for source B */
	{
		.type = IIO_VOLTAGE,
		.indexed = 1,
		.channel = 1,
		.extend_name = "inst",
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
		.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE),
		.address = INSTAN_VB,
		.scan_index = 9,
		.scan_type = {
			.sign = 's',
			.realbits = 32,
			.storagebits = 32,
			.shift = 23,
		},
	},
	{
		.type = IIO_CURRENT,
		.indexed = 1,
		.channel = 1,
		.extend_name = "rms",
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
		.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE),
		.address = IB_RMS,
		.scan_index = 10,
		.scan_type = {
			.sign = 's',
			.realbits = 32,
			.storagebits = 32,
			.shift = 23,
		},
	},
	{
		.type = IIO_POWER,
		.indexed = 1,
		.channel = 1,
		.extend_name = "inst_act",
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
		.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE),
		.address = INSTAN_PB,
		.scan_index = 11,
		.scan_type = {
			.sign = 's',
			.realbits = 32,
			.storagebits = 32,
			.shift = 23,
		},
	},
	{
		.type = IIO_POWER,
		.indexed = 1,
		.channel = 1,
		.extend_name = "inst_react",
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
		.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE),
		.address = INSTAN_PQB,
		.scan_index = 12,
		.scan_type = {
			.sign = 's',
			.realbits = 32,
			.storagebits = 32,
			.shift = 23,
		},
	},
	{
		.type = IIO_POWER,
		.indexed = 1,
		.channel = 1,
		.extend_name = "avg_act",
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
		.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE),
		.address = WATT_B,
		.scan_index = 13,
		.scan_type = {
			.sign = 's',
			.realbits = 32,
			.storagebits = 32,
			.shift = 23,
		},
	},
	{
		.type = IIO_POWER,
		.indexed = 1,
		.channel = 1,
		.extend_name = "avg_react",
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
		.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE),
		.address = VAR_B,
		.scan_index = 14,
		.scan_type = {
			.sign = 's',
			.realbits = 32,
			.storagebits = 32,
			.shift = 23,
		},
	},
	{
		.type = IIO_POWER,
		.indexed = 1,
		.channel = 1,
		.extend_name = "apparent",
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
		.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE),
		.address = VA_B,
		.scan_index = 15,
		.scan_type = {
			.sign = 's',
			.realbits = 32,
			.storagebits = 32,
			.shift = 23,
		},
	},
	{
		.type = IIO_POWER,
		.indexed = 1,
		.channel = 1,
		.extend_name = "factor",
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
		.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE),
		.address = PFB,
		.scan_index = 16,
		.scan_type = {
			.sign = 's',
			.realbits = 32, /* data type S.22 */
			.storagebits = 32,
			.shift = 22,
		},
	},
	{
		.type = IIO_VOLTAGE,
		.indexed = 1,
		.channel = 1,
		.extend_name = "rms",
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
		.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE),
		.address = VB_RMS,
		.scan_index = 17,
		.scan_type = {
			.sign = 's',
			.realbits = 32,
			.storagebits = 32,
			.shift = 23,
		},
	},
	{
		.type = IIO_CURRENT,
		.indexed = 1,
		.channel = 0,
		.extend_name = "inst",
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
		.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE),
		.address = INSTAN_IA,
		.scan_index = 18,
		.scan_type = {
			.sign = 's',
			.realbits = 32,
			.storagebits = 32,
			.shift = 23,
		},
	},
	{
		.type = IIO_CURRENT,
		.indexed = 1,
		.channel = 1,
		.extend_name = "inst",
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
		.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE),
		.address = INSTAN_IB,
		.scan_index = 19,
		.scan_type = {
			.sign = 's',
			.realbits = 32,
			.storagebits = 32,
			.shift = 23,
		},
	},

	IIO_CHAN_SOFT_TIMESTAMP(20),
};

/* max number of iio channels */
#define MAX_CHAN_NUM		ARRAY_SIZE(max78m6610_lmu_channels)

/* eADC state structure */
struct max78m6610_lmu_state {
	struct spi_device	*spi;
	struct iio_dev_attr	*iio_attr;
	struct spi_transfer	ring_xfer[MAX_CHAN_NUM];
	struct spi_transfer	scan_single_xfer;
	struct spi_message	ring_msg;
	struct spi_message	scan_single_msg;

	u8	tx_buf[SPI_MSG_LEN * MAX_CHAN_NUM];
	u8	rx_buf[SPI_MSG_LEN * MAX_CHAN_NUM + sizeof(s64)];
};

/**
 * ret_fraction_log2
 *
 * @param val: pointer to val
 * @param val2: pointer to val2
 * @return: no returns
 *
 * this function to re-implement of IIO_VAL_FRACTIONAL_LOG2 marco in IIO
 * because of the do_div() function is not correctly handle the negative
 * input value.
 */
static void ret_fraction_log2(int *val, int *val2)
{
	s64 tmp;

	tmp = *val;

	if (*val < 0) {
		pr_debug("%s: before shr: tmp=0x%016llX, *val=0x%08X, tmp=%lld, *val=%d\n",
				__func__, tmp, *val, tmp, *val);
		/* the do_div function will return trash if the value
		 * of input is negative. We need to treat tmp as
		 * a positive number for calculation.
		 * 1. XOR tmp with 0xFFFFFFFFFFFFFFFF.
		 * 2. add on the differential
		 */
		tmp = (tmp ^ SIGN_CONVERT) + 1;
		tmp = tmp * 1000000000LL >> (*val2);
		*val2 = do_div(tmp, 1000000000LL);
		*val = tmp;
		/* the IIO_VAL_INT_PLUS_NANO marco is used in the later stage
		 * to return the proper format of output.
		 * The IIO use the value of val2 to determinate the sign
		 * of the output.
		 * Convert val2 from positive to negative to fool IIO to
		 * display the
		 * correct output format.
		 */
		*val2 = *val2 ^ SIGN_CONVERT;
		pr_debug("%s: at the end: *val=0x%08x, tmp=%lld, *val=%d\n",
				__func__, *val, tmp, *val);

	} else {

		tmp = tmp * 1000000000LL >> (*val2);
		*val2 = do_div(tmp, 1000000000LL);
		*val = tmp;
	}
}

/**
 * max78m6610_lmu_update_scan_mode
 *
 * @param indio_dev: iio_dev pointer.
 * @param active_scan_mask: pointer to scan mask.
 * @return 0 on success or standard errnos on failure
 *
 * setup the spi transfer buffer for the actived scan mask
 **/
static int max78m6610_lmu_update_scan_mode(struct iio_dev *indio_dev,
	const unsigned long *active_scan_mask)
{
	struct max78m6610_lmu_state *st = iio_priv(indio_dev);
	int i, tx = 0, k = 0;
	unsigned addr;

	spi_message_init(&st->ring_msg);

	/* scan through all the channels */
	for (i = 0; i < MAX_CHAN_NUM; i++) {
		/* we build the the spi message here that support
		 * multiple register access request on the selected channel */
		if (test_bit(i, active_scan_mask)) {
			addr = max78m6610_lmu_channels[i].address;
			/* first two bytes are the contol bytes */
			st->tx_buf[tx] = SPI_CB(addr);
			st->tx_buf[tx+1] = SPI_TB_READ(addr);

			st->ring_xfer[k].cs_change = 0;
			st->ring_xfer[k].tx_buf = &st->tx_buf[tx];
			/* rx buffer */
			/* All the HW registers in the HW are designed as 24 bit
			 * size, so we skip the first byte in the rx_buf when
			 * constructing the ring_xfer.
			 */
			st->ring_xfer[k].rx_buf = &st->rx_buf[tx];
			st->ring_xfer[k].len = SPI_MSG_LEN;
			st->ring_xfer[k].cs_change = 1;

			spi_message_add_tail(&st->ring_xfer[k],
					&st->ring_msg);
			/* update in bytes number */
			tx += SPI_MSG_LEN;
			k++;
		}
	}

	return 0;
}

/**
 * max78m6610_lmu_trigger_handle
 *
 * @param irq: irq indicator
 * @parma p: iio pull funciton pointer
 * @return IRQ_HANDLED
 *
 * bh handler of trigger launched polling to ring buffer
 *
 **/
static irqreturn_t max78m6610_lmu_trigger_handler(int irq, void *p)
{
	struct iio_poll_func *pf = p;
	struct iio_dev *indio_dev = pf->indio_dev;
	struct max78m6610_lmu_state *st = iio_priv(indio_dev);

	u32 scan_buf[((sizeof(u32)*MAX_CHAN_NUM)+sizeof(s64))/sizeof(u32)];
	s64 time_ns = 0;
	int b_sent;
	int i = 0, rx_bit = 0;
	int scan_count;

	b_sent = spi_sync(st->spi, &st->ring_msg);
	if (b_sent) {
		pr_err("spi_sync failed.\n");
		goto done;
	}

	scan_count = bitmap_weight(indio_dev->active_scan_mask,
				   indio_dev->masklength);

	if (indio_dev->scan_timestamp) {
		time_ns = iio_get_time_ns();
		memcpy((u8 *)scan_buf + indio_dev->scan_bytes - sizeof(s64),
			&time_ns, sizeof(time_ns));
	}

	for (i = 0; i < scan_count; i++) {
		u32 *rx_buf_32 = NULL;
		rx_bit = i*SPI_MSG_LEN + RX_OFFSET;
		rx_buf_32 = (u32 *)&(st->rx_buf[rx_bit]);
		*rx_buf_32 = be32_to_cpu(*rx_buf_32) & DATA_BIT_MASK;
		scan_buf[i] = sign_extend32(*rx_buf_32,
				SIGN_BIT_NUM);
	}

	iio_push_to_buffers(indio_dev, (u8 *)scan_buf);
done:
	iio_trigger_notify_done(indio_dev->trig);

	return IRQ_HANDLED;
}

/**
 * max78m6610_lmu_scan_direct
 *
 * @param st: max78m6610 state structure pointer
 * @param addr: register address
 * @return: signed 32-bits result value or standard errno on failure.
 *
 * buildup SPI message to scan HW register based on input address.
 */
static int max78m6610_lmu_scan_direct(struct max78m6610_lmu_state *st,
				unsigned addr)
{
	int ret;
	u32 *rx_buf_32 = NULL;

	pr_debug("build SPI request msg to addr 0x%02x\n", addr);

	st->tx_buf[0] = SPI_CB(addr);
	st->tx_buf[1] = SPI_TB_READ(addr);

	ret = spi_sync(st->spi, &st->scan_single_msg);
	if (ret) {
		pr_err("spi_sync return non-zero value\n");
		return -EIO;
	}

	rx_buf_32 = (uint32_t *)&(st->rx_buf[RX_OFFSET]);
	*rx_buf_32 = be32_to_cpu(*rx_buf_32) & DATA_BIT_MASK;

	ret = sign_extend32(*rx_buf_32, SIGN_BIT_NUM);

	return ret;
}

/**
 * max78m6610_lmu_read_raw
 *
 * @param indio_dev: iio_dev pointer
 * @param chan: pointer to iio channel spec struct
 * @param val: return value pointer
 * @param val2: return value 2 ponter
 * @parma m: read mask
 * @return: IIO value type
 *
 * This function will be invoked when request a value form the device.
 * Read mask specifies which value, return value will specify the type of
 * value returned from device, val and val2 will contains the elements
 * making up the return value.
 */
static int max78m6610_lmu_read_raw(struct iio_dev *indio_dev,
			   struct iio_chan_spec const *chan,
			   int *val,
			   int *val2,
			   long m)
{
	int ret;
	struct max78m6610_lmu_state *st = iio_priv(indio_dev);

	switch (m) {

	case IIO_CHAN_INFO_RAW:
		mutex_lock(&indio_dev->mlock);
		if (indio_dev->currentmode == INDIO_BUFFER_TRIGGERED) {
			ret = -EBUSY;
			return ret;
		} else {
			ret = max78m6610_lmu_scan_direct(st, chan->address);
		}
		mutex_unlock(&indio_dev->mlock);

		*val = ret;
		*val2 = chan->scan_type.shift;

		ret_fraction_log2(val, val2);
		return IIO_VAL_INT_PLUS_NANO;

		/* the full scale units : -1.0 to 1-LSB (0x7FFFFF)
		 * As an example, if 230V-peak at the input to the voltage
		 * divider gives 250mV-peak at the chip input, one would get a
		 * full scale register reading of 1 - LSB (0x7FFFFF) for
		 * instaneous voltage.
		 * Similarly, if 30Apk at the sensor input provides 250mV-peak
		 * to the chip input, a full scale register value of 1 - LSB
		 * (0x7FFFFF) for instanteous current would correspond to
		 * 30 amps.
		 * Full scale watts correspond to the result of full scale
		 * current and voltage so, in this example, full scale watts
		 * is 230 x 30 or 6900 watts.
		 */

	case IIO_CHAN_INFO_SCALE:
		switch (chan->type) {
		case IIO_CURRENT:
			*val = 250; /* unit mV */
			return IIO_VAL_INT;

		case IIO_VOLTAGE:
			*val = 250; /* unit: mV */
			return IIO_VAL_INT;

		case IIO_POWER:
			*val = 250*250; /* uV */
			return IIO_VAL_INT;

		default:
			return -EINVAL;
		}

	}
	return -EINVAL;
}

/* Driver specific iio info structure */
static const struct iio_info max78m6610_lmu_info = {
	.read_raw = max78m6610_lmu_read_raw,
	.update_scan_mode = max78m6610_lmu_update_scan_mode,
	.driver_module = THIS_MODULE,
};

/**
 * max78m6610_lmu_probe
 *
 * @param spi: spi device pointer
 * @return: return 0 or standard errorids if failure
 *
 * device driver probe funciton for iio_dev struct initialisation.
 */
static int max78m6610_lmu_probe(struct spi_device *spi)
{
	struct max78m6610_lmu_state *st;
	struct iio_dev *indio_dev = iio_device_alloc(sizeof(*st));
	int ret;

	if (indio_dev == NULL)
		return -ENOMEM;
	st = iio_priv(indio_dev);

	spi_set_drvdata(spi, indio_dev);
	st->spi = spi;

	indio_dev->name = spi_get_device_id(spi)->name;
	indio_dev->dev.parent = &spi->dev;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->channels = max78m6610_lmu_channels;
	indio_dev->num_channels = ARRAY_SIZE(max78m6610_lmu_channels);
	indio_dev->info = &max78m6610_lmu_info;

	/* Setup default message */
	st->scan_single_xfer.tx_buf = &st->tx_buf[0];
	st->scan_single_xfer.rx_buf = &st->rx_buf[0];
	st->scan_single_xfer.len = SPI_MSG_LEN;

	spi_message_init(&st->scan_single_msg);
	spi_message_add_tail(&st->scan_single_xfer, &st->scan_single_msg);

	ret = iio_triggered_buffer_setup(indio_dev, NULL,
			&max78m6610_lmu_trigger_handler, NULL);
	if (ret) {
		pr_err("triger buffer setup failed !\n");
		goto error_free;
	}

	pr_debug("%s: alloc dev id: %d\n", __func__, indio_dev->id);
	ret = iio_device_register(indio_dev);
	if (ret)
		goto error_cleanup_ring;

	return 0;

error_cleanup_ring:
	iio_triggered_buffer_cleanup(indio_dev);
error_free:
	iio_device_free(indio_dev);

	return ret;
}

/**
 * max78m6610_lmu_remove
 *
 * @param spi: spi device pointer
 * @return: return 0
 *
 * iio device unregister & cleanup
 */
static int max78m6610_lmu_remove(struct spi_device *spi)
{
	struct iio_dev *indio_dev = spi_get_drvdata(spi);

	iio_device_unregister(indio_dev);
	iio_triggered_buffer_cleanup(indio_dev);
	iio_device_free(indio_dev);

	return 0;
}

static const struct spi_device_id max78m6610_lmu_id[] = {
	{"max78m6610_lmu", 0},
	{}
};
MODULE_DEVICE_TABLE(spi, max78m6610_lmu_id);

static struct spi_driver max78m6610_lmu_driver = {
	.driver = {
		.name	= "max78m6610_lmu",
		.owner	= THIS_MODULE,
	},
	.probe		= max78m6610_lmu_probe,
	.remove		= max78m6610_lmu_remove,
	.id_table	= max78m6610_lmu_id,
};

/**
 * max78m6610_lmu_init
 *
 * device driver module init
 */
static __init int max78m6610_lmu_init(void)
{
	int ret;

	ret = spi_register_driver(&max78m6610_lmu_driver);
	if (ret < 0)
		return ret;

	return 0;
}
module_init(max78m6610_lmu_init);

/**
 * max78m6610_lmu_exit
 *
 * device driver module exit
 */
static __exit void max78m6610_lmu_exit(void)
{
	spi_unregister_driver(&max78m6610_lmu_driver);
}
module_exit(max78m6610_lmu_exit);


MODULE_AUTHOR("Kai Ji <kai.ji@emutex.com>");
MODULE_DESCRIPTION("Maxim 78M6610+LMU eADC");
MODULE_LICENSE("GPL v2");
