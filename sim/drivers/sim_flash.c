// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Andrew Yong <me@ndoo.sg>

#define DT_DRV_COMPAT dmrtastic_sim_spi_nor

#include <errno.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/kernel.h>

#include <cmdline.h>
#include <soc.h>

#include <sim/sim_host.h>

#define SIM_FLASH_SIZE     DT_INST_PROP(0, size)
#define SIM_FLASH_SECTOR   4096U
#define SIM_FLASH_ERASED   0xFF

static uint8_t sim_flash_mem[SIM_FLASH_SIZE];
static char *sim_flash_path;

static const struct flash_parameters sim_flash_params = {
	.write_block_size = 1,
	.erase_value = SIM_FLASH_ERASED,
};

static int sim_flash_check(off_t offset, size_t len)
{
	if (offset < 0 || (size_t)offset > SIM_FLASH_SIZE || len > SIM_FLASH_SIZE - offset) {
		return -EINVAL;
	}
	return 0;
}

static int sim_flash_read(const struct device *dev, off_t offset, void *data, size_t len)
{
	ARG_UNUSED(dev);
	if (sim_flash_check(offset, len) != 0) {
		return -EINVAL;
	}
	memcpy(data, &sim_flash_mem[offset], len);
	return 0;
}

static int sim_flash_write(const struct device *dev, off_t offset, const void *data, size_t len)
{
	ARG_UNUSED(dev);
	if (sim_flash_check(offset, len) != 0) {
		return -EINVAL;
	}
	for (size_t i = 0; i < len; i++) {
		sim_flash_mem[offset + i] &= ((const uint8_t *)data)[i];
	}
	return 0;
}

static int sim_flash_erase(const struct device *dev, off_t offset, size_t size)
{
	ARG_UNUSED(dev);
	if (sim_flash_check(offset, size) != 0 || offset % SIM_FLASH_SECTOR != 0 ||
	    size % SIM_FLASH_SECTOR != 0) {
		return -EINVAL;
	}
	memset(&sim_flash_mem[offset], SIM_FLASH_ERASED, size);
	return 0;
}

static const struct flash_parameters *sim_flash_get_parameters(const struct device *dev)
{
	ARG_UNUSED(dev);
	return &sim_flash_params;
}

static int sim_flash_get_size(const struct device *dev, uint64_t *size)
{
	ARG_UNUSED(dev);
	*size = SIM_FLASH_SIZE;
	return 0;
}

#if defined(CONFIG_FLASH_PAGE_LAYOUT)
static const struct flash_pages_layout sim_flash_layout = {
	.pages_count = SIM_FLASH_SIZE / SIM_FLASH_SECTOR,
	.pages_size = SIM_FLASH_SECTOR,
};

static void sim_flash_page_layout(const struct device *dev,
				  const struct flash_pages_layout **layout, size_t *layout_size)
{
	ARG_UNUSED(dev);
	*layout = &sim_flash_layout;
	*layout_size = 1;
}
#endif

static int sim_flash_sfdp_read(const struct device *dev, off_t offset, void *data, size_t len)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(offset);
	ARG_UNUSED(data);
	ARG_UNUSED(len);
	return -ENOTSUP;
}

static int sim_flash_read_jedec_id(const struct device *dev, uint8_t *id)
{
	static const uint8_t jedec[] = DT_INST_PROP(0, jedec_id);

	ARG_UNUSED(dev);
	memcpy(id, jedec, sizeof(jedec));
	return 0;
}

static DEVICE_API(flash, sim_flash_api) = {
	.read = sim_flash_read,
	.write = sim_flash_write,
	.erase = sim_flash_erase,
	.get_parameters = sim_flash_get_parameters,
	.get_size = sim_flash_get_size,
#if defined(CONFIG_FLASH_PAGE_LAYOUT)
	.page_layout = sim_flash_page_layout,
#endif
	.sfdp_read = sim_flash_sfdp_read,
	.read_jedec_id = sim_flash_read_jedec_id,
};

static int sim_flash_init(const struct device *dev)
{
	ARG_UNUSED(dev);
	memset(sim_flash_mem, SIM_FLASH_ERASED, sizeof(sim_flash_mem));
	if (sim_flash_path != NULL &&
	    sim_host_read_file(sim_flash_path, sim_flash_mem, sizeof(sim_flash_mem)) < 0) {
		return -EIO;
	}
	return 0;
}

static void sim_flash_options(void)
{
	static struct args_struct_t opts[] = {
		{
			.option = "codeplug",
			.name = "path",
			.type = 's',
			.dest = (void *)&sim_flash_path,
			.descript = "Codeplug flash image, loaded at start and written back at exit",
		},
		ARG_TABLE_ENDMARKER,
	};

	native_add_command_line_opts(opts);
}

static void sim_flash_save(void)
{
	if (sim_flash_path != NULL) {
		sim_host_write_file(sim_flash_path, sim_flash_mem, sizeof(sim_flash_mem));
	}
}

NATIVE_TASK(sim_flash_options, PRE_BOOT_1, 1);
NATIVE_TASK(sim_flash_save, ON_EXIT, 1);

DEVICE_DT_INST_DEFINE(0, sim_flash_init, NULL, NULL, NULL, POST_KERNEL,
		      CONFIG_FLASH_INIT_PRIORITY, &sim_flash_api);
