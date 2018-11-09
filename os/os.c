/* Power management and DVFS for STM32L0 */

#include <stdbool.h>
#include <assert.h>

#include <libopencm3/cm3/cortex.h>
#include <libopencm3/cm3/scb.h>

#include <libopencm3/stm32/rcc.h>
#include <libopencm3/stm32/pwr.h>
#include <libopencm3/stm32/gpio.h>
#include <libopencm3/stm32/flash.h>

#include <os/os.h>


/* Maximum speed shortens DW1000 active time, might be beneficial */
void os_dvfs_hsi16(void) {
  cm_disable_interrupts();
  rcc_osc_on(RCC_HSI16);

  flash_set_ws(FLASH_ACR_LATENCY_1WS); /* 1WS in range 2, 0WS in range 1 */
  rcc_periph_clock_enable(RCC_PWR);
  pwr_set_vos_scale(PWR_SCALE2);
  rcc_periph_clock_disable(RCC_PWR);

  rcc_wait_for_osc_ready(RCC_HSI16);
  rcc_set_sysclk_source(RCC_HSI16);
  rcc_osc_off(RCC_MSI);

  rcc_ahb_frequency   = 16000000;
  rcc_apb1_frequency  = 16000000;
  rcc_apb2_frequency  = 16000000;

  cm_enable_interrupts();
  tick_setup();
}

static const unsigned msi_range[] = {
  RCC_ICSCR_MSIRANGE_65KHZ, RCC_ICSCR_MSIRANGE_131KHZ, RCC_ICSCR_MSIRANGE_262KHZ, RCC_ICSCR_MSIRANGE_524KHZ, RCC_ICSCR_MSIRANGE_1MHZ, RCC_ICSCR_MSIRANGE_2MHZ, RCC_ICSCR_MSIRANGE_4MHZ,
};

static const unsigned msi_freq[] = {
  65536, 131072, 262144, 524288, 1048576, 2097152, 4194304,
};

static_assert(ARRAY_SIZE(msi_range) == ARRAY_SIZE(msi_freq), "Check msi_range[] and msi_freq[]!");

void os_dvfs_msi(unsigned f) {
  unsigned i;

  for (i = 0; i < ARRAY_SIZE(msi_freq); i ++) {
    if (msi_freq[i] > f) {
      break;
    }
  }

  cm_disable_interrupts();
  rcc_osc_on(RCC_MSI);
  RCC_ICSCR = (RCC_ICSCR & (~(RCC_ICSCR_MSIRANGE_MASK << RCC_ICSCR_MSIRANGE_SHIFT))) | (msi_range[i] << RCC_ICSCR_MSIRANGE_SHIFT);

  rcc_wait_for_osc_ready(RCC_MSI);
  rcc_set_sysclk_source(RCC_MSI);
  rcc_osc_off(RCC_HSI16);

  rcc_periph_clock_enable(RCC_PWR);
  pwr_set_vos_scale(PWR_SCALE3);
  rcc_periph_clock_disable(RCC_PWR);
  flash_set_ws(FLASH_ACR_LATENCY_0WS);

  rcc_ahb_frequency   = msi_freq[i];
  rcc_apb1_frequency  = msi_freq[i];
  rcc_apb2_frequency  = msi_freq[i];

  cm_enable_interrupts();
  tick_setup();
}

/* NOTE: LSI is for IWDG/RTC only */


void os_init(void) {
  flash_prefetch_disable(); /* Slightly slower but more energy-efficient */

  /* Configure GPIO for LP */
  rcc_periph_clock_enable(RCC_GPIOA);
  //gpio_mode_setup(GPIOA, GPIO_MODE_ANALOG, GPIO_PUPD_NONE, GPIO_ALL);
  gpio_mode_setup(GPIOA, GPIO_MODE_ANALOG, GPIO_PUPD_NONE, GPIO_ALL & (~(GPIO13 | GPIO14))); /* Keep SWD active during development */
  rcc_periph_clock_disable(RCC_GPIOA);
  //
  rcc_periph_clock_enable(RCC_GPIOB);
  gpio_mode_setup(GPIOB, GPIO_MODE_ANALOG, GPIO_PUPD_NONE, GPIO_ALL);
  rcc_periph_clock_disable(RCC_GPIOB);
  //
  rcc_periph_clock_enable(RCC_GPIOC);
  gpio_mode_setup(GPIOC, GPIO_MODE_ANALOG, GPIO_PUPD_NONE, GPIO_ALL);
  rcc_periph_clock_disable(RCC_GPIOC);

  rcc_periph_clock_enable(RCC_PWR);
  pwr_voltage_regulator_low_power_in_stop();
  PWR_CR |= PWR_CR_LPSDSR;
  //PWR_CR |= PWR_CR_LPRUN; /* NOTE: LPRUN is for kHz only, and does not work for our system */
  rcc_periph_clock_disable(RCC_PWR);

  rcc_set_hpre(RCC_CFGR_HPRE_NODIV); /* Actually very marginal, cannot afford to be any slower */
  rcc_set_ppre1(RCC_CFGR_PPRE1_NODIV); /* We are not using anything on it, the bridge should be off */
  rcc_set_ppre2(RCC_CFGR_PPRE2_NODIV);

  os_dvfs_msi(4000000); /* Best for configuring DW1000 */
}

void os_halt(void) {
  rcc_periph_clock_enable(RCC_PWR);
  pwr_disable_power_voltage_detect();
  pwr_disable_wakeup_pin();

  /* According to ST App note... */
  SCB_SCR |= SCB_SCR_SLEEPDEEP;
  /* Only sets PDDS... Well, others are universal to CM3 anyway. */
  pwr_set_standby_mode();
  pwr_clear_wakeup_flag();
  /* Enter standby */
  asm("wfi");
}

void os_panic(void) {
  while (true) {
    asm("nop");
  }
}

void nmi_handler(void)
__attribute__ ((alias ("os_panic")));

void hard_fault_handler(void)
__attribute__ ((alias ("os_panic")));
