#pragma once

// Test-only dump of the P1 receive path. Compiled when P1_TEST_RX_DUMP is set.
// Prints the same lines on Arduino-esp32 2.0.14 and on pioarduino 3.x so the
// two can be compared field-for-field.

#include <Arduino.h>
#include <Preferences.h>
#include <WiFi.h>
#include <esp_idf_version.h>
#include <esp_wifi.h>
#include <soc/gpio_periph.h>
#include <soc/gpio_reg.h>
#include <soc/gpio_sig_map.h>
#include <soc/io_mux_reg.h>
#include <soc/uart_reg.h>

#ifndef GPIO_FUNC_IN_SEL_CFG_REG
#define GPIO_FUNC_IN_SEL_CFG_REG(n) (GPIO_FUNC0_IN_SEL_CFG_REG + ((n) * 4))
#endif

#if defined(CONFIG_PM_ENABLE)
#include <esp_pm.h>
#endif

namespace rxdump {
namespace {

void line(const char* s)
{
    Serial.println(s);
}

void gpio_iomux(int gpio)
{
    const uint32_t mux_addr = GPIO_PIN_MUX_REG[gpio];
    const uint32_t mux = REG_READ(mux_addr);
    const unsigned fun = (mux >> MCU_SEL_S) & MCU_SEL;
    const unsigned drv = (mux >> FUN_DRV_S) & FUN_DRV;
    const unsigned ie = (mux & FUN_IE) ? 1u : 0u;
    const unsigned wpu = (mux & FUN_PU) ? 1u : 0u;
    const unsigned wpd = (mux & FUN_PD) ? 1u : 0u;
    unsigned oe = 0;
    unsigned lvl = 0;
    if (gpio < 32) {
        oe = (REG_READ(GPIO_ENABLE_REG) >> gpio) & 1u;
        lvl = (REG_READ(GPIO_IN_REG) >> gpio) & 1u;
    } else {
        const unsigned b = static_cast<unsigned>(gpio - 32);
        oe = (REG_READ(GPIO_ENABLE1_REG) >> b) & 1u;
        lvl = (REG_READ(GPIO_IN1_REG) >> b) & 1u;
    }
    Serial.printf(
        "RXDUMP gpio%u iomux=0x%08x fun=%u ie=%u wpu=%u wpd=%u drv=%u oe=%u in=%u\n",
        static_cast<unsigned>(gpio), static_cast<unsigned>(mux), fun, ie, wpu, wpd, drv, oe,
        lvl);
}

void dump_legacy_trigger()
{
    Preferences p;
    const bool open = p.begin("cofy-config", true);
    if (!open) {
        line("RXDUMP legacy cofy-config: open failed");
        return;
    }
    auto one = [&](const char* key) {
        const bool have = p.isKey(key);
        const unsigned t = have ? static_cast<unsigned>(p.getType(key)) : 255u;
        const unsigned v = have ? p.getUInt(key, 0) : 0;
        Serial.printf("RXDUMP legacy %s present=%u nvs_type=%u value=%u\n", key,
                      have ? 1u : 0u, t, v);
    };
    one("TRG_TYPE");
    one("TRG_INT");
    p.end();
}

}  // namespace

inline void dump()
{
    line("RXDUMP begin");
    gpio_iomux(21);
    gpio_iomux(22);
    gpio_iomux(25);
    gpio_iomux(39);

    const uint32_t rx_idx = U1RXD_IN_IDX;
    const uint32_t in_cfg = REG_READ(GPIO_FUNC_IN_SEL_CFG_REG(rx_idx));
    const unsigned gpio_sel = in_cfg & 0x3Fu;
    const unsigned inv = (in_cfg >> 6) & 1u;
    const unsigned sig_in_sel = (in_cfg >> 7) & 1u;
    Serial.printf(
        "RXDUMP uart1_rx matrix idx=%u cfg=0x%08x gpio=%u sig_in_sel=%u inv=%u\n",
        static_cast<unsigned>(rx_idx), static_cast<unsigned>(in_cfg), gpio_sel, sig_in_sel,
        inv);

    const uint32_t clkdiv = REG_READ(UART_CLKDIV_REG(1));
    const uint32_t conf0 = REG_READ(UART_CONF0_REG(1));
    const uint32_t conf1 = REG_READ(UART_CONF1_REG(1));
    const unsigned div_int = clkdiv & UART_CLKDIV_V;
    const unsigned div_frag = (clkdiv >> UART_CLKDIV_FRAG_S) & UART_CLKDIV_FRAG_V;
    const unsigned rxd_inv = (conf0 & UART_RXD_INV) ? 1u : 0u;
    const unsigned txd_inv = (conf0 & UART_TXD_INV) ? 1u : 0u;
    const unsigned tick_apb = (conf0 & UART_TICK_REF_ALWAYS_ON) ? 1u : 0u;
    const unsigned full_th = (conf1 >> UART_RXFIFO_FULL_THRHD_S) & UART_RXFIFO_FULL_THRHD_V;
    const unsigned tout_en = (conf1 & UART_RX_TOUT_EN) ? 1u : 0u;
    const unsigned tout_th = (conf1 >> UART_RX_TOUT_THRHD_S) & UART_RX_TOUT_THRHD_V;
    const uint32_t apb = static_cast<uint32_t>(getApbFrequency());
    const double div = static_cast<double>(div_int) + (static_cast<double>(div_frag) / 16.0);
    const unsigned baud_apb =
        (div > 0.0) ? static_cast<unsigned>((static_cast<double>(apb) / div) + 0.5) : 0u;
    Serial.printf("RXDUMP uart1 clkdiv=0x%08x int=%u frag=%u\n", static_cast<unsigned>(clkdiv),
                  div_int, div_frag);
    Serial.printf("RXDUMP uart1 conf0=0x%08x rxd_inv=%u txd_inv=%u tick_apb=%u\n",
                  static_cast<unsigned>(conf0), rxd_inv, txd_inv, tick_apb);
    Serial.printf("RXDUMP uart1 conf1=0x%08x rx_full_thrhd=%u rx_tout_en=%u rx_tout_thrhd=%u\n",
                  static_cast<unsigned>(conf1), full_th, tout_en, tout_th);
    Serial.printf("RXDUMP uart1 baudRate()=%lu baud_from_apb=%u apb=%u\n",
                  static_cast<unsigned long>(Serial1.baudRate()), baud_apb, apb);
#ifdef UART_RX_FILT_REG
    Serial.printf("RXDUMP uart1 rx_filt=0x%08x\n",
                  static_cast<unsigned>(REG_READ(UART_RX_FILT_REG(1))));
#else
    line("RXDUMP uart1 rx_filt=n/a (no UART_RX_FILT_REG on this chip)");
#endif
#ifdef UART_CLK_CONF_REG
    Serial.printf("RXDUMP uart1 clk_conf=0x%08x\n",
                  static_cast<unsigned>(REG_READ(UART_CLK_CONF_REG(1))));
#else
    line("RXDUMP uart1 clk_conf=n/a (ESP32 UART uses CONF0 tick_ref_always_on)");
#endif
    Serial.printf("RXDUMP uart1 mem_conf=0x%08x flow_conf=0x%08x idle_conf=0x%08x\n",
                  static_cast<unsigned>(REG_READ(UART_MEM_CONF_REG(1))),
                  static_cast<unsigned>(REG_READ(UART_FLOW_CONF_REG(1))),
                  static_cast<unsigned>(REG_READ(UART_IDLE_CONF_REG(1))));

    Serial.printf("RXDUMP cpu_mhz=%u xtal_mhz=%u apb_hz=%u\n",
                  static_cast<unsigned>(getCpuFrequencyMhz()),
                  static_cast<unsigned>(getXtalFrequencyMhz()), apb);
#if defined(CONFIG_PM_ENABLE)
    {
#if ESP_IDF_VERSION_MAJOR >= 5
        esp_pm_config_t pmc{};
#else
        esp_pm_config_esp32_t pmc{};
#endif
        const esp_err_t e = esp_pm_get_configuration(&pmc);
        Serial.printf("RXDUMP pm enable=1 err=%d max=%d min=%d light_sleep=%d\n", static_cast<int>(e),
                      static_cast<int>(pmc.max_freq_mhz), static_cast<int>(pmc.min_freq_mhz),
                      pmc.light_sleep_enable ? 1 : 0);
    }
#else
    line("RXDUMP pm enable=0 (CONFIG_PM_ENABLE is not set)");
#endif

#if defined(CONFIG_UART_ISR_IN_IRAM)
    line("RXDUMP uart_isr_in_iram=1");
#else
    line("RXDUMP uart_isr_in_iram=0");
#endif

    wifi_ps_type_t ps = WIFI_PS_NONE;
    esp_wifi_get_ps(&ps);
    int8_t txq = 0;
    esp_wifi_get_max_tx_power(&txq);
    Serial.printf("RXDUMP wifi ps=%d arduino_sleep=%d tx_power_qdbm=%d\n", static_cast<int>(ps),
                  WiFi.getSleep() ? 1 : 0, static_cast<int>(txq));

    dump_legacy_trigger();
    line("RXDUMP done");
}

}  // namespace rxdump
