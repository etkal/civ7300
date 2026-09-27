/*
 * Pico LED class
 *
 * (c) 2025-2026 Erik Tkal
 *
 */

#include "led.h"

#if defined(PLATFORM_PICO_W)
#include "pico/cyw43_arch.h"
#endif
#include "ws2812.pio.h"
#include "timemgr.h"

std::deque<LED::Shared> LED::sm_mapLEDs;

// Use repeating_timer to avoid hangs in sleep_ms with pico_w
bool LED::ledOffTimerCallback(repeating_timer_t* pTimer)
{
    BlinkContext* pContext = reinterpret_cast<BlinkContext*>(pTimer->user_data);
    LED* pLed = pContext->spLED.get();
    pLed->SetPixel(pContext->idx, led_off);
    pLed->m_bUpdateRequested = true;
    return false; // cancels
}

LED::LED()
    : m_nIndexInMapLEDs(sm_mapLEDs.size())
{
    sm_mapLEDs.push_back(Shared(this));
}

LED::~LED()
{
    cancel_repeating_timer(&m_LedTimer);
    sm_mapLEDs.erase(sm_mapLEDs.begin() + m_nIndexInMapLEDs);
}

void LED::Blink_ms(uint idx, uint duration, uint32_t color)
{
    std::unique_ptr<BlinkContext> spContext = std::make_unique<BlinkContext>();
    spContext->spLED = Shared(this);
    spContext->idx = idx;
    if (color != led_off)
    {
        SetPixel(idx, color);
    }
    On(idx);
    add_repeating_timer_ms(duration, LED::ledOffTimerCallback, reinterpret_cast<void*>(spContext.release()), &m_LedTimer);
}

void LED::DoWork()
{
    if (m_bUpdateRequested)
    {
        Show();
        m_bUpdateRequested = false;
    }
}

LED::Shared LED::GetLED(uint nLEDIndex)
{
    if (nLEDIndex < sm_mapLEDs.size())
    {
        return sm_mapLEDs[nLEDIndex];
    }
    return nullptr;
}

//
// LED_pico - Raspberry Pi Pico GPIO LED support
//

LED_pico::LED_pico(uint pin)
    : m_nPin(pin),
      m_nColor(led_white)
{
}

LED_pico::LED_pico()
    : m_nPin(-1),
      m_nColor(led_white)
{
    // Default constructor for LED_pico, pin will need to be set later, used by LED_pico_w to avoid
    // calling gpio_init() in the base class constructor.
}

LED_pico::~LED_pico()
{
    Off();
    gpio_deinit(m_nPin);
}

void LED_pico::Initialize()
{
    gpio_init(m_nPin);
    gpio_set_dir(m_nPin, GPIO_OUT);
    Off();
}

void LED_pico::On(uint idx)
{
    if (idx == 0 || idx == led_all)
    {
        Show();
    }
}

void LED_pico::Off(uint idx)
{
    if (idx == 0 || idx == led_all)
    {
        m_nColor = led_off;
        Show();
    }
}

void LED_pico::Show()
{
    if (m_nColor != led_off)
    {
        gpio_put(m_nPin, LED_ON);
    }
    else
    {
        gpio_put(m_nPin, LED_OFF);
    }
}

void LED_pico::SetPixel(uint idx, uint32_t color)
{
    if (idx == 0 || idx == led_all)
    {
        for (auto i : m_vIgnore)
        {
            if (i == m_nColor)
            {
                m_nColor = led_off;
            }
        }
        m_nColor = color;
    }
}

uint32_t LED_pico::GetPixel(uint idx)
{
    return m_nColor;
}

void LED_pico::SetIgnore(std::vector<uint32_t> vIgnore)
{
    m_vIgnore = vIgnore;
}

//
// LED_pico_w - Raspberry Pi Pico W GPIO LED support
//

#if defined(PLATFORM_PICO_W)
LED_pico_w::LED_pico_w(uint pin)
{
    m_nPin = pin;
}

LED_pico_w::~LED_pico_w()
{
    Off();
}

void LED_pico_w::Initialize()
{
    Off();
}

void LED_pico_w::Show()
{
    cyw43_thread_enter();
    if (m_nColor != led_off)
    {
        cyw43_arch_gpio_put(m_nPin, 1);
    }
    else
    {
        cyw43_arch_gpio_put(m_nPin, 0);
    }
    cyw43_thread_exit();
}
#endif

//
// LED_neo - WS2812 LED support
//

static inline void put_pixel(uint32_t pixel_grb)
{
    pio_sm_put_blocking(pio0, 0, pixel_grb << 8u);
}

LED_neo::LED_neo(uint numLEDs, uint pin, uint powerPin, bool bIsRGBW)
    : m_nPin(pin),
      m_nPowerPin(powerPin),
      m_nNumLEDs(numLEDs),
      m_bIsRGBW(bIsRGBW)
{
}

LED_neo::~LED_neo()
{
    Off();
    if (0 != m_nPowerPin)
    {
        gpio_put(m_nPowerPin, 0);
        gpio_deinit(m_nPowerPin);
    }
}

void LED_neo::Initialize()
{
    PIO pio = pio0;
    uint sm = 0;
    uint offset = pio_add_program(pio, &ws2812_program);
    ws2812_program_init(pio, sm, offset, m_nPin, 800000, m_bIsRGBW);

    if (0 != m_nPowerPin)
    {
        gpio_init(m_nPowerPin);
        gpio_set_dir(m_nPowerPin, GPIO_OUT);
        gpio_put(m_nPowerPin, 1);
    }

    m_vPixels.resize(m_nNumLEDs);
    Off(led_all);
    sleep_us(300);
}

void LED_neo::On(uint idx)
{
    Show();
}

void LED_neo::Off(uint idx)
{
    SetPixel(idx, led_off);
    Show();
}

void LED_neo::Show()
{
    for (size_t i = 0; i < m_nNumLEDs; ++i)
    {
        put_pixel(m_vPixels[i]);
    }
}

void LED_neo::SetPixel(uint idx, uint32_t color)
{
    for (size_t i = 0; i < m_nNumLEDs; ++i)
    {
        if (i == idx || led_all == idx)
        {
            m_vPixels[i] = scale_color(color);
        }
    }
}

uint32_t LED_neo::GetPixel(uint idx)
{
    if (idx < m_nNumLEDs)
    {
        return m_vPixels[idx];
    }
    return led_off;
}
